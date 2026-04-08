#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>

#include "ast.h"
#include "error.h"
#include "schema.h"

/* DataRow/DataSet은 CSV에서 읽은 table contents를 메모리에 담는 구조다. */
typedef struct DataRow {
    SqlValue *values;
    size_t count;
} DataRow;

typedef struct DataSet {
    DataRow *rows;
    size_t count;
} DataSet;

/* storage layer는 schema에 맞는 CSV append/load만 담당한다. */
int storage_append_row(const char *db_root, const TableSchema *schema, const SqlValue *values, size_t value_count, Error *error);
int storage_load_rows(const char *db_root, const TableSchema *schema, DataSet *data_set, Error *error);
void data_row_free(DataRow *row);
void data_set_free(DataSet *data_set);

#endif
