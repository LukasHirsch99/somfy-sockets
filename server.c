#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define PORT 42069
#define HOST_IP "127.0.0.1"

int main() {

  // Create a socket
  int server_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    fprintf(stderr, "ERROR: could not create socket: %s\n", strerror(errno));
    return 1;
  }

  int on = 1;
  // Allow socket descriptor to be reuseable
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  if (ioctl(server_fd, FIONBIO, (char *)&on) < 0) {
    perror("ioctl() failed");
    close(server_fd);
    return -1;
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

  // Prepare the socket for incoming connections, 1 connection
  if (listen(server_fd, 1) < 0) {
    fprintf(stderr, "ERROR: could not start listening on socket: %s\n",
            strerror(errno));
    return 1;
  }

  printf("INFO: listening to %s:%d\n", HOST_IP, PORT);

  struct pollfd read_fds[20];
  memset(read_fds, 0, sizeof(read_fds));
  read_fds[0].fd = server_fd;
  read_fds[0].events = POLLIN;

  unsigned int nfds = 1;
  char buffer[128];
  short close_conn = 0;
  short end_server = 0;

  while (!end_server) {
    // Call poll and wait
    int rc = poll(read_fds, nfds, 3 * 60 * 1000);

    // Poll failed
    if (rc < 0) {
      fprintf(stderr, "ERROR: could not poll: %s\n", strerror(errno));
      return 1;
    }

    // Poll timed out
    if (rc == 0) {
      fprintf(stderr, "ERROR: poll() timed out\n");
      return -1;
    }

    int cur_size = nfds;
    for (int i = 0; i < cur_size; i++) {
      // No data to available
      if (read_fds[i].revents == 0)
        continue;

      if (read_fds[i].revents != POLLIN) {
        if (read_fds[i].revents & POLLHUP) {
          printf("Connection closed: %d\n", read_fds[i].fd);
          close(read_fds[i].fd);
          read_fds[i].fd = -1;
          // compress_array = TRUE;
          continue;
        }
        fprintf(stderr, "ERROR: unexpected revents: 0x%x\n",
                read_fds[i].revents);
        end_server = 1;
        break;
      }

      // Server descriptor is readable
      // Accept all new connection
      if (read_fds[i].fd == server_fd) {
        int new_fd = -1;
        do {
          new_fd = accept(server_fd, NULL, NULL);
          if (new_fd < 0) {
            if (errno != EWOULDBLOCK) {
              fprintf(stderr, "ERROR: accept(): %s\n", strerror(errno));
              return 1;
            }
            // We accepted all incoming connections
            break;
          }

          // add new connection to pollfd structure
          printf("New connection: %d\n", new_fd);
          read_fds[nfds].fd = new_fd;
          read_fds[nfds].events = POLLIN;
          nfds++;
        } while (new_fd != -1);
      }

      // Receive incoming data from socket
      else {
        printf("Descriptor %d is readable\n", read_fds[i].fd);

        do {
          rc = recv(read_fds[i].fd, buffer, sizeof(buffer), 0);
          if (rc < 0) {
            if (errno != EWOULDBLOCK) {
              fprintf(stderr, "ERROR: recv() failed: %s\n", strerror(errno));
              close_conn = 1;
              return 1;
            }
            break;
          }

          // Check to see if the connection has been closed by the client
          if (rc == 0) {
            printf("Connection closed\n");
            close_conn = 1;
            break;
          }

          // Data was received
          int len = rc;
          buffer[len] = 0;
          printf("%d bytes received\n", len);
          printf("Received: \"%s\"\n", buffer);

          // Echo the data back to the client
          if (send(read_fds[i].fd, buffer, len, 0) < 0) {
            fprintf(stderr, "ERROR: recv() failed: %s\n", strerror(errno));
            close_conn = 1;
            break;
          }
        } while (1);

        if (close_conn) {
          close(read_fds[i].fd);
          read_fds[i].fd = -1;
          // compress_array = TRUE;
        }
      }
    }
  }

  for (size_t i = 0; i < nfds; i++) {
    if (read_fds[i].fd >= 0)
      close(read_fds[i].fd);
  }

  return 0;
}
