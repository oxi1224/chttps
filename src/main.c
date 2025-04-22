#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "hashmap.h"
#include "http.h"
#include "log.h"

volatile sig_atomic_t SHUTDOWN = 0;
void handle_sig(int sig) { SHUTDOWN = 1; }

void exit_and_log(int sock_fd, SSL_CTX *context, const char *fmt, ...) {
  if (context != NULL) SSL_CTX_free(context);
  if (sock_fd > -1) close(sock_fd);
  if (SHUTDOWN) return;

  va_list args;
  va_start(args, fmt);
  flog(ERROR, fmt, args);
  va_end(args);
  
  if (ERR_peek_error() != 0) ERR_print_errors_fp(stderr);
  exit(EXIT_FAILURE);
}

void close_and_log(int client_fd, SSL *ssl, const char *fmt, ...) {
  if (ssl != NULL) SSL_free(ssl);
  if (client_fd > -1) close(client_fd);
  if (SHUTDOWN) return;

  va_list args;
  va_start(args, fmt);
  flog(WARN, fmt, args);
  va_end(args);

  if (ERR_peek_error() != 0) ERR_print_errors_fp(stderr);
}

void handle_request(http_request *req, http_response *res) {
  printf("-------------\n"); 
  flog(DEBUG, "Method: %s | Path: %s | Version: %s", req->method, req->path, req->version);
  flog(DEBUG, "Headers(%d)", req->headers->size);
  for (size_t i = 0; i < req->headers->capacity; i++) {
    if (req->headers->entries[i].value == NULL) continue;
    flog(
      DEBUG,
      "%s: %s",
      req->headers->entries[i].key,
      req->headers->entries[i].value
    );
  }
  flog(DEBUG, "Body (%d)", req->body_length);
  flog(DEBUG, "%.*s", req->body_length, req->body);
  printf("-------------\n");
  
  res->status_code = 200;
  res->status_message = strdup("OK");
  hm_set(res->headers, strdup("host"), strdup("localhost:9000"));
  hm_set(res->headers, strdup("Content-Length"), strdup("11"));
  res->body = malloc(sizeof(char) * 11);
  memcpy(res->body, "Hello World", 11);
  res->body_length = 11;
}


int main() {
  struct sigaction sa;
  sa.sa_handler = handle_sig;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  SSL_load_error_strings();
  OpenSSL_add_ssl_algorithms();

  SSL_CTX *context = SSL_CTX_new(TLS_method());
  if (context == NULL) exit_and_log(-1, NULL, "Unable to create SSL context");

  SSL_CTX_set_ecdh_auto(context, 1);
  if (SSL_CTX_use_certificate_file(context, "cert.pem", SSL_FILETYPE_PEM) <= 0) {
    exit_and_log(-1, context, "Unable to read cert.pem");
  }

  if (SSL_CTX_use_PrivateKey_file(context, "key.pem", SSL_FILETYPE_PEM) <= 0) {
    exit_and_log(-1, context, "Unable to read key.pem");
  }

  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = inet_addr("127.0.0.1"),
    .sin_port = htons(9000)
  };

  if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    exit_and_log(sock_fd, context, "bind() failed with code %d", errno);
  }

  if (listen(sock_fd, 10) < 0) {
    exit_and_log(sock_fd, context, "listen() failed with code %d", errno);
  }
  
  flog(DEBUG, "Listening for clients");
  while (!SHUTDOWN) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd = accept(sock_fd, (struct sockaddr*)&client_addr, &client_addr_len);

    if (client_fd < 0) {
      close_and_log(-1, NULL, "accept() failed with code %d", errno);
      continue;
    }

    SSL* ssl = SSL_new(context);
    SSL_set_fd(ssl, client_fd);
    if (SSL_accept(ssl) <= 0) {
      close_and_log(client_fd, ssl, "SSL_accept() failed");
      continue;
    }
    
    char buf[CHUNK_SIZE] = {};
    int rec_bytes = SSL_read(ssl, buf, CHUNK_SIZE - 1); // -1 - if request fills entire buffer there is space for \0
    buf[rec_bytes] = '\0'; // null terminte so strstr knows when to stop
    if (rec_bytes < 0) {
      close_and_log(client_fd, ssl, "SSL_read() failed with code %d", SSL_get_error(ssl, rec_bytes));
      continue;
    }
     
    char *cur = buf;
    char *line = readline(&cur); // start line
    if (line == NULL) {
      close_and_log(client_fd, ssl, "readline() failed to read the start line");
      continue;
    }

    char *method_end = strchr(line, ' ');
    if (method_end == NULL) {
      close_and_log(client_fd, ssl, "Missing method separator");
      continue;
    }

    char *path_end = strchr(method_end + 1, ' ');
    if (path_end == NULL) {
      close_and_log(client_fd, ssl, "Missing path separator");
      continue;
    }
    
    http_request request = {
      .method = substr(line, 0, method_end - line),
      .path = substr(line, method_end - line + 1, path_end - line),
      .version = substr(line + 1, path_end - line, strlen(line)),
    };
    hash_map *headers = hm_create();

    int parsing_err = 0;
    size_t body_length = 0;
    while ((line = readline(&cur)) != NULL && strcmp(line, "") != 0) {
      char* delim = strstr(line, ":");
      if (delim == NULL) {
        close_and_log(client_fd, ssl, "Expected header line does not have delimeter (:)");
        parsing_err = 1;
        break;
      }
      
      char *name = substr(line, 0, delim - line);
      char *value = substr(line, delim - line + 2, strlen(line));
      if (strcmp(name, "Content-Length") == 0) body_length = atoi(value);
      hm_set(headers, name, value);
    }
    if (parsing_err) continue;
    request.headers = headers;
  
    size_t header_length = cur - buf - 1;
    if (header_length + body_length > MAX_REQUEST_SIZE) {
      /// TODO: Respond with a 413 Payload Too Large, for now just exit
      close_and_log(client_fd, ssl, "Payload exceeds maximum (%d/%d)", header_length + body_length, MAX_REQUEST_SIZE);
      free_http_request(&request);
      continue;
    }

    request.body_length = body_length;
    if (header_length + body_length >= CHUNK_SIZE) {
      size_t read_bytes = CHUNK_SIZE - (cur - buf);
      char *body = malloc(sizeof(char) * body_length);
      if (body == NULL) {
        exit_and_log(sock_fd, context, "malloc() failed with code %d", errno);
      }
      memcpy(body, cur, sizeof(char) * read_bytes);

      while (read_bytes != body_length) {
        ssize_t n = recv(client_fd, body + read_bytes, body_length - read_bytes, 0);
        if (n <= 0) {
          close_and_log(client_fd, ssl, "recv() failed with code %d", errno);
          free_http_request(&request);
          parsing_err = 1;
          break;
        }
        read_bytes += n;
      }
      request.body = body;
    } else {
      char *body = malloc(sizeof(char) * body_length);
      memcpy(body, cur, sizeof(char) * body_length);
      request.body = body;
    }
    if (parsing_err) continue;
    
    http_response response;
    response.headers = hm_create();
    handle_request(&request, &response);

    size_t out_buf_len;
    char *out_buf = serialize_response(&response, &out_buf_len);
    if (SSL_write(ssl, out_buf, out_buf_len) < 0) {
      flog(ERROR, "SSL_write() failed with code %d", errno);
    }

    free_http_request(&request);
    free_http_response(&response);
    SSL_free(ssl);
    close(client_fd);
  }

  flog(LOG, "Shutting down...");
  SSL_CTX_free(context);
  close(sock_fd);
  return 0;
}
