#include "log.h"
#include <stdarg.h>

void log_message(const char* format, ...) {
#ifdef DEBUG
    va_list args;
    va_start(args, format);
    int ret = vprintf(format, args);
    va_end(args);
#endif
}