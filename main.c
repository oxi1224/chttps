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

void flog(enum LOG_LEVEL level, const char* fmt, ...) {
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

typedef struct {
  const char *name;
  const char *value;
} http_header;

typedef struct {
  http_header *headers;
  size_t count;
  size_t capacity;
} http_header_list;

typedef struct {
  const char *path;
  const char *method;
  const char *version;
  http_header_list header_list;
} http_request;

void free_http_request(http_request r) {
  free((void*)r.method);
  free((void*)r.path);
  free((void*)r.version);
  for (size_t i = 0; i < r.header_list.count; i++) {
    free((void*)r.header_list.headers[i].name);
    free((void*)r.header_list.headers[i].value);
  }
}

int main() {
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = inet_addr("127.0.0.1"),
    .sin_port = htons(9001)
  };

  if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    flog(ERROR, "bind() failed with code %d", errno);
    exit(EXIT_FAILURE);
  }

  if (listen(sock_fd, 10) < 0) {
    flog(ERROR, "listen() failed with code %d", errno);
    exit(EXIT_FAILURE);
  }
  
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

    http_header_list header_list = {
      .capacity = 8,
      .count = 0,
      .headers = malloc(sizeof(http_header) * 8)
    };
  
    int parsing_err = 0;
    while ((line = readline(&cur)) != NULL && strcmp(line, "") != 0) {
      char* delim = strstr(line, ":");
      if (delim == NULL) {
        flog(WARN, "Expected header line does not have delimeter (:)");
        close(client_fd);
        parsing_err = 1;
        break;
      }
      
      header_list.headers[header_list.count++] = (http_header){
        .name = substr(line, 0, delim - line),
        .value = substr(line, delim - line + 2, strlen(line))
      };
      
      if (header_list.count == header_list.capacity) {
        header_list.capacity *= 2;
        header_list.headers = realloc(
          header_list.headers,
          header_list.capacity * sizeof(http_header)
        );
      }
    }
    if (parsing_err) continue;
    request.header_list = header_list;
    
    printf("-------------"); 
    flog(DEBUG, "Method: %s | Path: %s | Version: %s", request.method, request.path, request.version);
    flog(DEBUG, "Headers(%d)", request.header_list.count);
    for (size_t i = 0; i < request.header_list.count; i++) {
      flog(
        DEBUG,
        "%s: %s",
        request.header_list.headers[i].name,
        request.header_list.headers[i].value
      );
    }
    printf("-------------"); 

    if (send(client_fd, &buf, CHUNK_SIZE, 0) < 0) {
      printf("send() failed with code %d", errno);
    }
    free_http_request(request);
    close(client_fd);
  }

  return 0;
}
