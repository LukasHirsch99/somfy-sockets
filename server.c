#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define PORT 42069
#define HOST_IP "127.0.0.1"

typedef struct {
  int server_fd;
  struct pollfd *read_fds;
  size_t nfds;
  short close_server;
} TCPServer;

TCPServer server_from_fds(int server_fd, struct pollfd *read_fds) {
  read_fds[0].fd = server_fd;
  read_fds[0].events = POLLIN;
  return (TCPServer){
      .server_fd = server_fd,
      .nfds = 1,
      .read_fds = read_fds,
      .close_server = 0,
  };
}

int poll_server(TCPServer server) {
  int rc = poll(server.read_fds, server.nfds, 3 * 60 * 1000);

  // Poll failed
  if (rc < 0) {
    fprintf(stderr, "ERROR: could not poll: %s\n", strerror(errno));
    return -1;
  }

  // Poll timed out
  if (rc == 0) {
    fprintf(stderr, "ERROR: poll() timed out\n");
    return -1;
  }

  return rc;
}

int accept_connections(TCPServer *server) {
  int new_fd = -1;
  do {
    new_fd = accept(server->server_fd, NULL, NULL);
    if (new_fd < 0) {
      // We accepted all incoming connections
      if (errno == EWOULDBLOCK)
        return 0;

      fprintf(stderr, "ERROR: accept(): %s\n", strerror(errno));
      server->close_server = 1;
      return -1;
    }

    // add new connection to pollfd structure
    printf("New connection: %d\n", new_fd);
    server->read_fds[server->nfds].fd = new_fd;
    server->read_fds[server->nfds].events = POLLIN;
    server->nfds++;
  } while (new_fd != -1);
  return 0;
}

int receive_data(TCPServer *server, int r_idx, char *buffer, size_t buf_len) {
  int rc;
  int len = 0;
  do {
    rc = recv(server->read_fds[r_idx].fd, buffer, buf_len, 0);
    if (rc < 0) {
      // Everything read
      if (errno == EWOULDBLOCK) {
        buffer[len] = 0;
        return len;
      }

      fprintf(stderr, "ERROR: recv() failed: %s\n", strerror(errno));
      server->close_server = 1;
      return -1;
    }

    // Check to see if the connection has been closed by the client
    if (rc == 0) {
      printf("Connection closed\n");
      return 0;
    }

    // Data was received
    len += rc;
    printf("%d bytes received\n", len);
    printf("Received: \"%s\"\n", buffer);

  } while (1);
}

int main() {
  int server_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    fprintf(stderr, "ERROR: could not create server socket: %s\n",
            strerror(errno));
    return 1;
  }

  int on = 1;
  // Allow socket descriptor to be reuseable
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    fprintf(stderr, "ERROR: could not configure server socket: %s\n",
            strerror(errno));
    return 1;
  }

  // Set non-blocking io
  if (ioctl(server_fd, FIONBIO, (char *)&on) < 0) {
    fprintf(stderr,
            "ERROR: could not configure server socket to non-blocking: %s\n",
            strerror(errno));
    close(server_fd);
    return 1;
  }

  // create a sockaddr, the address clients connect to
  struct sockaddr_in server_addr = {0};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(PORT);
  server_addr.sin_addr.s_addr = inet_addr(HOST_IP);

  // Bind the socket to the address
  if (bind(server_fd, (void *)&server_addr, sizeof(server_addr)) < 0) {
    fprintf(stderr, "ERROR: could not bind server socket: %s\n",
            strerror(errno));
    return 1;
  }

  if (listen(server_fd, 1) < 0) {
    fprintf(stderr, "ERROR: could not listen to server socket: %s\n",
            strerror(errno));
    return 1;
  }

  printf("INFO: listening to %s:%d\n", HOST_IP, PORT);

  struct pollfd read_fds[20];
  memset(read_fds, 0, sizeof(read_fds));
  TCPServer server = server_from_fds(server_fd, read_fds);

  char buffer[128];

  // Event loop
  while (!server.close_server) {
    // Call poll and wait
    if (poll_server(server) < 0) {
      server.close_server = 1;
      return 1;
    }

    size_t cur_size = server.nfds;
    // For each socket
    for (size_t i = 0; i < cur_size; i++) {
      struct pollfd r_fd = server.read_fds[i];

      // No data available
      if (r_fd.revents == 0)
        continue;

      if (r_fd.revents != POLLIN) {
        if (r_fd.revents & POLLHUP) {
          printf("Connection closed: %d\n", r_fd.fd);
          close(r_fd.fd);
          r_fd.fd = -1;
          // compress_array = TRUE;
          continue;
        }
        fprintf(stderr, "ERROR: unexpected revents: 0x%x\n", r_fd.revents);
        server.close_server = 1;
        break;
      }

      // Server descriptor is readable
      // Accept all new connection
      if (r_fd.fd == server_fd) {
        if (accept_connections(&server) < 0) {
          break;
        }
      }

      // Receive incoming data from socket
      else {
        printf("Descriptor %d is readable\n", r_fd.fd);
        int len = receive_data(&server, i, buffer, sizeof(buffer));

        // Echo the data back to the client
        if (send(r_fd.fd, buffer, len, 0) < 0) {
          fprintf(stderr, "ERROR: recv() failed: %s\n", strerror(errno));
          close(r_fd.fd);
          r_fd.fd = -1;
          break;
        }

      }
    }
  }
  printf("Server shutting down...\n");

  for (size_t i = 0; i < server.nfds; i++) {
    if (read_fds[i].fd >= 0)
      close(read_fds[i].fd);
  }

  return 0;
}
