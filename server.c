#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 42069
#define HOST_IP "127.0.0.1"

int main(int argc, char *argv[]) {
  int s = socket(PF_INET, SOCK_STREAM, 0);
  if (s == -1) {
    fprintf(stderr, "ERROR: could not create socket: %s\n", strerror(errno));
    return 1;
  }

  struct sockaddr_in server_addr = {0};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(PORT);
  server_addr.sin_addr.s_addr = inet_addr(HOST_IP);

  if (bind(s, (void *)&server_addr, sizeof(server_addr)) < 0) {
    fprintf(stderr, "ERROR: could not bind server socket: %s\n",
            strerror(errno));
    return 1;
  }

  if (listen(s, 1) < 0) {
    fprintf(stderr, "ERROR: could not start listening on socket: %s\n",
            strerror(errno));
    return 1;
  }

  printf("INFO: listening to %s:%d\n", HOST_IP, PORT);

  while (1) {
    struct sockaddr_in client_addr = {0};
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd = accept(s, (void *)&client_addr, &client_addr_len);

    if (client_fd < 0) {
      fprintf(stderr, "ERROR: could not accept connection from client: %s\n",
              strerror(errno));
      return 1;
    }

    char buf[1024];
    int n = recv(client_fd, buf, 1024, MSG_PEEK);
    if (n < 0) {
      fprintf(stderr, "ERROR: could not receive data from socket: %s\n",
              strerror(errno));
      return 1;
    }
    if (n == 0) {
      fprintf(stderr, "ERROR: connection closed from socket: %s\n",
              strerror(errno));
      return 1;
    }

    printf("%s\n", buf);
  }

  return 0;
}
