#include "executor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "schema.h"
#include "storage.h"

/* executor는 AST를 schema/storage와 연결해 runtime 의미 검증과 출력까지 담당한다. */
static int duplicate_value(const SqlValue *source, SqlValue *destination, Error *error) {
    destination->type = source->type;
    destination->int_value = source->int_value;
    destination->text_value = NULL;

    if (source->type == SQL_VALUE_TEXT) {
        size_t length = strlen(source->text_value);
        destination->text_value = (char *)malloc(length + 1);
        if (destination->text_value == NULL) {
            error_set(error, ERR_MEMORY, 0, 0, "out of memory while copying TEXT value");
            return 0;
        }
        memcpy(destination->text_value, source->text_value, length + 1);
    }

    return 1;
}

/* 현재 MVP 타입 검사는 exact match만 허용한다. */
static int validate_type(SqlValueType expected, const SqlValue *value) {
    return expected == value->type;
}

/* INSERT용 중간 row 버퍼를 해제한다. */
static void free_row_values(SqlValue *row_values, size_t count) {
    size_t i;
    for (i = 0; i < count; ++i) {
        sql_value_free(&row_values[i]);
    }
    free(row_values);
}

/* build_insert_row는 INSERT 값을 schema 전체 순서로 재배열하고 의미 오류를 검증한다. */
static int build_insert_row(const TableSchema *schema, const InsertStatement *statement, const ValueRow *input_row, SqlValue **output_values, Error *error) {
    SqlValue *row_values;
    int *assigned;
    size_t i;

    row_values = (SqlValue *)calloc(schema->column_count, sizeof(SqlValue));
    assigned = (int *)calloc(schema->column_count, sizeof(int));
    if (row_values == NULL || assigned == NULL) {
        free(row_values);
        free(assigned);
        error_set(error, ERR_MEMORY, 0, 0, "out of memory while preparing INSERT row");
        return 0;
    }

    if (statement->columns.count == 0) {
        /* column list가 없으면 schema 순서 전체 값을 그대로 받아야 한다. */
        if (input_row->count != schema->column_count) {
            free(row_values);
            free(assigned);
            error_set(error, ERR_EXEC, 0, 0, "INSERT value count does not match schema column count");
            return 0;
        }

        for (i = 0; i < schema->column_count; ++i) {
            if (!validate_type(schema->columns[i].type, &input_row->values[i])) {
                free_row_values(row_values, i);
                free(assigned);
                error_set(error, ERR_EXEC, 0, 0, "type mismatch for column '%s'", schema->columns[i].name);
                return 0;
            }
            if (!duplicate_value(&input_row->values[i], &row_values[i], error)) {
                free_row_values(row_values, i);
                free(assigned);
                return 0;
            }
            assigned[i] = 1;
        }

        free(assigned);
        *output_values = row_values;
        return 1;
    }

    if (input_row->count != statement->columns.count) {
        free(row_values);
        free(assigned);
        error_set(error, ERR_EXEC, 0, 0, "INSERT column count and value count do not match");
        return 0;
    }

    /* 명시 column list가 있으면 이름을 schema index로 바꿔 해당 자리에 값을 넣는다. */
    for (i = 0; i < statement->columns.count; ++i) {
        int schema_index = schema_find_column(schema, statement->columns.items[i]);
        if (schema_index < 0) {
            free_row_values(row_values, schema->column_count);
            free(assigned);
            error_set(error, ERR_EXEC, 0, 0, "unknown column '%s' in INSERT", statement->columns.items[i]);
            return 0;
        }
        if (assigned[schema_index]) {
            free_row_values(row_values, schema->column_count);
            free(assigned);
            error_set(error, ERR_EXEC, 0, 0, "duplicate column '%s' in INSERT", statement->columns.items[i]);
            return 0;
        }
        if (!validate_type(schema->columns[schema_index].type, &input_row->values[i])) {
            free_row_values(row_values, schema->column_count);
            free(assigned);
            error_set(error, ERR_EXEC, 0, 0, "type mismatch for column '%s'", statement->columns.items[i]);
            return 0;
        }
        if (!duplicate_value(&input_row->values[i], &row_values[schema_index], error)) {
            free_row_values(row_values, schema->column_count);
            free(assigned);
            return 0;
        }
        assigned[schema_index] = 1;
    }

    for (i = 0; i < schema->column_count; ++i) {
        if (!assigned[i]) {
            free_row_values(row_values, schema->column_count);
            free(assigned);
            error_set(error, ERR_EXEC, 0, 0, "missing value for column '%s'", schema->columns[i].name);
            return 0;
        }
    }

    free(assigned);
    *output_values = row_values;
    return 1;
}

/* ASCII table 폭 계산을 위해 값이 화면에 차지하는 길이를 구한다. */
static size_t value_print_width(const SqlValue *value) {
    char buffer[64];

    if (value->type == SQL_VALUE_INT) {
        snprintf(buffer, sizeof(buffer), "%ld", value->int_value);
        return strlen(buffer);
    }
    return strlen(value->text_value);
}

/* border/cell helpers는 SELECT 결과의 표 형식을 만든다. */
static int print_border(FILE *out, const size_t *widths, size_t count) {
    size_t i;

    if (fputc('+', out) == EOF) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        size_t j;
        for (j = 0; j < widths[i] + 2; ++j) {
            if (fputc('-', out) == EOF) {
                return 0;
            }
        }
        if (fputc('+', out) == EOF) {
            return 0;
        }
    }
    return fputc('\n', out) != EOF;
}

static int write_cell(FILE *out, const char *text, size_t width) {
    size_t text_length = strlen(text);
    size_t padding = width - text_length + 1;

    if (fprintf(out, " %s", text) < 0) {
        return 0;
    }
    while (padding-- > 0) {
        if (fputc(' ', out) == EOF) {
            return 0;
        }
    }
    return 1;
}

static int write_value(FILE *out, const SqlValue *value, size_t width) {
    char buffer[64];

    if (value->type == SQL_VALUE_INT) {
        snprintf(buffer, sizeof(buffer), "%ld", value->int_value);
        return write_cell(out, buffer, width);
    }
    return write_cell(out, value->text_value, width);
}

/* INSERT 실행: schema 로드 -> row 검증/재배열 -> CSV append -> 결과 메시지 출력 */
static int execute_insert(const InsertStatement *statement, const char *db_root, FILE *out, Error *error) {
    TableSchema schema;
    size_t inserted = 0;
    size_t i;

    if (!schema_load(db_root, statement->schema, statement->table, &schema, error)) {
        return 0;
    }

    for (i = 0; i < statement->row_count; ++i) {
        SqlValue *row_values = NULL;
        if (!build_insert_row(&schema, statement, &statement->rows[i], &row_values, error)) {
            schema_free(&schema);
            return 0;
        }
        if (!storage_append_row(db_root, &schema, row_values, schema.column_count, error)) {
            free_row_values(row_values, schema.column_count);
            schema_free(&schema);
            return 0;
        }
        free_row_values(row_values, schema.column_count);
        inserted++;
    }

    schema_free(&schema);
    if (fprintf(out, "INSERT OK (%zu rows)\n", inserted) < 0) {
        error_set(error, ERR_IO, 0, 0, "failed to write output");
        return 0;
    }
    return 1;
}

/* SELECT 실행: schema 로드 -> 전체 row 로드 -> projection 계산 -> ASCII table 출력 */
static int execute_select(const SelectStatement *statement, const char *db_root, FILE *out, Error *error) {
    TableSchema schema;
    DataSet data_set;
    int *projection = NULL;
    size_t projection_count = 0;
    size_t *widths = NULL;
    size_t i;

    if (!schema_load(db_root, statement->schema, statement->table, &schema, error)) {
        return 0;
    }
    if (!storage_load_rows(db_root, &schema, &data_set, error)) {
        schema_free(&schema);
        return 0;
    }

    if (statement->select_all) {
        projection_count = schema.column_count;
        projection = (int *)malloc(sizeof(int) * projection_count);
        if (projection == NULL) {
            data_set_free(&data_set);
            schema_free(&schema);
            error_set(error, ERR_MEMORY, 0, 0, "out of memory while preparing SELECT projection");
            return 0;
        }
        /* select_all은 schema column 순서를 projection으로 그대로 사용한다. */
        for (i = 0; i < projection_count; ++i) {
            projection[i] = (int)i;
        }
    } else {
        projection_count = statement->columns.count;
        projection = (int *)malloc(sizeof(int) * projection_count);
        if (projection == NULL) {
            data_set_free(&data_set);
            schema_free(&schema);
            error_set(error, ERR_MEMORY, 0, 0, "out of memory while preparing SELECT projection");
            return 0;
        }
        /* explicit projection은 column 이름을 schema index 배열로 바꿔 재사용한다. */
        for (i = 0; i < projection_count; ++i) {
            projection[i] = schema_find_column(&schema, statement->columns.items[i]);
            if (projection[i] < 0) {
                free(projection);
                data_set_free(&data_set);
                schema_free(&schema);
                error_set(error, ERR_EXEC, 0, 0, "unknown column '%s' in SELECT", statement->columns.items[i]);
                return 0;
            }
        }
    }

    widths = (size_t *)malloc(sizeof(size_t) * projection_count);
    if (widths == NULL) {
        free(projection);
        data_set_free(&data_set);
        schema_free(&schema);
        error_set(error, ERR_MEMORY, 0, 0, "out of memory while formatting result set");
        return 0;
    }

    for (i = 0; i < projection_count; ++i) {
        widths[i] = strlen(schema.columns[projection[i]].name);
    }
    /* 데이터까지 훑어 가장 넓은 셀에 맞춰 표 너비를 확정한다. */
    for (i = 0; i < data_set.count; ++i) {
        size_t j;
        for (j = 0; j < projection_count; ++j) {
            size_t width = value_print_width(&data_set.rows[i].values[projection[j]]);
            if (width > widths[j]) {
                widths[j] = width;
            }
        }
    }

    if (!print_border(out, widths, projection_count)) {
        free(widths);
        free(projection);
        data_set_free(&data_set);
        schema_free(&schema);
        error_set(error, ERR_IO, 0, 0, "failed to write output");
        return 0;
    }
    if (fputc('|', out) == EOF) {
        free(widths);
        free(projection);
        data_set_free(&data_set);
        schema_free(&schema);
        error_set(error, ERR_IO, 0, 0, "failed to write output");
        return 0;
    }
    for (i = 0; i < projection_count; ++i) {
        if (!write_cell(out, schema.columns[projection[i]].name, widths[i]) || fputc('|', out) == EOF) {
            free(widths);
            free(projection);
            data_set_free(&data_set);
            schema_free(&schema);
            error_set(error, ERR_IO, 0, 0, "failed to write output");
            return 0;
        }
    }
    if (fputc('\n', out) == EOF || !print_border(out, widths, projection_count)) {
        free(widths);
        free(projection);
        data_set_free(&data_set);
        schema_free(&schema);
        error_set(error, ERR_IO, 0, 0, "failed to write output");
        return 0;
    }

    for (i = 0; i < data_set.count; ++i) {
        size_t j;

        if (fputc('|', out) == EOF) {
            free(widths);
            free(projection);
            data_set_free(&data_set);
            schema_free(&schema);
            error_set(error, ERR_IO, 0, 0, "failed to write output");
            return 0;
        }
        for (j = 0; j < projection_count; ++j) {
            if (!write_value(out, &data_set.rows[i].values[projection[j]], widths[j]) || fputc('|', out) == EOF) {
                free(widths);
                free(projection);
                data_set_free(&data_set);
                schema_free(&schema);
                error_set(error, ERR_IO, 0, 0, "failed to write output");
                return 0;
            }
        }
        if (fputc('\n', out) == EOF) {
            free(widths);
            free(projection);
            data_set_free(&data_set);
            schema_free(&schema);
            error_set(error, ERR_IO, 0, 0, "failed to write output");
            return 0;
        }
    }

    if (!print_border(out, widths, projection_count) || fprintf(out, "(%zu rows)\n", data_set.count) < 0) {
        free(widths);
        free(projection);
        data_set_free(&data_set);
        schema_free(&schema);
        error_set(error, ERR_IO, 0, 0, "failed to write output");
        return 0;
    }

    free(widths);
    free(projection);
    data_set_free(&data_set);
    schema_free(&schema);
    return 1;
}

/* statement enum을 실제 executor 구현으로 연결하는 dispatch 함수다. */
int execute_statement(const Statement *statement, const char *db_root, FILE *out, Error *error) {
    if (statement->type == STMT_INSERT) {
        return execute_insert(&statement->as.insert_stmt, db_root, out, error);
    }
    if (statement->type == STMT_SELECT) {
        return execute_select(&statement->as.select_stmt, db_root, out, error);
    }

    error_set(error, ERR_EXEC, 0, 0, "unsupported statement type");
    return 0;
}
