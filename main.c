#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define CHUNK_SIZE 16 * 1024 // 16 KB

// Right now I just want the most basic http server there is
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
const char* method_table[] = { "GET", "POST", NULL };

method_t find_method(char* method) {
  for (int i = 0; method_table[i] != NULL; i++) {
    if (strcmp(method_table[i], method) == 0) return i;
  }
  return -1;
}

enum LOG_LEVEL {
  DEBUG,
  LOG,
  WARN,
  ERROR
};

void log_f(enum LOG_LEVEL level, const char* fmt, ...) {
  char *lvl; 

  switch (level) {
    case DEBUG: lvl = "DEBUG"; break;
    case LOG: lvl = "LOG"; break;
    case WARN: lvl = "WARN"; break;
    case ERROR: lvl = "ERROR"; break;
  }

  printf("[%s] ", lvl);
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  printf("\n");
}

char* readline(char **str_ptr) {
  if (!str_ptr || !(*str_ptr)) return NULL;
  char *start = *str_ptr;
  char *end = strstr(start, "\r\n");
  if (!end) return NULL;
  
  size_t line_len = end - start;
  char *line = malloc(sizeof(char) * (line_len + 1));
  strncpy(line, start, line_len);
  line[line_len] = '\0';
  *str_ptr += 2; // move the pointer past \r\n so next call gives next line
  return line;
}

int main() {
  log_f(ERROR, "Hello %s", "World");
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = inet_addr("127.0.0.1"),
    .sin_port = htons(9000)
  };

  if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    printf("bind() failed with code %d", errno);
    exit(EXIT_FAILURE);
  }

  if (listen(sock_fd, 10) < 0) {
    printf("listen() failed with code %d", errno);
    exit(EXIT_FAILURE);
  }
  
  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd = accept(sock_fd, (struct sockaddr*)&client_addr, &client_addr_len);

    if (client_fd < 0) {
      printf("accept() failed with code %d", errno);
      exit(EXIT_FAILURE);
    }
    
    char buf[CHUNK_SIZE] = {};
    int rec_bytes = recv(client_fd, &buf, CHUNK_SIZE - 1, 0); // -1 - if request fills entire buffer there is space for \0
    buf[rec_bytes] = '\0'; // null terminte so strstr knows when to stop
    if (rec_bytes < 0) {
      printf("recv() failed with code %d", errno);
      exit(EXIT_FAILURE);
    }
     
    char *cur = buf;
    char *line = readline(&cur); // start line
    if (line == NULL) {
      printf("readline() failed to read the start line\n");
      close(client_fd);
      continue;
    }

    char *method_end = strchr(line, ' ');
    if (method_end == NULL) {
      printf("Missing method separator\n");
      close(client_fd);
      continue;
    }

    char *path_end = strchr(method_end + 1, ' ');
    if (path_end == NULL) {
      printf("Missing path separator\n");
      close(client_fd);
      continue;
    }

    size_t method_len = method_end - line;
    char method[method_len + 1];
    strncpy(method, line, method_len);
    method[method_len] = '\0';
    
    size_t path_len = path_end - method_end - 1;
    char path[path_len + 1];
    strncpy(path, method_end + 1, path_len);
    path[path_len] = '\0';
    
    size_t version_len = strlen(line) - (path_end - line) - 1;
    char version[version_len + 1];
    strncpy(version, path_end + 1, version_len);
    version[version_len] = '\0';
    
    while ((line = readline(&cur)) != NULL) {} // headers
    
    if (send(client_fd, &buf, CHUNK_SIZE, 0) < 0) {
      printf("send() failed with code %d", errno);
    }
    close(client_fd);
  }

  printf("Hello World\n");
  return 0;
}
