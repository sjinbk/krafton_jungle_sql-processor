#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* storage layer는 SQL 의미 해석이 아니라 CSV 파일 포맷 계약을 지키는 역할을 맡는다. */
static int ensure_text_no_newline(const char *text) {
    while (*text != '\0') {
        if (*text == '\n' || *text == '\r') {
            return 0;
        }
        text++;
    }
    return 1;
}

/* writer는 INT는 그대로, TEXT는 quote + quote escaping 규칙으로 기록한다. */
static int csv_write_value(FILE *file, const SqlValue *value, Error *error) {
    const char *text;

    if (value->type == SQL_VALUE_INT) {
        if (fprintf(file, "%ld", value->int_value) < 0) {
            error_set(error, ERR_IO, 0, 0, "failed to write integer value");
            return 0;
        }
        return 1;
    }

    text = value->text_value;
    if (!ensure_text_no_newline(text)) {
        error_set(error, ERR_STORAGE, 0, 0, "TEXT values may not contain newlines in CSV storage");
        return 0;
    }

    if (fputc('"', file) == EOF) {
        error_set(error, ERR_IO, 0, 0, "failed to write text value");
        return 0;
    }
    while (*text != '\0') {
        if (*text == '"') {
            if (fputc('"', file) == EOF || fputc('"', file) == EOF) {
                error_set(error, ERR_IO, 0, 0, "failed to escape text value");
                return 0;
            }
        } else if (fputc(*text, file) == EOF) {
            error_set(error, ERR_IO, 0, 0, "failed to write text value");
            return 0;
        }
        text++;
    }
    if (fputc('"', file) == EOF) {
        error_set(error, ERR_IO, 0, 0, "failed to finish text value");
        return 0;
    }
    return 1;
}

/* INT field는 임시 버퍼로 복사한 뒤 strtol로 끝까지 검증한다. */
static int parse_int_field(const char *start, size_t length, SqlValue *value, Error *error) {
    char *buffer;
    char *end = NULL;

    if (length == 0) {
        error_set(error, ERR_STORAGE, 0, 0, "empty INT field in CSV row");
        return 0;
    }

    buffer = (char *)malloc(length + 1);
    if (buffer == NULL) {
        error_set(error, ERR_MEMORY, 0, 0, "out of memory while parsing CSV row");
        return 0;
    }
    memcpy(buffer, start, length);
    buffer[length] = '\0';

    value->type = SQL_VALUE_INT;
    value->int_value = strtol(buffer, &end, 10);
    value->text_value = NULL;
    if (end == NULL || *end != '\0') {
        error_set(error, ERR_STORAGE, 0, 0, "invalid INT field '%s' in CSV row", buffer);
        free(buffer);
        return 0;
    }

    free(buffer);
    return 1;
}

/* TEXT field는 "..." 형식을 강제하고 doubled quote를 원래 문자로 복원한다. */
static int parse_text_field(const char **cursor_ptr, SqlValue *value, Error *error) {
    const char *cursor = *cursor_ptr;
    size_t out_length = 0;
    char *buffer;

    if (*cursor != '"') {
        error_set(error, ERR_STORAGE, 0, 0, "TEXT field must be quoted in CSV row");
        return 0;
    }
    cursor++;

    while (cursor[out_length] != '\0') {
        if (cursor[out_length] == '"' && cursor[out_length + 1] != '"') {
            break;
        }
        if (cursor[out_length] == '"' && cursor[out_length + 1] == '"') {
            out_length += 2;
        } else {
            out_length += 1;
        }
    }

    if (cursor[out_length] != '"') {
        error_set(error, ERR_STORAGE, 0, 0, "unterminated TEXT field in CSV row");
        return 0;
    }

    buffer = (char *)malloc(out_length + 1);
    if (buffer == NULL) {
        error_set(error, ERR_MEMORY, 0, 0, "out of memory while parsing CSV row");
        return 0;
    }

    {
        size_t read_index = 0;
        size_t write_index = 0;
        while (read_index < out_length) {
            if (cursor[read_index] == '"' && cursor[read_index + 1] == '"') {
                buffer[write_index++] = '"';
                read_index += 2;
            } else {
                buffer[write_index++] = cursor[read_index++];
            }
        }
        buffer[write_index] = '\0';
    }

    value->type = SQL_VALUE_TEXT;
    value->int_value = 0;
    value->text_value = buffer;
    *cursor_ptr = cursor + out_length + 1;
    return 1;
}

/* schema의 각 column type이 CSV 각 field를 어떻게 읽을지 결정한다. */
static int csv_parse_line(const char *line, const TableSchema *schema, DataRow *row, Error *error) {
    const char *cursor = line;
    size_t i;

    row->values = NULL;
    row->count = 0;

    for (i = 0; i < schema->column_count; ++i) {
        SqlValue value;
        SqlValue *new_values;
        const char *field_start = cursor;

        if (schema->columns[i].type == SQL_VALUE_TEXT) {
            if (!parse_text_field(&cursor, &value, error)) {
                data_row_free(row);
                return 0;
            }
        } else {
            while (*cursor != '\0' && *cursor != ',' && *cursor != '\n' && *cursor != '\r') {
                cursor++;
            }
            if (!parse_int_field(field_start, (size_t)(cursor - field_start), &value, error)) {
                data_row_free(row);
                return 0;
            }
        }

        new_values = (SqlValue *)realloc(row->values, sizeof(SqlValue) * (row->count + 1));
        if (new_values == NULL) {
            sql_value_free(&value);
            data_row_free(row);
            error_set(error, ERR_MEMORY, 0, 0, "out of memory while parsing CSV row");
            return 0;
        }
        row->values = new_values;
        row->values[row->count++] = value;

        if (i + 1 < schema->column_count) {
            if (*cursor != ',') {
                data_row_free(row);
                error_set(error, ERR_STORAGE, 0, 0, "stored row width does not match schema");
                return 0;
            }
            cursor++;
        }
    }

    while (*cursor == '\r' || *cursor == '\n') {
        cursor++;
    }
    if (*cursor != '\0') {
        data_row_free(row);
        error_set(error, ERR_STORAGE, 0, 0, "stored row width does not match schema");
        return 0;
    }

    return 1;
}

/* blank line은 실제 row가 아니므로 건너뛴다. */
static int line_is_blank(const char *line) {
    while (*line != '\0') {
        if (*line != '\r' && *line != '\n') {
            return 0;
        }
        line++;
    }
    return 1;
}

/* append는 executor가 맞춰 둔 schema 순서 row를 CSV 한 줄로 직렬화한다. */
int storage_append_row(const char *db_root, const TableSchema *schema, const SqlValue *values, size_t value_count, Error *error) {
    char path[512];
    FILE *file;
    size_t i;

    if (value_count != schema->column_count) {
        error_set(error, ERR_EXEC, 0, 0, "row width does not match schema");
        return 0;
    }

    snprintf(path, sizeof(path), "%s/%s/%s.csv", db_root, schema->schema_name, schema->table_name);
    file = fopen(path, "ab");
    if (file == NULL) {
        error_set(error, ERR_IO, 0, 0, "failed to open table data file for append");
        return 0;
    }

    for (i = 0; i < value_count; ++i) {
        if (i > 0 && fputc(',', file) == EOF) {
            fclose(file);
            error_set(error, ERR_IO, 0, 0, "failed to write CSV delimiter");
            return 0;
        }
        if (!csv_write_value(file, &values[i], error)) {
            fclose(file);
            return 0;
        }
    }

    if (fputc('\n', file) == EOF) {
        fclose(file);
        error_set(error, ERR_IO, 0, 0, "failed to terminate CSV row");
        return 0;
    }

    fclose(file);
    return 1;
}

/* SELECT가 사용할 수 있도록 table 전체 row를 메모리 DataSet으로 로드한다. */
int storage_load_rows(const char *db_root, const TableSchema *schema, DataSet *data_set, Error *error) {
    char path[512];
    FILE *file;
    char line[1024];

    data_set->rows = NULL;
    data_set->count = 0;

    snprintf(path, sizeof(path), "%s/%s/%s.csv", db_root, schema->schema_name, schema->table_name);
    file = fopen(path, "rb");
    if (file == NULL) {
        return 1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        DataRow row;
        DataRow *new_rows;

        if (line_is_blank(line)) {
            continue;
        }

        if (!csv_parse_line(line, schema, &row, error)) {
            fclose(file);
            data_set_free(data_set);
            return 0;
        }

        new_rows = (DataRow *)realloc(data_set->rows, sizeof(DataRow) * (data_set->count + 1));
        if (new_rows == NULL) {
            fclose(file);
            data_row_free(&row);
            data_set_free(data_set);
            error_set(error, ERR_MEMORY, 0, 0, "out of memory while loading rows");
            return 0;
        }
        data_set->rows = new_rows;
        data_set->rows[data_set->count++] = row;
    }

    fclose(file);
    return 1;
}

/* nested SqlValue까지 포함해 DataRow/DataSet을 순서대로 해제한다. */
void data_row_free(DataRow *row) {
    size_t i;

    if (row == NULL) {
        return;
    }

    for (i = 0; i < row->count; ++i) {
        sql_value_free(&row->values[i]);
    }
    free(row->values);
    row->values = NULL;
    row->count = 0;
}

void data_set_free(DataSet *data_set) {
    size_t i;

    if (data_set == NULL) {
        return;
    }

    for (i = 0; i < data_set->count; ++i) {
        data_row_free(&data_set->rows[i]);
    }
    free(data_set->rows);
    data_set->rows = NULL;
    data_set->count = 0;
}
