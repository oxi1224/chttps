#include <stdlib.h>
#include <string.h>
// #include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "http.h"
// #include "log.h"

const char* method_table[] = { "GET", "POST", NULL };
method_t find_method(char* method) {
  for (int i = 0; method_table[i] != NULL; i++) {
    if (strcmp(method_table[i], method) == 0) return i;
  }
  return -1;
}

void free_http_request(http_request r) {
  free((void*)r.method);
  free((void*)r.path);
  free((void*)r.version);
  free((void*)r.body);
  for (size_t i = 0; i < r.header_list.count; i++) {
    free((void*)r.header_list.headers[i].name);
    free((void*)r.header_list.headers[i].value);
  }
}

char *readline(char **str_ptr) {
  if (!str_ptr || !(*str_ptr)) return NULL;
  char *start = *str_ptr;
  char *end = strstr(start, "\r\n");
  if (!end) return NULL;
  
  size_t line_len = end - start;
  char *line = malloc(sizeof(char) * (line_len + 1));
  strncpy(line, start, line_len);
  line[line_len] = '\0';
  *str_ptr = end + 2; // move the pointer past \r\n so next call gives next line
  return line;
}

char *substr(const char* src, size_t start, size_t end) {
  size_t len = end - start;
  char *val = malloc(sizeof(char) * (len + 1));
  strncpy(val, src + start, len);
  val[len] = '\0';
  return val;
}

// http_request http_parse_request(const char* buf);
