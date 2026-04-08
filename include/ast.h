#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stddef.h>

/* AST layer:
 * parser가 이 구조를 만들고, executor/storage가 이를 소비한다.
 * MVP가 INSERT와 SELECT만 지원하므로 구조도 그 범위에 맞춰 작게 유지된다.
 */
typedef enum SqlValueType {
    SQL_VALUE_INT,
    SQL_VALUE_TEXT
} SqlValueType;

/* parsing, schema 검사, CSV storage가 함께 쓰는 공통 runtime value 타입이다. */
typedef struct SqlValue {
    SqlValueType type;
    long int_value;
    char *text_value;
} SqlValue;

/* projection이나 optional INSERT column list에서 재사용하는 "column 이름 목록" 구조다. */
typedef struct ColumnList {
    char **items;
    size_t count;
} ColumnList;

/* INSERT statement 안의 VALUES row 하나를 표현한다. */
typedef struct ValueRow {
    SqlValue *values;
    size_t count;
} ValueRow;

/* INSERT는 대상 테이블, optional column list, 하나 이상의 row payload를 함께 가진다. */
typedef struct InsertStatement {
    char *schema;
    char *table;
    ColumnList columns;
    ValueRow *rows;
    size_t row_count;
} InsertStatement;

/* SELECT는 select_all 또는 explicit projection list 중 하나를 사용한다. */
typedef struct SelectStatement {
    char *schema;
    char *table;
    int select_all;
    ColumnList columns;
} SelectStatement;

typedef enum StatementType {
    STMT_INSERT,
    STMT_SELECT
} StatementType;

typedef struct Statement {
    StatementType type;
    union {
        InsertStatement insert_stmt;
        SelectStatement select_stmt;
    } as;
} Statement;

/* free helper는 parse 실패 후나 실행 종료 후 AST가 소유한 heap 메모리를 정리한다. */
void sql_value_free(SqlValue *value);
void column_list_free(ColumnList *list);
void value_row_free(ValueRow *row);
void statement_free(Statement *statement);

/* --emit-ast에서 파싱 결과를 짧은 한 줄 요약으로 출력할 때 사용한다. */
void statement_fprint(FILE *out, const Statement *statement);

#endif
