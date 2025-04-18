#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "hashmap.h"
#include "http.h"
#include "log.h"

int main() {
  // http_start("localhost", 9000);
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = inet_addr("127.0.0.1"),
    .sin_port = htons(9000)
  };

  if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    flog(ERROR, "bind() failed with code %d", errno);
    exit(EXIT_FAILURE);
  }

  if (listen(sock_fd, 10) < 0) {
    flog(ERROR, "listen() failed with code %d", errno);
    exit(EXIT_FAILURE);
  }
  
  flog(DEBUG, "Listening for clients");
  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd = accept(sock_fd, (struct sockaddr*)&client_addr, &client_addr_len);

    if (client_fd < 0) {
      flog(ERROR, "accept() failed with code %d", errno);
      exit(EXIT_FAILURE);
    }
    
    char buf[CHUNK_SIZE] = {};
    int rec_bytes = recv(client_fd, &buf, CHUNK_SIZE - 1, 0); // -1 - if request fills entire buffer there is space for \0
    buf[rec_bytes] = '\0'; // null terminte so strstr knows when to stop
    if (rec_bytes < 0) {
      flog(ERROR, "recv() failed with code %d", errno);
      exit(EXIT_FAILURE);
    }
     
    char *cur = buf;
    char *line = readline(&cur); // start line
    if (line == NULL) {
      flog(WARN, "readline() failed to read the start line");
      close(client_fd);
      continue;
    }

    char *method_end = strchr(line, ' ');
    if (method_end == NULL) {
      flog(WARN, "Missing method separator");
      close(client_fd);
      continue;
    }

    char *path_end = strchr(method_end + 1, ' ');
    if (path_end == NULL) {
      flog(WARN, "Missing path separator");
      close(client_fd);
      continue;
    }
    
    http_request request = {
      .method = substr(line, 0, method_end - line),
      .path = substr(line, method_end - line + 1, path_end - line),
      .version = substr(line, path_end - line, strlen(line)),
    };
    hash_map *headers = hm_create();

    int parsing_err = 0;
    size_t body_length = 0;
    while ((line = readline(&cur)) != NULL && strcmp(line, "") != 0) {
      char* delim = strstr(line, ":");
      if (delim == NULL) {
        flog(WARN, "Expected header line does not have delimeter (:)");
        close(client_fd);
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
      flog(WARN, "Payload exceeds maximum (%d)", header_length +  body_length);
      close(client_fd);
      free_http_request(request);
      continue;
    }

    request.body_length = body_length;
    if (header_length + body_length >= CHUNK_SIZE) {
      size_t read_bytes = CHUNK_SIZE - (cur - buf);
      char *body = malloc(sizeof(char) * body_length);
      if (body == NULL) {
        flog(ERROR, "malloc() failed with code %d", errno);
        exit(EXIT_FAILURE);
      }
      memcpy(body, cur, sizeof(char) * read_bytes);

      while (read_bytes != body_length) {
        ssize_t n = recv(client_fd, body + read_bytes, body_length - read_bytes, 0);
        if (n <= 0) {
          flog(WARN, "recv() failed or connection closed while reading (%d)", errno);
          free(body);
          free_http_request(request);
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
    
    printf("-------------\n"); 
    flog(DEBUG, "Method: %s | Path: %s | Version: %s", request.method, request.path, request.version);
    flog(DEBUG, "Headers(%d)", request.headers->size);
    for (size_t i = 0; i < request.headers->capacity; i++) {
      if (request.headers->entries[i].value == NULL) continue;
      flog(
        DEBUG,
        "%s: %s",
        request.headers->entries[i].key,
        request.headers->entries[i].value
      );
    }
    flog(DEBUG, "Body (%d)", request.body_length);
    flog(DEBUG, "%.*s", request.body_length, request.body);
    printf("-------------\n"); 

    if (send(client_fd, &buf, CHUNK_SIZE, 0) < 0) {
      printf("send() failed with code %d", errno);
    }
    free_http_request(request);
    close(client_fd);
  }

  return 0;
}
