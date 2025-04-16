#pragma once

enum LOG_LEVEL {
  DEBUG,
  LOG,
  WARN,
  ERROR
};

void flog(enum LOG_LEVEL level, const char* fmt, ...);
