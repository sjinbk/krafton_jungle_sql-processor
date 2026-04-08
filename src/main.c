#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "error.h"
#include "executor.h"
#include "input.h"
#include "parser.h"

/* main은 전체 실행 흐름을 조립하는 최상위 조정자다.
 * CLI 인자를 읽고, SQL 파일을 불러오고, statement로 나누고, AST를 만든 뒤 실행과 오류 보고까지 연결한다.
 */
static void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [--emit-ast] --db <db_root> --file <sql_file>\n", program_name);
}

int main(int argc, char **argv) { // argc : 몇개의 문자열 인자가 들어왔는가, argv[0]실행한 프로그램 이름 혹은 경로 / [1] 사용자가 실제로 넘긴 옵션
    const char *db_root = NULL;
    const char *sql_file = NULL;
    int emit_ast = 0;
    char *contents = NULL;
    StatementChunkList chunks;
    Error error;
    size_t i;

    error_clear(&error);
    memset(&chunks, 0, sizeof(chunks));

    /* 지원하는 옵션 수가 적기 때문에 수동 파싱이 오히려 허용 인자 형태를 더 분명하게 보여 준다. */
    for (i = 1; i < (size_t)argc; ) {
        if (strcmp(argv[i], "--emit-ast") == 0) {
            emit_ast = 1;
            i += 1;
        } else if (i + 1 < (size_t)argc && strcmp(argv[i], "--db") == 0) {
            db_root = argv[i + 1];
            i += 2;
        } else if (i + 1 < (size_t)argc && strcmp(argv[i], "--file") == 0) {
            sql_file = argv[i + 1];
            i += 2;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (db_root == NULL || sql_file == NULL) {
        print_usage(argv[0]);
        return 1;
    }

    /* statement 분리는 줄 단위가 아니라 원문 문자 자체를 봐야 하므로 파일 전체를 먼저 읽는다. */
    if (!read_text_file(sql_file, &contents, &error)) {
        fprintf(stderr, "error: %s\n", error.message);
        return 1;
    }

    /* splitter는 각 statement의 시작 line/column도 기록해 이후 parser가 정확한 소스 위치를 재사용하게 한다. */
    if (!split_statements(contents, &chunks, &error)) {
        fprintf(stderr, "%d:%d: %s\n", error.line, error.column, error.message);
        free(contents);
        return 1;
    }

    for (i = 0; i < chunks.count; ++i) {
        Statement statement;

        memset(&statement, 0, sizeof(statement));
        error_clear(&error);

        /* 여러 statement가 있을 때는 결과 앞에 짧은 header를 붙여 어떤 SQL의 결과인지 바로 보이게 한다. */
        if (chunks.count > 1) {
            fprint_statement_header(stdout, i + 1, chunks.items[i].text);
        }

        /* parser는 statement 텍스트와 원본 파일 기준 시작 위치를 함께 받아 오류 좌표를 맞춘다. */
        if (!parse_statement(chunks.items[i].text, chunks.items[i].line, chunks.items[i].column, &statement, &error)) {
            fprintf(stderr, "%d:%d: %s\n", error.line, error.column, error.message);
            statement_free(&statement);
            statement_chunk_list_free(&chunks);
            free(contents);
            return 1;
        }

        /* AST 디버그 출력은 stdout 계약을 깨지 않도록 stderr로만 보낸다. */
        if (emit_ast) {
            statement_fprint(stderr, &statement);
        }

        /* 첫 실패 statement에서 즉시 멈춰 실행 순서와 오류 처리 규칙을 단순하게 유지한다. */
        if (!execute_statement(&statement, db_root, stdout, &error)) {
            if (error.line > 0) {
                fprintf(stderr, "%d:%d: %s\n", error.line, error.column, error.message);
            } else {
                fprintf(stderr, "error: %s\n", error.message);
            }
            statement_free(&statement);
            statement_chunk_list_free(&chunks);
            free(contents);
            return 1;
        }

        statement_free(&statement);
    }

    /* statement별 할당과 원본 SQL 버퍼를 모두 정리한다. */
    statement_chunk_list_free(&chunks);
    free(contents);
    return 0;
}
