#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "http.h"
#include "hashmap.h"
#include "log.h"

const char* method_table[] = { "GET", "POST", NULL };
method_t find_method(char* method) {
  for (int i = 0; method_table[i] != NULL; i++) {
    if (strcmp(method_table[i], method) == 0) return i;
  }
  return -1;
}

void free_http_request(http_request *r) {
  free((void*)r->method);
  free((void*)r->path);
  free((void*)r->version);
  free((void*)r->body);
  hm_free((void*)r->headers);
}

void free_http_response(http_response *r) {
  if (r->status_message != NULL) free(r->status_message);
  if (r->body != NULL) free(r->body);
  hm_free(r->headers);
}

char *serialize_response(http_response *r, size_t *out_length) {
  // 15 = 9 (version + space) + 3 (code) + space + 2 (\r\n)
  size_t buf_length = 15 + strlen(r->status_message);
  if (r->headers->size > 0) {
    for (size_t i = 0; i < r->headers->capacity; i++) {
      hm_entry entry = r->headers->entries[i];
      if (entry.key == NULL) continue;
      buf_length += strlen(entry.key) + (strlen((char *)entry.value)) + 4; // space + : + \r\n
    }
  }
  buf_length += 2 + r->body_length;
  
  char *buf = malloc(sizeof(char) * buf_length);
  if (buf == NULL) return NULL;
  int offset = snprintf(buf, buf_length, "%s %d %s\r\n", HTTP_VERSION, r->status_code, r->status_message);
  
  if (r->headers->size > 0) {
    for (size_t i = 0; i < r->headers->capacity; i++) {
      hm_entry entry = r->headers->entries[i];
      if (entry.key == NULL) continue;
      offset += snprintf(buf + offset, buf_length - offset, "%s: %s\r\n", entry.key, (char *)entry.value);
    }
  }
  offset += snprintf(buf + offset, buf_length - offset, "\r\n");
  *out_length = buf_length;
  memcpy(buf + offset, r->body, r->body_length);
  return buf;
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

http_server http_create(const char *address, int port) {
  http_server s = {
    .address = address,
    .port = port,
    .handlers = hm_create(),
    .has_ssl = 0
  };
  return s;
}

void http_use_ssl(http_server *server, const char *cert_path, const char *key_path) {
  server->has_ssl = 1;
  server->cert_path = cert_path;
  server->key_path = key_path;
}

void register_handler(http_server *server, const char *path, request_handler cb) {
  hm_set(server->handlers, strdup(path), (void *)cb);
}

void http_start(http_server *server);
