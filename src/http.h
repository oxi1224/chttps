#pragma once

#include <stdio.h>
#include "hashmap.h"

#define CHUNK_SIZE 16 * 1024 // 16 KB
#define MAX_REQUEST_SIZE 24 * 1024 * 1024 // 24 MB
#define HTTP_VERSION "HTTP/1.1"

typedef enum {
  // OPTIONS,
  GET,
  // HEAD,
  POST,
  // PUT,
  // DELETE,
  // TRACE,
  // CONNECT
} method_t;
method_t find_method(char* method);

typedef struct {
  const char *name;
  const char *value;
} http_header;

typedef struct {
  const char *path;
  const char *method;
  const char *version;
  const char *body;
  size_t body_length;
  const hash_map *headers;
} http_request;

typedef struct {
  int status_code;
  char *status_message;
  hash_map *headers;
  char *body;
  size_t body_length;
} http_response;

// TODO: Move this here later, prefer it in main.c for now
// void http_start(const char *addr, int port);

void free_http_request(http_request *r);
void free_http_response(http_response *r);

char *serialize_response(http_response *r, size_t *out_length);

// TODO: Move this to a separate file (idk, maybe?)
char *readline(char **str_ptr);
char *substr(const char* src, size_t start, size_t end);
