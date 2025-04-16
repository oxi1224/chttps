#include <stdarg.h>
#include <stdio.h>

#include "log.h"

void flog(enum LOG_LEVEL level, const char* fmt, ...) {
  char *lvl; 

  switch (level) {
    case DEBUG: lvl = "DEBUG"; break;
    case LOG: lvl = "LOG"; break;
    case WARN: lvl = "WARN"; break;
    case ERROR: lvl = "ERROR"; break;
  }

  printf("[%s] ", lvl);
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  printf("\n");
}
