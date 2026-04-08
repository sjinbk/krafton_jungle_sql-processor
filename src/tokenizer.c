#include "tokenizer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* tokenizer는 parser가 다루기 쉬운 평탄한 token stream만 만든다. */
static char *duplicate_range(const char *start, size_t length) {
    char *copy = (char *)malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

/* push_token은 token 배열 확장과 lexeme 복사를 함께 처리한다. */
static int push_token(TokenList *tokens, TokenType type, const char *start, size_t length, int line, int column, Error *error) {
    Token *new_items;
    char *lexeme;

    new_items = (Token *)realloc(tokens->items, sizeof(Token) * (tokens->count + 1));
    if (new_items == NULL) {
        error_set(error, ERR_MEMORY, line, column, "out of memory while tokenizing");
        return 0;
    }

    lexeme = duplicate_range(start, length);
    if (lexeme == NULL) {
        error_set(error, ERR_MEMORY, line, column, "out of memory while tokenizing");
        return 0;
    }

    tokens->items = new_items;
    tokens->items[tokens->count].type = type;
    tokens->items[tokens->count].lexeme = lexeme;
    tokens->items[tokens->count].line = line;
    tokens->items[tokens->count].column = column;
    tokens->count++;
    return 1;
}

/* string literal은 '' escape를 실제 quote 문자 하나로 복원해 TOKEN_STRING으로 저장한다. */
static int read_string(const char **cursor_ptr, int *column_ptr, int line, TokenList *tokens, Error *error) {
    const char *cursor = *cursor_ptr + 1;
    const char *start = cursor;
    size_t out_length = 0;
    char *buffer;
    int column = *column_ptr;

    while (*cursor != '\0') {
        if (*cursor == '\'') {
            if (cursor[1] == '\'') {
                out_length += 1;
                cursor += 2;
                continue;
            }
            break;
        }
        if (*cursor == '\n') {
            error_set(error, ERR_LEX, line, column, "newline is not allowed inside string literal");
            return 0;
        }
        out_length += 1;
        cursor++;
    }

    if (*cursor != '\'') {
        error_set(error, ERR_LEX, line, column, "unterminated string literal");
        return 0;
    }

    buffer = (char *)malloc(out_length + 1);
    if (buffer == NULL) {
        error_set(error, ERR_MEMORY, line, column, "out of memory while reading string literal");
        return 0;
    }

    {
        const char *reader = start;
        size_t writer = 0;
        while (reader < cursor) {
            if (*reader == '\'' && reader[1] == '\'') {
                buffer[writer++] = '\'';
                reader += 2;
            } else {
                buffer[writer++] = *reader++;
            }
        }
        buffer[writer] = '\0';
    }

    if (!push_token(tokens, TOKEN_STRING, buffer, strlen(buffer), line, column, error)) {
        free(buffer);
        return 0;
    }

    free(buffer);
    *column_ptr += (int)(cursor - *cursor_ptr) + 1;
    *cursor_ptr = cursor + 1;
    return 1;
}

/* tokenizer는 whitespace를 건너뛰며 MVP grammar에 필요한 토큰만 인식한다. */
int tokenize_sql(const char *sql, int base_line, int base_column, TokenList *tokens, Error *error) {
    const char *cursor = sql;
    int line = base_line;
    int column = base_column;

    tokens->items = NULL;
    tokens->count = 0;

    while (*cursor != '\0') {
        /* 줄바꿈 좌표는 parse error 위치 정확도를 위해 유지한다. */
        if (isspace((unsigned char)*cursor)) {
            if (*cursor == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
            cursor++;
            continue;
        }

        /* identifier와 keyword는 같은 토큰으로 만들고 parser에서 keyword 판별을 한다. */
        if (isalpha((unsigned char)*cursor) || *cursor == '_') {
            const char *start = cursor;
            int start_column = column;

            while (isalnum((unsigned char)*cursor) || *cursor == '_') {
                cursor++;
                column++;
            }
            if (!push_token(tokens, TOKEN_IDENTIFIER, start, (size_t)(cursor - start), line, start_column, error)) {
                token_list_free(tokens);
                return 0;
            }
            continue;
        }

        /* 숫자는 정수 literal만 지원하므로 단순한 연속 숫자 규칙이면 충분하다. */
        if (isdigit((unsigned char)*cursor) || (*cursor == '-' && isdigit((unsigned char)cursor[1]))) {
            const char *start = cursor;
            int start_column = column;

            cursor++;
            column++;
            while (isdigit((unsigned char)*cursor)) {
                cursor++;
                column++;
            }
            if (!push_token(tokens, TOKEN_NUMBER, start, (size_t)(cursor - start), line, start_column, error)) {
                token_list_free(tokens);
                return 0;
            }
            continue;
        }

        if (*cursor == '\'') {
            if (!read_string(&cursor, &column, line, tokens, error)) {
                token_list_free(tokens);
                return 0;
            }
            continue;
        }

        {
            TokenType type;
            int recognized = 1;
            int token_column = column;

            /* grammar에 실제로 쓰는 punctuation만 토큰화한다. */
            switch (*cursor) {
                case ',':
                    type = TOKEN_COMMA;
                    break;
                case '.':
                    type = TOKEN_DOT;
                    break;
                case '(':
                    type = TOKEN_LPAREN;
                    break;
                case ')':
                    type = TOKEN_RPAREN;
                    break;
                case '*':
                    type = TOKEN_STAR;
                    break;
                default:
                    recognized = 0;
                    break;
            }

            if (!recognized) {
                token_list_free(tokens);
                error_set(error, ERR_LEX, line, column, "unexpected character '%c'", *cursor);
                return 0;
            }

            if (!push_token(tokens, type, cursor, 1, line, token_column, error)) {
                token_list_free(tokens);
                return 0;
            }

            cursor++;
            column++;
        }
    }

    if (!push_token(tokens, TOKEN_EOF, "", 0, line, column, error)) {
        token_list_free(tokens);
        return 0;
    }

    return 1;
}

/* token 배열과 각 lexeme heap 메모리를 함께 정리한다. */
void token_list_free(TokenList *tokens) {
    size_t i;

    if (tokens == NULL) {
        return;
    }

    for (i = 0; i < tokens->count; ++i) {
        free(tokens->items[i].lexeme);
    }
    free(tokens->items);
    tokens->items = NULL;
    tokens->count = 0;
}

/* SQL keyword 비교는 대소문자를 구분하지 않는다. */
int token_is_keyword(const Token *token, const char *keyword) {
    size_t i;

    if (token->type != TOKEN_IDENTIFIER) {
        return 0;
    }
    if (strlen(token->lexeme) != strlen(keyword)) {
        return 0;
    }

    for (i = 0; keyword[i] != '\0'; ++i) {
        char left = token->lexeme[i];
        char right = keyword[i];

        if (left >= 'a' && left <= 'z') {
            left = (char)(left - 'a' + 'A');
        }
        if (right >= 'a' && right <= 'z') {
            right = (char)(right - 'a' + 'A');
        }
        if (left != right) {
            return 0;
        }
    }

    return 1;
}
