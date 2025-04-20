#pragma once

#ifdef ERROR
#undef ERROR
#endif

enum LOG_LEVEL {
  DEBUG,
  LOG,
  WARN,
  ERROR
};

void flog(enum LOG_LEVEL level, const char* fmt, ...);
