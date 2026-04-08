#ifndef SCHEMA_H
#define SCHEMA_H

#include <stddef.h>

#include "ast.h"
#include "error.h"

/* .schema file의 한 줄은 ColumnDef 하나가 된다. */
typedef struct ColumnDef {
    char *name;
    SqlValueType type;
} ColumnDef;

/* TableSchema는 executor/storage가 공유하는 canonical table metadata다. */
typedef struct TableSchema {
    char *schema_name;
    char *table_name;
    ColumnDef *columns;
    size_t column_count;
} TableSchema;

/* schema_load는 <db_root>/<schema>/<table>.schema를 읽어 메모리 구조로 만든다. */
int schema_load(const char *db_root, const char *schema_name, const char *table_name, TableSchema *schema, Error *error);
int schema_find_column(const TableSchema *schema, const char *name);
void schema_free(TableSchema *schema);

#endif
