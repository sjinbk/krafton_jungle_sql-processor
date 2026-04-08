#ifndef ERROR_H
#define ERROR_H

/* Error는 모든 레이어가 공유하는 공통 오류 보고 구조다.
 * 위치를 아는 레이어는 line/column을 채우고, 모르는 레이어는 0을 사용한다.
 */
typedef struct Error {
    int code;
    int line;
    int column;
    char message[256];
} Error;

enum {
    ERR_OK = 0,
    ERR_IO = 1,
    ERR_LEX = 2,
    ERR_PARSE = 3,
    ERR_EXEC = 4,
    ERR_MEMORY = 5,
    ERR_SCHEMA = 6,
    ERR_STORAGE = 7
};

/* error_clear는 재사용 전 초기화, error_set은 printf-style 메시지 설정에 쓴다. */
void error_clear(Error *error);
void error_set(Error *error, int code, int line, int column, const char *fmt, ...);

#endif
