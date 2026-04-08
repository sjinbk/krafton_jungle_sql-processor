#include "schema.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* schema layer는 .schema file을 executor/storage가 쓰는 메타데이터로 변환한다. */
static char *duplicate_string(const char *text) {
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

/* fgets 결과 줄 끝 개행을 제거해 parsing을 단순화한다. */
static void trim_line(char *line) {
    size_t length = strlen(line);

    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[--length] = '\0';
    }
}

/* 현재 MVP type system은 INT/TEXT 두 타입만 허용한다. */
static int parse_type(const char *type_name, SqlValueType *type) {
    if (strcmp(type_name, "INT") == 0) {
        *type = SQL_VALUE_INT;
        return 1;
    }
    if (strcmp(type_name, "TEXT") == 0) {
        *type = SQL_VALUE_TEXT;
        return 1;
    }
    return 0;
}

/* schema_load는 file-backed DB의 구조적 source of truth인 .schema를 읽는다. */
int schema_load(const char *db_root, const char *schema_name, const char *table_name, TableSchema *schema, Error *error) {
    char path[512];
    FILE *file;
    char line[256];

    memset(schema, 0, sizeof(*schema));
    snprintf(path, sizeof(path), "%s/%s/%s.schema", db_root, schema_name, table_name);

    file = fopen(path, "rb");
    if (file == NULL) {
        error_set(error, ERR_SCHEMA, 0, 0, "failed to open schema file '%s'", path);
        return 0;
    }

    schema->schema_name = duplicate_string(schema_name);
    schema->table_name = duplicate_string(table_name);
    if (schema->schema_name == NULL || schema->table_name == NULL) {
        fclose(file);
        schema_free(schema);
        error_set(error, ERR_MEMORY, 0, 0, "out of memory while loading schema");
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *comma;
        ColumnDef *new_columns;
        char *name;
        char *type_name;

        /* 빈 줄은 무시하고, 나머지는 정확히 "name,TYPE" 형식을 따라야 한다. */
        trim_line(line);
        if (line[0] == '\0') {
            continue;
        }

        comma = strchr(line, ',');
        if (comma == NULL) {
            fclose(file);
            schema_free(schema);
            error_set(error, ERR_SCHEMA, 0, 0, "invalid schema line '%s'", line);
            return 0;
        }
        *comma = '\0';
        name = line;
        type_name = comma + 1;

        while (*type_name != '\0' && isspace((unsigned char)*type_name)) {
            type_name++;
        }

        new_columns = (ColumnDef *)realloc(schema->columns, sizeof(ColumnDef) * (schema->column_count + 1));
        if (new_columns == NULL) {
            fclose(file);
            schema_free(schema);
            error_set(error, ERR_MEMORY, 0, 0, "out of memory while loading schema");
            return 0;
        }
        schema->columns = new_columns;
        schema->columns[schema->column_count].name = duplicate_string(name);
        if (schema->columns[schema->column_count].name == NULL) {
            fclose(file);
            schema_free(schema);
            error_set(error, ERR_MEMORY, 0, 0, "out of memory while loading schema");
            return 0;
        }
        if (!parse_type(type_name, &schema->columns[schema->column_count].type)) {
            fclose(file);
            schema_free(schema);
            error_set(error, ERR_SCHEMA, 0, 0, "unsupported schema type '%s'", type_name);
            return 0;
        }
        schema->column_count++;
    }

    fclose(file);
    if (schema->column_count == 0) {
        schema_free(schema);
        error_set(error, ERR_SCHEMA, 0, 0, "schema '%s.%s' has no columns", schema_name, table_name);
        return 0;
    }

    return 1;
}

/* executor가 projection/INSERT 매핑을 할 때 이름으로 column index를 찾는다. */
int schema_find_column(const TableSchema *schema, const char *name) {
    size_t i;

    for (i = 0; i < schema->column_count; ++i) {
        if (strcmp(schema->columns[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* schema_load가 할당한 모든 문자열/배열을 정리한다. */
void schema_free(TableSchema *schema) {
    size_t i;

    if (schema == NULL) {
        return;
    }

    free(schema->schema_name);
    free(schema->table_name);
    for (i = 0; i < schema->column_count; ++i) {
        free(schema->columns[i].name);
    }
    free(schema->columns);
    schema->schema_name = NULL;
    schema->table_name = NULL;
    schema->columns = NULL;
    schema->column_count = 0;
}
