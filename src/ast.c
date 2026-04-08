#include "ast.h"

#include <stdio.h>
#include <stdlib.h>

/* AST helper들은 parser가 만든 구조를 안전하게 정리하고 요약 출력한다. */
void sql_value_free(SqlValue *value) {
    if (value == NULL) {
        return;
    }
    if (value->type == SQL_VALUE_TEXT) {
        free(value->text_value);
        value->text_value = NULL;
    }
}

/* ColumnList는 컬럼 이름 문자열 배열의 공통 해제 루틴이다. */
void column_list_free(ColumnList *list) {
    size_t i;

    if (list == NULL) {
        return;
    }

    for (i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

/* ValueRow는 row 내부 각 value와 row 배열 자체를 함께 해제한다. */
void value_row_free(ValueRow *row) {
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

/* statement 종류별 소유 메모리 구조가 달라 내부 free helper를 나눴다. */
static void insert_statement_free(InsertStatement *statement) {
    size_t i;

    free(statement->schema);
    free(statement->table);
    column_list_free(&statement->columns);
    for (i = 0; i < statement->row_count; ++i) {
        value_row_free(&statement->rows[i]);
    }
    free(statement->rows);
    statement->schema = NULL;
    statement->table = NULL;
    statement->rows = NULL;
    statement->row_count = 0;
}

static void select_statement_free(SelectStatement *statement) {
    free(statement->schema);
    free(statement->table);
    column_list_free(&statement->columns);
    statement->schema = NULL;
    statement->table = NULL;
}

/* parse 실패/실행 종료 공통 경로에서 쓰는 top-level cleanup 함수다. */
void statement_free(Statement *statement) {
    if (statement == NULL) {
        return;
    }

    if (statement->type == STMT_INSERT) {
        insert_statement_free(&statement->as.insert_stmt);
    } else if (statement->type == STMT_SELECT) {
        select_statement_free(&statement->as.select_stmt);
    }
}

/* AST debug 출력에서 column list는 사람이 읽기 쉬운 compact 형식으로 보여 준다. */
static void fprint_column_list(FILE *out, const ColumnList *list) {
    size_t i;

    fputc('[', out);
    for (i = 0; i < list->count; ++i) {
        if (i > 0) {
            fputs(", ", out);
        }
        fputs(list->items[i], out);
    }
    fputc(']', out);
}

/* --emit-ast는 SQL 원문 대신 파싱 결과 구조를 짧은 한 줄로 설명한다. */
void statement_fprint(FILE *out, const Statement *statement) {
    if (statement->type == STMT_INSERT) {
        const InsertStatement *insert_stmt = &statement->as.insert_stmt;
        fputs("AST INSERT ", out);
        fprintf(out, "%s.%s ", insert_stmt->schema, insert_stmt->table);
        if (insert_stmt->columns.count == 0) {
            fputs("columns=ALL ", out);
        } else {
            fputs("columns=", out);
            fprint_column_list(out, &insert_stmt->columns);
            fputc(' ', out);
        }
        fprintf(out, "rows=%zu\n", insert_stmt->row_count);
        return;
    }

    if (statement->type == STMT_SELECT) {
        const SelectStatement *select_stmt = &statement->as.select_stmt;
        fputs("AST SELECT ", out);
        fprintf(out, "%s.%s ", select_stmt->schema, select_stmt->table);
        if (select_stmt->select_all) {
            fputs("columns=*\n", out);
        } else {
            fputs("columns=", out);
            fprint_column_list(out, &select_stmt->columns);
            fputc('\n', out);
        }
    }
}
