#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
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
    int rec_bytes = recv(client_fd, &buf, CHUNK_SIZE, 0);
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
    
    
    while ((line = readline(&cur)) != NULL) {} // headers
    
    // char* ocur = strstr((const char*)&buf, "\r\n\r\n");
    // while (ocur != NULL) {
    //
    //   ocur = strstr((const char*)&buf, "\r\n\r\n");
    // }
    //
    // for (int i = 0; i < rec_bytes; i++) {
    //
    // }

    
    if (send(client_fd, &buf, CHUNK_SIZE, 0) < 0) {
      printf("send() failed with code %d", errno);
    }
    close(client_fd);
  }

  printf("Hello World\n");
  return 0;
}
