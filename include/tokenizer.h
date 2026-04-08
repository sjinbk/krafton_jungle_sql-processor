#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>

#include "error.h"

/* tokenizer는 parser에 필요한 최소 token set만 제공한다. */
typedef enum TokenType {
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_STAR,
    TOKEN_EOF
} TokenType;

typedef struct Token {
    TokenType type;
    char *lexeme;
    int line;
    int column;
} Token;

typedef struct TokenList {
    Token *items;
    size_t count;
} TokenList;

/* token_is_keyword는 identifier lexeme를 대소문자 무시 keyword와 비교한다. */
int tokenize_sql(const char *sql, int base_line, int base_column, TokenList *tokens, Error *error);
void token_list_free(TokenList *tokens);
int token_is_keyword(const Token *token, const char *keyword);

#endif
