#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
// #include <netinet/in.h>
#include <arpa/inet.h>

#define CHUNK_SIZE 16 * 1024 // 16 KB

int main() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = inet_addr("127.0.0.1"),
    .sin_port = htons(6000)
  };

  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    printf("bind() failed with code %d", errno);
    exit(EXIT_FAILURE);
  }

  if (listen(sock, 10) < 0) {
    printf("listen() failed with code %d", errno);
    exit(EXIT_FAILURE);
  }
  
  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd = accept(sock, (struct sockaddr*)&client_addr, &client_addr_len);

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
    
    if (send(client_fd, &buf, CHUNK_SIZE, 0) < 0) {
      printf("send() failed with code %d", errno);
    }
    close(client_fd);
  }

  printf("Hello World\n");
  return 0;
}
