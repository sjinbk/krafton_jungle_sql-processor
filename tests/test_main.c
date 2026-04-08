#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ast.h"
#include "error.h"
#include "executor.h"
#include "input.h"
#include "parser.h"
#include "schema.h"
#include "storage.h"

static int failures = 0;

/* 저장소 규모가 작기 때문에 별도 프레임워크 없이 local assert 매크로만으로도 테스트를 읽기 쉽게 유지할 수 있다. */
#define ASSERT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "Assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; return; } } while (0)
#define ASSERT_EQ_INT(left, right) do { if ((left) != (right)) { fprintf(stderr, "Assertion failed at %s:%d: %s == %s (%d vs %d)\n", __FILE__, __LINE__, #left, #right, (int)(left), (int)(right)); failures++; return; } } while (0)
#define ASSERT_EQ_LONG(left, right) do { if ((left) != (right)) { fprintf(stderr, "Assertion failed at %s:%d: %s == %s (%ld vs %ld)\n", __FILE__, __LINE__, #left, #right, (long)(left), (long)(right)); failures++; return; } } while (0)
#define ASSERT_EQ_STR(left, right) do { if (strcmp((left), (right)) != 0) { fprintf(stderr, "Assertion failed at %s:%d: %s == %s (%s vs %s)\n", __FILE__, __LINE__, #left, #right, (left), (right)); failures++; return; } } while (0)

/* Temp DB helper는 테스트 중 checked-in sample_db seed가 실수로 바뀌지 않도록 보호한다. */
static int make_dir_if_missing(const char *path) {
    if (mkdir(path, 0777) == 0) {
        return 1;
    }
    return access(path, F_OK) == 0;
}

static int write_text_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }
    if (fputs(contents, file) == EOF) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static int create_temp_db(char *db_root, size_t db_root_size) {
    int i;
    char public_path[256];

    if (!make_dir_if_missing(".tmp")) {
        return 0;
    }

    for (i = 0; i < 100; ++i) {
        snprintf(db_root, db_root_size, ".tmp/test_db_%ld_%d", (long)getpid(), i);
        if (access(db_root, F_OK) != 0) {
            break;
        }
    }
    if (i == 100 || mkdir(db_root, 0777) != 0) {
        return 0;
    }

    snprintf(public_path, sizeof(public_path), "%s/public", db_root);
    if (mkdir(public_path, 0777) != 0) {
        rmdir(db_root);
        return 0;
    }

    {
        size_t schema_path_size = strlen(public_path) + strlen("/users.schema") + 1;
        size_t csv_path_size = strlen(public_path) + strlen("/users.csv") + 1;
        char *schema_path = (char *)malloc(schema_path_size);
        char *csv_path = (char *)malloc(csv_path_size);

        if (schema_path == NULL || csv_path == NULL) {
            free(schema_path);
            free(csv_path);
            rmdir(public_path);
            rmdir(db_root);
            return 0;
        }

        snprintf(schema_path, schema_path_size, "%s/users.schema", public_path);
        snprintf(csv_path, csv_path_size, "%s/users.csv", public_path);
        if (!write_text_file(schema_path, "id,INT\nname,TEXT\nage,INT\n") ||
            !write_text_file(csv_path, "1,\"alice\",31\n2,\"bob\",24\n")) {
            remove(schema_path);
            remove(csv_path);
            free(schema_path);
            free(csv_path);
            rmdir(public_path);
            rmdir(db_root);
            return 0;
        }

        free(schema_path);
        free(csv_path);
    }

    return 1;
}

static void destroy_temp_db(const char *db_root) {
    size_t path_size = strlen(db_root) + strlen("/public/users.schema") + 1;
    char *path = (char *)malloc(path_size);

    if (path == NULL) {
        return;
    }

    snprintf(path, path_size, "%s/public/users.schema", db_root);
    remove(path);
    snprintf(path, path_size, "%s/public/users.csv", db_root);
    remove(path);
    snprintf(path, path_size, "%s/public", db_root);
    rmdir(path);
    rmdir(db_root);
    free(path);
}

/* splitter가 comment를 건너뛰면서도 실제 statement 경계를 올바르게 찾는지 확인한다. */
static void test_split_statements(void) {
    const char *sql = "-- comment\nINSERT INTO users VALUES (1, 'kim', 25);\nSELECT id, name FROM users;\n";
    StatementChunkList chunks;
    Error error;

    error_clear(&error);
    ASSERT_TRUE(split_statements(sql, &chunks, &error));
    ASSERT_EQ_INT((int)chunks.count, 2);
    ASSERT_TRUE(strstr(chunks.items[0].text, "INSERT") != NULL);
    ASSERT_TRUE(strstr(chunks.items[1].text, "SELECT") != NULL);
    ASSERT_EQ_INT(chunks.items[0].line, 2);
    ASSERT_EQ_INT(chunks.items[0].column, 1);
    statement_chunk_list_free(&chunks);
}

/* 입력 계약상 trailing semicolon이 필수이므로, 없으면 실패해야 한다. */
static void test_split_statements_requires_semicolon(void) {
    StatementChunkList chunks;
    Error error;

    error_clear(&error);
    ASSERT_TRUE(!split_statements("SELECT * FROM users", &chunks, &error));
    ASSERT_EQ_INT(error.code, ERR_PARSE);
    ASSERT_EQ_INT(error.line, 1);
    ASSERT_EQ_INT(error.column, 1);
    ASSERT_EQ_STR(error.message, "statement must end with ';'");
}

/* statement header는 줄바꿈과 과한 공백을 정리해도 읽기 쉬운 형태를 유지해야 한다. */
static void test_statement_header_print(void) {
    FILE *tmp;
    char buffer[256];
    size_t read_size;

    tmp = tmpfile();
    ASSERT_TRUE(tmp != NULL);
    fprint_statement_header(tmp, 2, "SELECT   id,\n  name FROM public.users");
    rewind(tmp);
    read_size = fread(buffer, 1, sizeof(buffer) - 1, tmp);
    buffer[read_size] = '\0';
    ASSERT_EQ_STR(buffer, "-- Statement 2: SELECT id, name FROM public.users\n");
    fclose(tmp);
}

/* 긴 SQL은 잘라서 보여 줘야 demo 출력이 한눈에 읽힌다. */
static void test_statement_header_truncates_long_sql(void) {
    FILE *tmp;
    char buffer[256];
    size_t read_size;

    tmp = tmpfile();
    ASSERT_TRUE(tmp != NULL);
    fprint_statement_header(
        tmp,
        1,
        "INSERT INTO users (id, name, age) VALUES (1, 'alice', 31), (2, 'bob', 24), (3, 'kim', 25)"
    );
    rewind(tmp);
    read_size = fread(buffer, 1, sizeof(buffer) - 1, tmp);
    buffer[read_size] = '\0';
    ASSERT_EQ_STR(buffer, "-- Statement 1: INSERT INTO users (id, name, age) VALUES (1, 'alice', 31)...\n");
    fclose(tmp);
}

/* 다음 statement가 같은 줄에서 시작해도 error column이 정확해야 한다. */
static void test_same_line_statement_column_after_spaces(void) {
    const char *sql = "SELECT * FROM users;   SELECT FROM users;";
    StatementChunkList chunks;
    Statement statement;
    Error error;

    error_clear(&error);
    ASSERT_TRUE(split_statements(sql, &chunks, &error));
    ASSERT_EQ_INT((int)chunks.count, 2);
    ASSERT_EQ_INT(chunks.items[1].line, 1);
    ASSERT_EQ_INT(chunks.items[1].column, 24);

    error_clear(&error);
    ASSERT_TRUE(!parse_statement(chunks.items[1].text, chunks.items[1].line, chunks.items[1].column, &statement, &error));
    ASSERT_EQ_INT(error.code, ERR_PARSE);
    ASSERT_EQ_INT(error.line, 1);
    ASSERT_EQ_INT(error.column, 31);
    ASSERT_EQ_STR(error.message, "expected projection list or '*'");
    statement_chunk_list_free(&chunks);
}

/* 핵심 INSERT 문법 경로를 한 테스트에서 함께 검증한다. */
static void test_parser_insert(void) {
    Statement statement;
    Error error;

    error_clear(&error);
    ASSERT_TRUE(parse_statement("INSERT INTO public.users (id, name, age) VALUES (1, 'O''Neil', 25), (2, 'lee', 30)", 1, 1, &statement, &error));
    ASSERT_EQ_INT(statement.type, STMT_INSERT);
    ASSERT_EQ_STR(statement.as.insert_stmt.schema, "public");
    ASSERT_EQ_STR(statement.as.insert_stmt.table, "users");
    ASSERT_EQ_INT((int)statement.as.insert_stmt.columns.count, 3);
    ASSERT_EQ_INT((int)statement.as.insert_stmt.row_count, 2);
    ASSERT_EQ_LONG(statement.as.insert_stmt.rows[0].values[0].int_value, 1);
    ASSERT_EQ_STR(statement.as.insert_stmt.rows[0].values[1].text_value, "O'Neil");
    statement_free(&statement);
}

/* SELECT는 projection을 읽어야 하고, schema를 생략하면 기본 public을 적용해야 한다. */
static void test_parser_select_projection(void) {
    Statement statement;
    Error error;

    error_clear(&error);
    ASSERT_TRUE(parse_statement("SELECT id, name FROM users", 1, 1, &statement, &error));
    ASSERT_EQ_INT(statement.type, STMT_SELECT);
    ASSERT_EQ_INT(statement.as.select_stmt.select_all, 0);
    ASSERT_EQ_INT((int)statement.as.select_stmt.columns.count, 2);
    ASSERT_EQ_STR(statement.as.select_stmt.columns.items[0], "id");
    ASSERT_EQ_STR(statement.as.select_stmt.columns.items[1], "name");
    ASSERT_EQ_STR(statement.as.select_stmt.schema, "public");
    statement_free(&statement);
}

/* FROM 누락은 parser 오류 메시지 품질을 확인하기 좋은 사례다. */
static void test_parser_select_missing_from(void) {
    Statement statement;
    Error error;

    error_clear(&error);
    ASSERT_TRUE(!parse_statement("SELECT id, name users", 1, 1, &statement, &error));
    ASSERT_EQ_INT(error.code, ERR_PARSE);
    ASSERT_EQ_STR(error.message, "expected FROM");
}

/* sample database를 기준으로 schema load와 CSV read가 실제로 연결되는지 증명한다. */
static void test_schema_and_storage(void) {
    TableSchema schema;
    DataSet data_set;
    Error error;

    error_clear(&error);
    ASSERT_TRUE(schema_load("sample_db", "public", "users", &schema, &error));
    ASSERT_EQ_INT((int)schema.column_count, 3);
    ASSERT_TRUE(storage_load_rows("sample_db", &schema, &data_set, &error));
    ASSERT_TRUE(data_set.count >= 2);
    ASSERT_EQ_STR(data_set.rows[0].values[1].text_value, "alice");
    data_set_free(&data_set);
    schema_free(&schema);
}

/* 따옴표가 들어간 TEXT 값이 append 후 reload를 거쳐도 그대로 유지되는지 확인한다. */
static void test_storage_csv_escaping(void) {
    char db_root[256];
    TableSchema schema;
    DataSet data_set;
    SqlValue row[3];
    Error error;

    ASSERT_TRUE(create_temp_db(db_root, sizeof(db_root)));
    error_clear(&error);
    ASSERT_TRUE(schema_load(db_root, "public", "users", &schema, &error));

    row[0].type = SQL_VALUE_INT;
    row[0].int_value = 3;
    row[0].text_value = NULL;
    row[1].type = SQL_VALUE_TEXT;
    row[1].int_value = 0;
    row[1].text_value = "A \"quoted\" user";
    row[2].type = SQL_VALUE_INT;
    row[2].int_value = 41;
    row[2].text_value = NULL;

    ASSERT_TRUE(storage_append_row(db_root, &schema, row, 3, &error));
    ASSERT_TRUE(storage_load_rows(db_root, &schema, &data_set, &error));
    ASSERT_EQ_INT((int)data_set.count, 3);
    ASSERT_EQ_STR(data_set.rows[2].values[1].text_value, "A \"quoted\" user");

    data_set_free(&data_set);
    schema_free(&schema);
    destroy_temp_db(db_root);
}

/* 실제 SELECT를 실행해 formatted output 안에 기대한 데이터가 들어 있는지 확인한다. */
static void test_executor_select(void) {
    Statement statement;
    Error error;
    FILE *tmp;
    char buffer[2048];
    size_t read_size;

    error_clear(&error);
    ASSERT_TRUE(parse_statement("SELECT id, name FROM users", 1, 1, &statement, &error));
    tmp = tmpfile();
    ASSERT_TRUE(tmp != NULL);
    ASSERT_TRUE(execute_statement(&statement, "sample_db", tmp, &error));
    rewind(tmp);
    read_size = fread(buffer, 1, sizeof(buffer) - 1, tmp);
    buffer[read_size] = '\0';
    ASSERT_TRUE(strstr(buffer, "alice") != NULL);
    ASSERT_TRUE(strstr(buffer, "(2 rows)") != NULL);
    fclose(tmp);
    statement_free(&statement);
}

/* 타입 불일치는 실행 단계에서 명확한 메시지와 함께 거절되어야 한다. */
static void test_executor_type_mismatch(void) {
    Statement statement;
    Error error;
    FILE *tmp;

    error_clear(&error);
    ASSERT_TRUE(parse_statement("INSERT INTO users (id, name, age) VALUES ('oops', 'kim', 25)", 1, 1, &statement, &error));
    tmp = tmpfile();
    ASSERT_TRUE(tmp != NULL);
    ASSERT_TRUE(!execute_statement(&statement, "sample_db", tmp, &error));
    ASSERT_EQ_STR(error.message, "type mismatch for column 'id'");
    fclose(tmp);
    statement_free(&statement);
}

/* 잘못된 SELECT는 기대한 parse 위치와 오류 메시지를 만들어야 한다. */
static void test_parser_error(void) {
    Statement statement;
    Error error;

    error_clear(&error);
    ASSERT_TRUE(!parse_statement("SELECT FROM users", 1, 1, &statement, &error));
    ASSERT_EQ_INT(error.code, ERR_PARSE);
    ASSERT_EQ_INT(error.line, 1);
    ASSERT_EQ_INT(error.column, 8);
    ASSERT_EQ_STR(error.message, "expected projection list or '*'");
}

/* AST debug printer도 지원된 디버그 흐름의 일부이므로 출력 형태를 고정해 둔다. */
static void test_statement_fprint(void) {
    Statement statement;
    Error error;
    FILE *tmp;
    char buffer[256];
    size_t read_size;

    error_clear(&error);
    ASSERT_TRUE(parse_statement("SELECT id, name FROM users", 1, 1, &statement, &error));
    tmp = tmpfile();
    ASSERT_TRUE(tmp != NULL);
    statement_fprint(tmp, &statement);
    rewind(tmp);
    read_size = fread(buffer, 1, sizeof(buffer) - 1, tmp);
    buffer[read_size] = '\0';
    ASSERT_EQ_STR(buffer, "AST SELECT public.users columns=[id, name]\n");
    fclose(tmp);
    statement_free(&statement);
}

/* 계약 중심 테스트를 읽기 쉬운 순서로 실행하고 실패 수를 누적한다. */
int main(void) {
    test_split_statements();
    test_split_statements_requires_semicolon();
    test_statement_header_print();
    test_statement_header_truncates_long_sql();
    test_same_line_statement_column_after_spaces();
    test_parser_insert();
    test_parser_select_projection();
    test_parser_select_missing_from();
    test_schema_and_storage();
    test_storage_csv_escaping();
    test_executor_select();
    test_executor_type_mismatch();
    test_parser_error();
    test_statement_fprint();

    if (failures > 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    printf("All tests passed\n");
    return 0;
}
