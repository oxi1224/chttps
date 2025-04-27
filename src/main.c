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

void test_handler(http_request *req, http_response *res) {
  const char *html = "<body>Test</body>";
  res->status_code = 200;
  res->status_message = strdup("OK");
  res->body = strdup(html);
  res->body_length = strlen(html);
}

int main() {
  struct sigaction sa;
  sa.sa_handler = handle_sig;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  http_server server = http_create("127.0.0.1", 9000);

  if (http_use_ssl(&server, "cert.pem", "key.pem") <= 0) {
    // This will print the SSL errors
    exit_and_log(&server, "http_use_ssl failed");
  }

  register_handler(&server, "*", handle_request);
  register_handler(&server, "/test", test_handler);
  
  if (http_init(&server) <= 0) {
    // TODO: Better error system
    exit_and_log(&server, "http_init failed");
  }
  
  request_handler default_handler = hm_get(server.handlers, "*");
  if (default_handler == NULL) flog(WARN, "No default handler specified (set with register_handler(\"*\", fun)");

  flog(DEBUG, "Listening for clients");
  while (!SHUTDOWN) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    http_client client = http_accept(&server, (struct sockaddr*)&client_addr, &client_addr_len);
    // int client_fd = accept(sock_fd, (struct sockaddr*)&client_addr, &client_addr_len);
    if (client.client_fd < 0) {
      close_and_log(&client, "accept() failed with code %d", errno);
      continue;
    }

    if (client._ssl_ret <= 0) {
      close_and_log(&client, "SSL_accept() failed");
      continue;
    }
    
    char buf[CHUNK_SIZE] = {};
    int rec_bytes = http_read(&client, buf, CHUNK_SIZE - 1);
    buf[rec_bytes] = '\0'; // null terminte so strstr knows when to stop
    if (rec_bytes < 0) {
      close_and_log(&client, "http_read failed");
      continue;
    }
     
    char *cur = buf;
    char *line = readline(&cur); // start line
    if (line == NULL) {
      close_and_log(&client, "readline() failed to read the start line");
      continue;
    }

    char *method_end = strchr(line, ' ');
    if (method_end == NULL) {
      close_and_log(&client, "Missing method separator");
      continue;
    }

    char *path_end = strchr(method_end + 1, ' ');
    if (path_end == NULL) {
      close_and_log(&client, "Missing path separator");
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
        close_and_log(&client, "Expected header line does not have delimeter (:)");
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
      close_and_log(&client, "Payload exceeds maximum (%d/%d)", header_length + body_length, MAX_REQUEST_SIZE);
      free_http_request(&request);
      continue;
    }

    request.body_length = body_length;
    if (header_length + body_length >= CHUNK_SIZE) {
      size_t read_bytes = CHUNK_SIZE - (cur - buf);
      char *body = malloc(sizeof(char) * body_length);
      if (body == NULL) {
        exit_and_log(&server, "malloc() failed with code %d", errno);
      }
      memcpy(body, cur, sizeof(char) * read_bytes);

      while (read_bytes != body_length) {
        ssize_t n = http_read(&client, body + read_bytes, body_length - read_bytes);
        // ssize_t n = recv(client_fd, body + read_bytes, body_length - read_bytes, 0);
        if (n <= 0) {
          close_and_log(&client, "recv() failed with code %d", errno);
          free_http_request(&request);
          free(body);
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

    request_handler handler = hm_get(server.handlers, request.path);
    if (handler != NULL) handler(&request, &response);
    else if(default_handler != NULL) default_handler(&request, &response);

    if (http_write(&client, &response) < 0) {
      flog(ERROR, "http_write failed");
    }

    free_http_request(&request);
    free_http_response(&response);
    http_close(&client);
  }

  flog(LOG, "Shutting down...");
  http_stop(&server);
  return 0;
}
