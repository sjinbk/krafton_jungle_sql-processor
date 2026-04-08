#include "input.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    STATEMENT_SUMMARY_MAX = 60
};

/* StringBuilder는 splitter가 고정 크기 버퍼 없이 statement 텍스트를 조금씩 모을 수 있게 한다. */
typedef struct StringBuilder {
    char *data;
    size_t length;
    size_t capacity;
} StringBuilder;

/* capacity를 지수적으로 늘려 append가 반복돼도 비용이 급격히 커지지 않게 한다. */
static int builder_reserve(StringBuilder *builder, size_t needed, Error *error, int line, int column) {
    char *new_data;
    size_t new_capacity;

    if (needed <= builder->capacity) {
        return 1;
    }

    new_capacity = builder->capacity == 0 ? 64 : builder->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    new_data = (char *)realloc(builder->data, new_capacity);
    if (new_data == NULL) {
        error_set(error, ERR_MEMORY, line, column, "out of memory while reading SQL input");
        return 0;
    }

    builder->data = new_data;
    builder->capacity = new_capacity;
    return 1;
}

/* 끝에 항상 NUL을 유지해 builder 내용을 일반 C 문자열처럼 다룰 수 있게 한다. */
static int builder_append(StringBuilder *builder, char ch, Error *error, int line, int column) {
    if (!builder_reserve(builder, builder->length + 2, error, line, column)) {
        return 0;
    }
    builder->data[builder->length++] = ch;
    builder->data[builder->length] = '\0';
    return 1;
}

/* statement가 바뀔 때마다 새로 할당하지 않고 기존 메모리를 재사용한다. */
static void builder_clear(StringBuilder *builder) {
    builder->length = 0;
    if (builder->data != NULL) {
        builder->data[0] = '\0';
    }
}

/* 하나의 완성된 statement를 원본 시작 위치와 함께 chunk 목록에 복사해 저장한다. */
static int chunk_list_push(StatementChunkList *chunks, const char *text, size_t start, size_t end, int line, int column, Error *error) {
    StatementChunk *new_items;
    char *copy;
    size_t length = end - start;

    if (length == 0) {
        return 1;
    }

    copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        error_set(error, ERR_MEMORY, line, column, "out of memory while storing SQL statement");
        return 0;
    }
    memcpy(copy, text + start, length);
    copy[length] = '\0';

    new_items = (StatementChunk *)realloc(chunks->items, sizeof(StatementChunk) * (chunks->count + 1));
    if (new_items == NULL) {
        free(copy);
        error_set(error, ERR_MEMORY, line, column, "out of memory while storing SQL statement");
        return 0;
    }

    chunks->items = new_items;
    chunks->items[chunks->count].text = copy;
    chunks->items[chunks->count].line = line;
    chunks->items[chunks->count].column = column;
    chunks->count++;
    return 1;
}

/* splitter와 parser는 같은 line/column 좌표계를 공유한다. */
static void advance_position(char ch, int *line, int *column) {
    if (ch == '\n') {
        *line += 1;
        *column = 1;
    } else {
        *column += 1;
    }
}

/* statement 경계는 줄 단위가 아니라 문자 단위 규칙에 따라 결정되므로 파일 전체를 읽는다. */
int read_text_file(const char *path, char **contents, Error *error) {
    FILE *file;
    long file_size;
    size_t read_size;
    char *buffer;

    *contents = NULL;
    file = fopen(path, "rb");
    if (file == NULL) {
        error_set(error, ERR_IO, 0, 0, "failed to open SQL file '%s'", path);
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        error_set(error, ERR_IO, 0, 0, "failed to seek SQL file '%s'", path);
        return 0;
    }
    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        error_set(error, ERR_IO, 0, 0, "failed to size SQL file '%s'", path);
        return 0;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        error_set(error, ERR_IO, 0, 0, "failed to rewind SQL file '%s'", path);
        return 0;
    }

    buffer = (char *)malloc((size_t)file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        error_set(error, ERR_MEMORY, 0, 0, "out of memory while reading SQL file");
        return 0;
    }

    read_size = fread(buffer, 1, (size_t)file_size, file);
    if (read_size != (size_t)file_size) {
        free(buffer);
        fclose(file);
        error_set(error, ERR_IO, 0, 0, "failed to read SQL file '%s'", path);
        return 0;
    }

    buffer[file_size] = '\0';
    fclose(file);
    *contents = buffer;
    return 1;
}

/* tokenization 전에 statement를 먼저 나눈다.
 * string literal 바깥의 semicolon과 line comment만 경계 판단에 영향을 준다.
 */
int split_statements(const char *sql, StatementChunkList *chunks, Error *error) {
    StringBuilder builder;
    int line = 1;
    int column = 1;
    int statement_line = 1;
    int statement_column = 1;
    int has_content = 0;
    int in_string = 0;
    const char *cursor = sql;

    builder.data = NULL;
    builder.length = 0;
    builder.capacity = 0;
    chunks->items = NULL;
    chunks->count = 0;

    while (*cursor != '\0') {
        /* line comment는 statement 텍스트에는 넣지 않되, 소스 좌표는 계속 전진시킨다. */
        if (!in_string && cursor[0] == '-' && cursor[1] == '-') {
            while (*cursor != '\0' && *cursor != '\n') {
                advance_position(*cursor, &line, &column);
                cursor++;
            }
            continue;
        }

        /* 앞쪽 공백은 건너뛰어 기록된 시작 위치가 실제 SQL 첫 글자를 가리키게 한다. */
        if (!has_content && isspace((unsigned char)*cursor)) {
            advance_position(*cursor, &line, &column);
            cursor++;
            continue;
        }

        /* SQL에서는 문자열 안의 작은따옴표 두 개가 실제 quote 문자 하나를 뜻한다. */
        if (*cursor == '\'' && in_string && cursor[1] == '\'') {
            if (!builder_append(&builder, cursor[0], error, line, column) ||
                !builder_append(&builder, cursor[1], error, line, column + 1)) {
                statement_chunk_list_free(chunks);
                free(builder.data);
                return 0;
            }
            has_content = 1;
            cursor += 2;
            column += 2;
            continue;
        }

        /* statement가 문자열 리터럴로 시작할 수도 있으므로 상태를 바꾸기 전에 시작 위치를 기록한다. */
        if (*cursor == '\'' && !has_content) {
            statement_line = line;
            statement_column = column;
            has_content = 1;
        }

        /* quote 문자는 텍스트에 남기고 문자열 상태만 전환한다. */
        if (*cursor == '\'') {
            if (!builder_append(&builder, *cursor, error, line, column)) {
                statement_chunk_list_free(chunks);
                free(builder.data);
                return 0;
            }
            in_string = !in_string;
            advance_position(*cursor, &line, &column);
            cursor++;
            continue;
        }

        /* string literal 바깥의 semicolon만 현재 statement를 끝낸다. */
        if (*cursor == ';' && !in_string) {
            size_t start = 0;
            size_t end = builder.length;

            while (start < end && isspace((unsigned char)builder.data[start])) {
                start++;
            }
            while (end > start && isspace((unsigned char)builder.data[end - 1])) {
                end--;
            }

            if (!chunk_list_push(chunks, builder.data, start, end, statement_line, statement_column, error)) {
                statement_chunk_list_free(chunks);
                free(builder.data);
                return 0;
            }

            builder_clear(&builder);
            has_content = 0;
            statement_line = line;
            statement_column = column + 1;
            advance_position(*cursor, &line, &column);
            cursor++;
            continue;
        }

        /* 처음 만난 실제 SQL 문자라면 여기서 statement 시작 위치를 잡는다. */
        if (!has_content) {
            statement_line = line;
            statement_column = column;
            has_content = 1;
        }

        if (!builder_append(&builder, *cursor, error, line, column)) {
            statement_chunk_list_free(chunks);
            free(builder.data);
            return 0;
        }
        advance_position(*cursor, &line, &column);
        cursor++;
    }

    if (in_string) {
        statement_chunk_list_free(chunks);
        free(builder.data);
        error_set(error, ERR_PARSE, line, column, "unterminated string literal");
        return 0;
    }

    /* MVP 입력 계약을 분명히 유지하기 위해 비어 있지 않은 statement는 반드시 ';'로 끝나야 한다. */
    if (has_content) {
        statement_chunk_list_free(chunks);
        free(builder.data);
        error_set(error, ERR_PARSE, statement_line, statement_column, "statement must end with ';'");
        return 0;
    }

    free(builder.data);
    return 1;
}

/* 여러 statement를 시연할 때는 긴 SQL을 짧은 요약 줄로 줄여야 출력이 읽기 쉽다. */
void fprint_statement_header(FILE *stream, size_t statement_index, const char *sql) {
    char summary[STATEMENT_SUMMARY_MAX + 1];
    size_t summary_len = 0;
    int previous_was_space = 0;
    const unsigned char *cursor = (const unsigned char *)sql;

    while (*cursor != '\0') {
        unsigned char ch = *cursor++;

        if (isspace(ch)) {
            if (summary_len == 0 || previous_was_space) {
                continue;
            }
            ch = ' ';
            previous_was_space = 1;
        } else {
            previous_was_space = 0;
        }

        if (summary_len == STATEMENT_SUMMARY_MAX) {
            break;
        }

        summary[summary_len++] = (char)ch;
    }

    while (summary_len > 0 && summary[summary_len - 1] == ' ') {
        summary_len--;
    }

    if (*cursor != '\0' && summary_len >= 3) {
        summary[summary_len - 3] = '.';
        summary[summary_len - 2] = '.';
        summary[summary_len - 1] = '.';
    }

    summary[summary_len] = '\0';
    fprintf(stream, "-- Statement %lu: %s\n", (unsigned long)statement_index, summary);
}

/* chunk 목록이 소유한 statement 문자열들을 모두 해제한다. */
void statement_chunk_list_free(StatementChunkList *chunks) {
    size_t i;

    if (chunks == NULL) {
        return;
    }

    for (i = 0; i < chunks->count; ++i) {
        free(chunks->items[i].text);
    }
    free(chunks->items);
    chunks->items = NULL;
    chunks->count = 0;
}
