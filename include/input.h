#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>
#include <stdio.h>

#include "error.h"

/* split_statements는 원본 SQL file을 statement 단위 chunk로 나누고 시작 좌표를 함께 저장한다. */
typedef struct StatementChunk {
    char *text;
    int line;
    int column;
} StatementChunk;

typedef struct StatementChunkList {
    StatementChunk *items;
    size_t count;
} StatementChunkList;

/* input layer는 파일 읽기, statement splitting, statement header formatting을 담당한다. */
int read_text_file(const char *path, char **contents, Error *error);
int split_statements(const char *sql, StatementChunkList *chunks, Error *error);
void fprint_statement_header(FILE *stream, size_t statement_index, const char *sql);
void statement_chunk_list_free(StatementChunkList *chunks);

#endif
