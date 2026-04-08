#include "parser.h"

#include <stdlib.h>
#include <string.h>

#include "tokenizer.h"

/* Parser는 token 스트림과 현재 읽기 위치만 가지는 작은 상태 객체다. */
typedef struct Parser {
    TokenList tokens;
    size_t index;
} Parser;

/* 대부분의 helper가 같은 커서를 기준으로 동작하므로 현재 token 조회를 한곳에 모은다. */
static Token *current_token(Parser *parser) {
    return &parser->tokens.items[parser->index];
}

/* match helper는 성공하면 입력을 소비하고, 실패 메시지는 호출자가 고르게 한다. */
static int match_type(Parser *parser, TokenType type) {
    if (current_token(parser)->type != type) {
        return 0;
    }
    parser->index++;
    return 1;
}

static int match_keyword(Parser *parser, const char *keyword) {
    if (!token_is_keyword(current_token(parser), keyword)) {
        return 0;
    }
    parser->index++;
    return 1;
}

/* parser 오류는 문법이 깨진 지점의 token 위치를 기준으로 보고한다. */
static int parser_error(Parser *parser, Error *error, const char *message) {
    Token *token = current_token(parser);
    error_set(error, ERR_PARSE, token->line, token->column, "%s", message);
    return 0;
}

/* token 배열을 해제한 뒤에도 AST 문자열은 살아 있어야 하므로 별도 메모리로 복사한다. */
static char *duplicate_string(const char *text) {
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

/* 이 MVP에서는 schema 이름, table 이름, column 이름이 모두 plain identifier다. */
static int parse_identifier(Parser *parser, char **out, Error *error) {
    Token *token = current_token(parser);

    if (token->type != TOKEN_IDENTIFIER) {
        return parser_error(parser, error, "expected identifier");
    }

    *out = duplicate_string(token->lexeme);
    if (*out == NULL) {
        error_set(error, ERR_MEMORY, token->line, token->column, "out of memory while parsing identifier");
        return 0;
    }
    parser->index++;
    return 1;
}

/* [schema.]table 형식을 지원하되, 가장 흔한 경우를 위해 기본 schema는 public으로 둔다. */
static int parse_qualified_name(Parser *parser, char **schema, char **table, Error *error) {
    char *first = NULL;
    char *second = NULL;

    if (!parse_identifier(parser, &first, error)) {
        return 0;
    }

    if (match_type(parser, TOKEN_DOT)) {
        if (!parse_identifier(parser, &second, error)) {
            free(first);
            return 0;
        }
        *schema = first;
        *table = second;
    } else {
        *schema = duplicate_string("public");
        *table = first;
        if (*schema == NULL) {
            free(first);
            error_set(error, ERR_MEMORY, current_token(parser)->line, current_token(parser)->column, "out of memory while parsing table name");
            return 0;
        }
    }

    return 1;
}

/* column 이름 목록을 하나씩 확장해 쌓는다. */
static int column_list_push(ColumnList *list, char *name, Error *error, Token *token) {
    char **new_items = (char **)realloc(list->items, sizeof(char *) * (list->count + 1));

    if (new_items == NULL) {
        free(name);
        error_set(error, ERR_MEMORY, token->line, token->column, "out of memory while parsing column list");
        return 0;
    }

    list->items = new_items;
    list->items[list->count++] = name;
    return 1;
}

/* SELECT projection과 INSERT column list는 같은 쉼표 구분 identifier 문법을 사용한다. */
static int parse_column_names(Parser *parser, ColumnList *columns, Error *error) {
    char *name;
    Token *token;

    columns->items = NULL;
    columns->count = 0;

    token = current_token(parser);
    if (!parse_identifier(parser, &name, error)) {
        return 0;
    }
    if (!column_list_push(columns, name, error, token)) {
        return 0;
    }

    while (match_type(parser, TOKEN_COMMA)) {
        token = current_token(parser);
        if (!parse_identifier(parser, &name, error)) {
            return 0;
        }
        if (!column_list_push(columns, name, error, token)) {
            return 0;
        }
    }

    return 1;
}

/* literal 지원 범위는 의도적으로 좁다. 이 MVP에서는 INT와 TEXT만 허용한다. */
static int parse_value(Parser *parser, SqlValue *value, Error *error) {
    Token *token = current_token(parser);

    if (token->type == TOKEN_NUMBER) {
        value->type = SQL_VALUE_INT;
        value->int_value = strtol(token->lexeme, NULL, 10);
        value->text_value = NULL;
        parser->index++;
        return 1;
    }

    if (token->type == TOKEN_STRING) {
        value->type = SQL_VALUE_TEXT;
        value->int_value = 0;
        value->text_value = duplicate_string(token->lexeme);
        if (value->text_value == NULL) {
            error_set(error, ERR_MEMORY, token->line, token->column, "out of memory while parsing string literal");
            return 0;
        }
        parser->index++;
        return 1;
    }

    return parser_error(parser, error, "expected literal value");
}

/* (1, 'alice', 31) 같은 괄호 묶음 VALUES row 하나를 파싱한다. */
static int parse_value_row(Parser *parser, ValueRow *row, Error *error) {
    SqlValue value;
    SqlValue *new_values;

    row->values = NULL;
    row->count = 0;

    if (!match_type(parser, TOKEN_LPAREN)) {
        return parser_error(parser, error, "expected '(' before value list");
    }

    if (!parse_value(parser, &value, error)) {
        return 0;
    }

    new_values = (SqlValue *)realloc(row->values, sizeof(SqlValue) * (row->count + 1));
    if (new_values == NULL) {
        sql_value_free(&value);
        error_set(error, ERR_MEMORY, current_token(parser)->line, current_token(parser)->column, "out of memory while parsing values");
        return 0;
    }
    row->values = new_values;
    row->values[row->count++] = value;

    while (match_type(parser, TOKEN_COMMA)) {
        if (!parse_value(parser, &value, error)) {
            value_row_free(row);
            return 0;
        }
        new_values = (SqlValue *)realloc(row->values, sizeof(SqlValue) * (row->count + 1));
        if (new_values == NULL) {
            sql_value_free(&value);
            value_row_free(row);
            error_set(error, ERR_MEMORY, current_token(parser)->line, current_token(parser)->column, "out of memory while parsing values");
            return 0;
        }
        row->values = new_values;
        row->values[row->count++] = value;
    }

    if (!match_type(parser, TOKEN_RPAREN)) {
        value_row_free(row);
        return parser_error(parser, error, "expected ')' after value list");
    }

    return 1;
}

/* INSERT 파싱은 실제 SQL 형태를 그대로 따른다.
 * 대상 테이블, optional column list, 그리고 하나 이상의 VALUES row 순서다.
 */
static int parse_insert(Parser *parser, Statement *statement, Error *error) {
    InsertStatement *insert_stmt = &statement->as.insert_stmt;

    statement->type = STMT_INSERT;
    memset(insert_stmt, 0, sizeof(*insert_stmt));

    if (!match_keyword(parser, "INSERT")) {
        return parser_error(parser, error, "expected INSERT");
    }
    if (!match_keyword(parser, "INTO")) {
        return parser_error(parser, error, "expected INTO after INSERT");
    }
    if (!parse_qualified_name(parser, &insert_stmt->schema, &insert_stmt->table, error)) {
        return 0;
    }

    if (current_token(parser)->type == TOKEN_LPAREN) {
        match_type(parser, TOKEN_LPAREN);
        if (!parse_column_names(parser, &insert_stmt->columns, error)) {
            return 0;
        }
        if (!match_type(parser, TOKEN_RPAREN)) {
            return parser_error(parser, error, "expected ')' after column list");
        }
    }

    if (!match_keyword(parser, "VALUES")) {
        return parser_error(parser, error, "expected VALUES");
    }

    /* 뒤에 comma가 더 없을 때까지 row를 계속 모은다. */
    do {
        ValueRow row;
        ValueRow *new_rows;

        if (!parse_value_row(parser, &row, error)) {
            return 0;
        }

        new_rows = (ValueRow *)realloc(insert_stmt->rows, sizeof(ValueRow) * (insert_stmt->row_count + 1));
        if (new_rows == NULL) {
            value_row_free(&row);
            error_set(error, ERR_MEMORY, current_token(parser)->line, current_token(parser)->column, "out of memory while storing value rows");
            return 0;
        }

        insert_stmt->rows = new_rows;
        insert_stmt->rows[insert_stmt->row_count++] = row;
    } while (match_type(parser, TOKEN_COMMA));

    return 1;
}

/* SELECT는 FROM 앞에서 "*" 또는 explicit projection list 중 하나를 반드시 선택해야 한다. */
static int parse_select(Parser *parser, Statement *statement, Error *error) {
    SelectStatement *select_stmt = &statement->as.select_stmt;

    statement->type = STMT_SELECT;
    memset(select_stmt, 0, sizeof(*select_stmt));

    if (!match_keyword(parser, "SELECT")) {
        return parser_error(parser, error, "expected SELECT");
    }

    if (match_type(parser, TOKEN_STAR)) {
        select_stmt->select_all = 1;
    } else {
        /* FROM이 바로 나오면 projection list 자체를 건너뛴 것이다. */
        if (current_token(parser)->type != TOKEN_IDENTIFIER || token_is_keyword(current_token(parser), "FROM")) {
            return parser_error(parser, error, "expected projection list or '*'");
        }
        if (!parse_column_names(parser, &select_stmt->columns, error)) {
            return 0;
        }
    }

    if (!match_keyword(parser, "FROM")) {
        return parser_error(parser, error, "expected FROM");
    }
    if (!parse_qualified_name(parser, &select_stmt->schema, &select_stmt->table, error)) {
        return 0;
    }

    return 1;
}

/* statement 하나를 파싱할 때는 먼저 tokenize하고, 첫 keyword로 분기한 뒤, 남은 token이 없는지 확인한다. */
int parse_statement(const char *sql, int base_line, int base_column, Statement *statement, Error *error) {
    Parser parser;

    memset(statement, 0, sizeof(*statement));
    parser.index = 0;

    if (!tokenize_sql(sql, base_line, base_column, &parser.tokens, error)) {
        return 0;
    }

    if (token_is_keyword(current_token(&parser), "INSERT")) {
        if (!parse_insert(&parser, statement, error)) {
            token_list_free(&parser.tokens);
            statement_free(statement);
            return 0;
        }
    } else if (token_is_keyword(current_token(&parser), "SELECT")) {
        if (!parse_select(&parser, statement, error)) {
            token_list_free(&parser.tokens);
            statement_free(statement);
            return 0;
        }
    } else {
        /* 지원하지 않는 statement는 여기서 바로 거절해 지원 범위를 명시적으로 유지한다. */
        parser_error(&parser, error, "only INSERT and SELECT are supported");
        token_list_free(&parser.tokens);
        return 0;
    }

    /* token이 남아 있으면 문법이 statement 전체가 아니라 앞부분만 해석한 것이다. */
    if (current_token(&parser)->type != TOKEN_EOF) {
        parser_error(&parser, error, "unexpected trailing tokens");
        token_list_free(&parser.tokens);
        statement_free(statement);
        return 0;
    }

    token_list_free(&parser.tokens);
    return 1;
}
