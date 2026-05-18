#include "server.h"
#include "protocol.h"
#include "utils.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

_Static_assert(sizeof(proto_header_t) == 15, "proto_header_t size mismatch");

static volatile int running = 1;
static int listen_fd = -1;
static const char *root_dir = DEFAULT_ROOT;

static void sigint_handler(int sig) {
  (void)sig;
  running = 0;
  if (listen_fd >= 0) {
    close(listen_fd);
  }
}

static void print_usage(const char *prog) {
  fprintf(stderr, "Usage: %s [-p port] [-r root_dir]\n", prog);
  fprintf(stderr, "  -p <port>     Listen port (default: %d)\n", DEFAULT_PORT);
  fprintf(stderr, "  -r <root>      Storage root directory (default: %s)\n",
          DEFAULT_ROOT);
}

void handle_mkdir(int sockfd, proto_message_t *msg) {
  char *dirpath = (char *)msg->payload;
  proto_message_t *resp;

  if (path_validate(dirpath) < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_INVALID_PATH;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  char resolved[MAX_PATH_LEN];
  if (path_resolve(root_dir, dirpath, resolved, MAX_PATH_LEN) < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_INVALID_PATH;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  if (mkdir(resolved, 0755) < 0) {
    if (errno == EEXIST) {
      resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
      resp->header.status = STATUS_EXISTS;
      proto_send_msg(sockfd, resp);
      destroy_msg(resp);
      free(resp);
      return;
    }
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_IO_ERROR;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
  resp->header.status = STATUS_OK;
  proto_send_msg(sockfd, resp);
  destroy_msg(resp);
  free(resp);
}

void handle_upload(int sockfd, proto_message_t *req) {
  proto_message_t *resp;

  if (req->header.payload_len < MAX_FILE_LEN + 8) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_INVALID_PATH;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  char filename[MAX_FILE_LEN];
  memcpy(filename, req->payload, MAX_FILE_LEN);
  filename[MAX_FILE_LEN - 1] = '\0';

  uint64_t file_size = le_to_uint64(req->payload + MAX_FILE_LEN);

  if (path_validate(filename) < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_INVALID_PATH;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  char resolved[MAX_PATH_LEN];
  if (path_resolve(root_dir, filename, resolved, sizeof(resolved)) < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_INVALID_PATH;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  int fd = open(resolved, O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (fd < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = (errno == EEXIST) ? STATUS_EXISTS : STATUS_IO_ERROR;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
  resp->header.status = STATUS_OK;
  proto_send_msg(sockfd, resp);
  destroy_msg(resp);
  free(resp);

  uint64_t received = 0;
  int upload_ok = 1;

  while (received < file_size) {
    proto_message_t data_msg = {0};
    if (proto_recv_msg(sockfd, &data_msg) < 0) {
      upload_ok = 0;
      break;
    }
    if (data_msg.header.type != MSG_TYPE_DATA) {
      destroy_msg(&data_msg);
      upload_ok = 0;
      break;
    }

    size_t chunk = data_msg.header.payload_len;
    if (received + chunk > file_size) {
      destroy_msg(&data_msg);
      upload_ok = 0;
      break;
    }

    uint32_t crc = calculate_crc(data_msg.payload, chunk);
    if (crc != data_msg.header.checksum) {
      destroy_msg(&data_msg);
      upload_ok = 0;
      break;
    }

    ssize_t n = write(fd, data_msg.payload, chunk);
    if (n != (ssize_t)chunk) {
      destroy_msg(&data_msg);
      upload_ok = 0;
      break;
    }

    destroy_msg(&data_msg);
    received += chunk;
  }

  close(fd);
  if (!upload_ok || received != file_size) {
    unlink(resolved);
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_IO_ERROR;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
  resp->header.status = STATUS_OK;
  proto_send_msg(sockfd, resp);
  destroy_msg(resp);
  free(resp);
}

void handle_download(int sockfd, proto_message_t *req) {
  char *remote_path = (char *)req->payload;
  proto_message_t *resp;

  if (path_validate(remote_path) < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_INVALID_PATH;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  char resolved[MAX_PATH_LEN];
  if (path_resolve(root_dir, remote_path, resolved, sizeof(resolved)) < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_INVALID_PATH;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  int fd = open(resolved, O_RDONLY);
  if (fd < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status =
        (errno == ENOENT) ? STATUS_FILE_NOT_FOUND : STATUS_IO_ERROR;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  struct stat st;
  if (fstat(fd, &st) < 0) {
    close(fd);
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_IO_ERROR;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }
  uint64_t file_size = st.st_size;

  uint8_t size_buf[8];
  uint64_to_le(file_size, size_buf);

  resp = create_msg(MSG_TYPE_RESPONSE, 0, 8);
  resp->payload = malloc(8);
  if (!resp->payload) {
    destroy_msg(resp);
    free(resp);
    close(fd);
    return;
  }
  memcpy(resp->payload, size_buf, 8);
  resp->header.status = STATUS_OK;
  resp->header.checksum = calculate_crc(resp->payload, 8);
  proto_send_msg(sockfd, resp);
  destroy_msg(resp);
  free(resp);

  uint8_t buf[CHUNK_SIZE];
  uint64_t sent = 0;
  int error = 0;

  while (sent < file_size) {
    size_t to_read = CHUNK_SIZE;
    if (sent + to_read > file_size) {
      to_read = file_size - sent;
    }
    ssize_t n = read(fd, buf, to_read);
    if (n <= 0) {
      error = 1;
      break;
    }

    proto_message_t *data_msg = create_msg(MSG_TYPE_DATA, 0, n);
    data_msg->payload = malloc(n);
    if (!data_msg->payload) {
      destroy_msg(data_msg);
      free(data_msg);
      error = 1;
      break;
    }
    memcpy(data_msg->payload, buf, n);
    data_msg->header.checksum = calculate_crc(data_msg->payload, n);

    if (proto_send_msg(sockfd, data_msg) < 0) {
      destroy_msg(data_msg);
      free(data_msg);
      error = 1;
      break;
    }
    destroy_msg(data_msg);
    free(data_msg);
    sent += n;
  }

  close(fd);
  if (error) {
    return;
  }

  proto_message_t ack = {0};
  if (proto_recv_msg(sockfd, &ack) < 0) {
    return;
  }
  destroy_msg(&ack);
}

void handle_delete(int sockfd, proto_message_t *req) {
  char *path = (char *)req->payload;
  proto_message_t *resp;

  if (path_validate(path) < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_INVALID_PATH;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  char resolved[MAX_PATH_LEN];
  if (path_resolve(root_dir, path, resolved, sizeof(resolved)) < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_INVALID_PATH;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  struct stat st;
  if (stat(resolved, &st) < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_FILE_NOT_FOUND;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  int ret;
  if (S_ISREG(st.st_mode)) {
    ret = unlink(resolved);
  } else if (S_ISDIR(st.st_mode)) {
    ret = rmdir(resolved);
  } else {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_IO_ERROR;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
  if (ret < 0) {
    resp->header.status = STATUS_IO_ERROR;
  } else {
    resp->header.status = STATUS_OK;
  }
  proto_send_msg(sockfd, resp);
  destroy_msg(resp);
  free(resp);
}

static void format_size(uint64_t size, char *buf, size_t buf_len) {
  if (size >= 1024 * 1024 * 1024) {
    snprintf(buf, buf_len, "%.1fG", (double)size / (1024 * 1024 * 1024));
  } else if (size >= 1024 * 1024) {
    snprintf(buf, buf_len, "%.1fM", (double)size / (1024 * 1024));
  } else if (size >= 1024) {
    snprintf(buf, buf_len, "%.1fK", (double)size / 1024);
  } else {
    snprintf(buf, buf_len, "%luB", (unsigned long)size);
  }
}

static void format_time(time_t t, char *buf, size_t buf_len) {
  struct tm tm_info;
  localtime_r(&t, &tm_info);
  strftime(buf, buf_len, "%Y-%m-%d %H:%M", &tm_info);
}

void handle_list(int sockfd, proto_message_t *req) {
  proto_message_t *resp;
  char *subpath = NULL;

  if (req && req->header.payload_len > 0 && req->payload) {
    subpath = (char *)req->payload;
    if (path_validate(subpath) < 0) {
      resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
      resp->header.status = STATUS_INVALID_PATH;
      proto_send_msg(sockfd, resp);
      destroy_msg(resp);
      free(resp);
      return;
    }
  }

  char list_path[MAX_PATH_LEN];
  if (subpath) {
    if (path_resolve(root_dir, subpath, list_path, sizeof(list_path)) < 0) {
      resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
      resp->header.status = STATUS_INVALID_PATH;
      proto_send_msg(sockfd, resp);
      destroy_msg(resp);
      free(resp);
      return;
    }
  } else {
    snprintf(list_path, sizeof(list_path), "%s", root_dir);
  }

  DIR *dir = opendir(list_path);
  if (dir == NULL) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = (errno == ENOENT) ? STATUS_FILE_NOT_FOUND : STATUS_IO_ERROR;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  char buffer[8192] = {0};
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    char entry_path[MAX_PATH_LEN + 256];
    int n = snprintf(entry_path, sizeof(entry_path), "%s/%s", list_path, entry->d_name);
    if (n < 0 || n >= (int)sizeof(entry_path)) {
      continue;
    }

    struct stat st;
    if (stat(entry_path, &st) < 0) {
      continue;
    }

    char type_char = S_ISDIR(st.st_mode) ? 'D' : 'F';
    char size_str[16];
    char time_str[20];

    if (S_ISDIR(st.st_mode)) {
      strcpy(size_str, "-");
    } else {
      format_size(st.st_size, size_str, sizeof(size_str));
    }
    format_time(st.st_mtime, time_str, sizeof(time_str));

    char line[512];
    snprintf(line, sizeof(line), "%c %-8s %s %s\n", type_char, size_str, time_str, entry->d_name);
    if (strlen(buffer) + strlen(line) < sizeof(buffer) - 1) {
      strcat(buffer, line);
    }
  }
  closedir(dir);

  size_t payload_len = strlen(buffer);
  resp = create_msg(MSG_TYPE_RESPONSE, 0, payload_len);
  if (payload_len > 0) {
    resp->payload = (uint8_t *)strdup(buffer);
    resp->header.checksum = calculate_crc(resp->payload, payload_len);
  }
  resp->header.status = STATUS_OK;
  proto_send_msg(sockfd, resp);
  destroy_msg(resp);
  free(resp);
}

void handle_rename(int sockfd, proto_message_t *req) {
  proto_message_t *resp;

  if (!req || req->header.payload_len < 2) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_INVALID_PATH;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  char *payload = (char *)req->payload;
  char *old_path = payload;
  char *new_path = payload + strlen(old_path) + 1;

  if (path_validate(old_path) < 0 || path_validate(new_path) < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_INVALID_PATH;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  char old_resolved[MAX_PATH_LEN];
  char new_resolved[MAX_PATH_LEN];

  if (path_resolve(root_dir, old_path, old_resolved, sizeof(old_resolved)) < 0 ||
      path_resolve(root_dir, new_path, new_resolved, sizeof(new_resolved)) < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    resp->header.status = STATUS_INVALID_PATH;
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  if (rename(old_resolved, new_resolved) < 0) {
    resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
    if (errno == ENOENT) {
      resp->header.status = STATUS_FILE_NOT_FOUND;
    } else if (errno == EEXIST) {
      resp->header.status = STATUS_EXISTS;
    } else {
      resp->header.status = STATUS_IO_ERROR;
    }
    proto_send_msg(sockfd, resp);
    destroy_msg(resp);
    free(resp);
    return;
  }

  resp = create_msg(MSG_TYPE_RESPONSE, 0, 0);
  resp->header.status = STATUS_OK;
  proto_send_msg(sockfd, resp);
  destroy_msg(resp);
  free(resp);
}

void *client_handler(void *arg) {
  int client_fd = *(int *)arg;
  free(arg);

  struct timeval timeout;
  timeout.tv_sec = SOCKET_TIMEOUT_SEC;
  timeout.tv_usec = 0;
  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  while (running) {
    proto_message_t *msg = malloc(sizeof(proto_message_t));
    if (!msg) {
      break;
    }
    if (proto_recv_msg(client_fd, msg) < 0) {
      free(msg);
      break;
    }
    switch (msg->header.command) {
    case CMD_MKDIR:
      handle_mkdir(client_fd, msg);
      break;
    case CMD_RENAME:
      handle_rename(client_fd, msg);
      break;
    case CMD_UPLOAD:
      handle_upload(client_fd, msg);
      break;
    case CMD_LIST:
      handle_list(client_fd, msg);
      break;
    case CMD_DOWNLOAD:
      handle_download(client_fd, msg);
      break;
    case CMD_DELETE:
      handle_delete(client_fd, msg);
      break;
    default:
      break;
    }
    destroy_msg(msg);
    free(msg);
  }
  close(client_fd);
  return NULL;
}

int main(int argc, char *argv[]) {
  int port = DEFAULT_PORT;
  int opt;

  while ((opt = getopt(argc, argv, "p:r:h")) != -1) {
    switch (opt) {
    case 'p':
      port = atoi(optarg);
      if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", optarg);
        return 1;
      }
      break;
    case 'r':
      root_dir = optarg;
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, sigint_handler);

  if (mkdir(root_dir, 0755) < 0 && errno != EEXIST) {
    perror("mkdir root_dir");
    return 1;
  }

  proto_init();

  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("socket");
    return 1;
  }

  int reuse = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(listen_fd);
    return 1;
  }

  if (listen(listen_fd, 128) < 0) {
    perror("listen");
    close(listen_fd);
    return 1;
  }

  printf("Server listening on port %d, root: %s\n", port, root_dir);
  fflush(stdout);

  while (running) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int *fd_ptr = malloc(sizeof(int));
    if (!fd_ptr) {
      continue;
    }

    *fd_ptr = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (*fd_ptr < 0) {
      free(fd_ptr);
      if (!running) {
        break;
      }
      continue;
    }

    pthread_t tid;
    if (pthread_create(&tid, NULL, client_handler, fd_ptr) != 0) {
      close(*fd_ptr);
      free(fd_ptr);
      continue;
    }
    pthread_detach(tid);
  }

  printf("\nServer shutting down...\n");
  close(listen_fd);
  return 0;
}
