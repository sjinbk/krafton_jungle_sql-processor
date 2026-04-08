#include "error.h"

#include <stdarg.h>
#include <stdio.h>

/* Error 구조체는 한 번 만들어 여러 레이어에서 재사용하므로 clear/set helper가 중심이다. */
void error_clear(Error *error) {
    if (error == NULL) {
        return;
    }

    error->code = ERR_OK;
    error->line = 0;
    error->column = 0;
    error->message[0] = '\0';
}

/* 공통 오류 메시지 형식을 위해 printf-style formatting을 사용한다. */
void error_set(Error *error, int code, int line, int column, const char *fmt, ...) {
    va_list args;

    if (error == NULL) {
        return;
    }

    error->code = code;
    error->line = line;
    error->column = column;
    va_start(args, fmt);
    vsnprintf(error->message, sizeof(error->message), fmt, args);
    va_end(args);
    error->message[sizeof(error->message) - 1] = '\0';
}
