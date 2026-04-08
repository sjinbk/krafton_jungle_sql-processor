#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <stdio.h>

#include "ast.h"
#include "error.h"

/* executor는 AST를 schema/storage와 결합해 실제 SQL 의미를 수행한다. */
int execute_statement(const Statement *statement, const char *db_root, FILE *out, Error *error);

#endif
