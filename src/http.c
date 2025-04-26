#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

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

int http_use_ssl(http_server *server, const char *cert_path, const char *key_path) {
  server->has_ssl = 1;

  SSL_load_error_strings();
  OpenSSL_add_ssl_algorithms();

  server->_ssl_context = SSL_CTX_new(TLS_method());
  if (server->_ssl_context == NULL) return -1;

  SSL_CTX_set_ecdh_auto(server->_ssl_context, 1);
  int ret = SSL_CTX_use_certificate_file(server->_ssl_context, "cert.pem", SSL_FILETYPE_PEM);
  if (ret <= 0) return ret;
  ret = SSL_CTX_use_PrivateKey_file(server -> _ssl_context, "key.pem", SSL_FILETYPE_PEM);
  if (ret <= 0) return ret;
  return 1;
}

void register_handler(http_server *server, const char *path, request_handler cb) {
  hm_set(server->handlers, strdup(path), (void *)cb);
}

http_client http_accept(http_server *server, struct sockaddr *__restrict addr, socklen_t *__restrict addr_len) {
  http_client c = { 0 };
  c.client_fd = accept(server->_socket_fd, addr, addr_len);
  
  if (server->has_ssl) {
    c.ssl = SSL_new(server->_ssl_context);
    SSL_set_fd(c.ssl, c.client_fd);
    c._ssl_ret = SSL_accept(c.ssl);

    if (c._ssl_ret <= 0) {
      SSL_free(c.ssl);
      c.ssl = NULL;
    }
  }
  return c;
}

int http_read(http_client *client, void *buf, size_t n) {
  if (client->ssl == NULL) {
    return recv(client->client_fd, buf, n, 0);
  } else {
    return SSL_read(client->ssl, buf, n);
  }
}

int http_write(http_client *client, http_response *response) {
  size_t buf_len;
  char *buf = serialize_response(response, &buf_len);
  int ret;
  if (client->ssl == NULL) {
    ret = send(client->client_fd, buf, buf_len, 0);
  } else {
    ret = SSL_write(client->ssl, buf, buf_len);
  }
  free(buf);
  return ret;
}

int http_init(http_server *server) {
  server->_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = inet_addr(server->address),
    .sin_port = htons(server->port)
  };

  int ret = bind(server->_socket_fd, (struct sockaddr*)&addr, sizeof(addr));
  if (ret < 0) return ret;
  ret = listen(server->_socket_fd, 10);
  if (ret < 0) return ret;
  return 1;
}

void http_stop(http_server *server) {
  if (server->_ssl_context != NULL) SSL_CTX_free(server->_ssl_context);
  if (server->_socket_fd >= 0) close(server->_socket_fd);
  hm_free(server->handlers);
  free(server);
}

void http_close(http_client *client) {
  if (client->ssl != NULL) {
    if (client->_ssl_ret > 0) SSL_shutdown(client->ssl);
    SSL_free(client->ssl);
  }
  if (client->client_fd >= 0) close(client->client_fd);
}

void exit_and_log(http_server *server, const char *fmt, ...) {
  http_stop(server);

  va_list args;
  va_start(args, fmt);
  flog(ERROR, fmt, args);
  va_end(args);
  
  if (ERR_peek_error() != 0) ERR_print_errors_fp(stderr);
  exit(EXIT_FAILURE);
}

void close_and_log(http_client *client, const char *fmt, ...) {
  http_close(client);

  va_list args;
  va_start(args, fmt);
  flog(WARN, fmt, args);
  va_end(args);

  if (ERR_peek_error() != 0) ERR_print_errors_fp(stderr);
}
