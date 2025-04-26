#pragma once

#include <stdio.h>

#include <sys/socket.h>
#include <unistd.h>

#include <openssl/ssl.h>

#include "hashmap.h"

#define CHUNK_SIZE 16 * 1024 // 16 KB
#define MAX_REQUEST_SIZE 24 * 1024 * 1024 // 24 MB
#define HTTP_VERSION "HTTP/1.0"

typedef enum {
  GET,
  POST,
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

void free_http_request(http_request *r);
void free_http_response(http_response *r);
char *serialize_response(http_response *r, size_t *out_length);

char *readline(char **str_ptr);
char *substr(const char* src, size_t start, size_t end);

typedef struct {
  const char *address;
  int port;
  hash_map *handlers;
  int has_ssl;
  
  // Internal
  int _socket_fd;
  SSL_CTX *_ssl_context;
} http_server;

typedef struct {
  int client_fd;
  SSL *ssl;
  int _ssl_ret;
} http_client;

typedef void(*request_handler)(http_request*, http_response*);

http_server http_create(const char *address, int port);
int http_use_ssl(http_server *server, const char *cert_path, const char *key_path);
void register_handler(http_server *server, const char *path, request_handler cb);

http_client http_accept(http_server *server, struct sockaddr *__restrict addr, socklen_t *__restrict addr_len);
int http_read(http_client *client, void *buf, size_t n);
int http_write(http_client *client, http_response *response);

int http_init(http_server *server);
void http_stop(http_server *server);
void http_close(http_client *client);

void exit_and_log(http_server *server, const char *fmt, ...);
void close_and_log(http_client *client, const char *fmt, ...);

// void http_start(http_server *server);
