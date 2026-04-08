#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "error.h"

/* base_line/base_column은 원본 SQL file 기준 에러 위치를 복원하기 위한 오프셋이다. */
int parse_statement(const char *sql, int base_line, int base_column, Statement *statement, Error *error);

#endif
