#include "protocol.h"
#include "transfer.h"
#include "utils.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_ARGS 64

static void print_usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [-h <host>] [-p <port>] [command [args...]]\n"
          "\n"
          "Interactive mode: run without command to enter shell\n"
          "Command mode:    run with command for single execution\n"
          "\n"
          "Commands:\n"
          "  upload <local> <remote> [more files...]  Upload file(s)\n"
          "  download <remote> <local>                 Download file\n"
          "  mkdir <dirname>                           Create directory\n"
          "  list [path]                               List directory\n"
          "  delete <path>                             Delete file or directory\n"
          "  rename <old> <new>                        Rename file or directory\n"
          "  help                                      Show this help\n"
          "  exit                                      Exit shell\n",
          prog);
}

static int connect_server(const char *host, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return -1;
  }
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
    perror("inet_pton");
    close(fd);
    return -1;
  }
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connect");
    close(fd);
    return -1;
  }
  return fd;
}

static int do_upload(int fd, const char *local, const char *remote) {
  return transfer_upload(fd, local, remote, NULL);
}

static int do_download(int fd, const char *remote, const char *local) {
  return transfer_download(fd, remote, local, NULL);
}

static int do_mkdir(int fd, const char *dirname) {
  size_t len = strlen(dirname) + 1;
  proto_message_t *req = create_msg(MSG_TYPE_REQUEST, CMD_MKDIR, len);
  if (!req)
    return -1;
  req->payload = malloc(len);
  if (!req->payload) {
    destroy_msg(req);
    free(req);
    return -1;
  }
  memcpy(req->payload, dirname, len);
  req->header.checksum = calculate_crc(req->payload, len);

  if (proto_send_msg(fd, req) < 0) {
    destroy_msg(req);
    free(req);
    return -1;
  }
  destroy_msg(req);
  free(req);

  proto_message_t resp = {0};
  if (proto_recv_msg(fd, &resp) < 0) {
    return -1;
  }
  printf("mkdir: %s\n", status_to_string(resp.header.status));
  int ret = (resp.header.status == STATUS_OK) ? 0 : -1;
  destroy_msg(&resp);
  return ret;
}

static int do_list(int fd, const char *path) {
  size_t len = 0;
  if (path) {
    len = strlen(path) + 1;
  }

  proto_message_t *req = create_msg(MSG_TYPE_REQUEST, CMD_LIST, len);
  if (!req)
    return -1;

  if (len > 0) {
    req->payload = malloc(len);
    if (!req->payload) {
      destroy_msg(req);
      free(req);
      return -1;
    }
    memcpy(req->payload, path, len);
    req->header.checksum = calculate_crc(req->payload, len);
  }

  if (proto_send_msg(fd, req) < 0) {
    destroy_msg(req);
    free(req);
    return -1;
  }
  destroy_msg(req);
  free(req);

  proto_message_t resp = {0};
  if (proto_recv_msg(fd, &resp) < 0) {
    printf("Failed to receive response\n");
    return -1;
  }
  if (resp.header.status != STATUS_OK) {
    printf("list failed: %s\n", status_to_string(resp.header.status));
    destroy_msg(&resp);
    return -1;
  }
  if (resp.payload && resp.header.payload_len > 0) {
    printf("%.*s", (int)resp.header.payload_len, (char *)resp.payload);
  } else {
    printf("(empty)\n");
  }
  destroy_msg(&resp);
  return 0;
}

static int do_delete(int fd, const char *path) {
  size_t len = strlen(path) + 1;
  proto_message_t *req = create_msg(MSG_TYPE_REQUEST, CMD_DELETE, len);
  if (!req)
    return -1;
  req->payload = malloc(len);
  if (!req->payload) {
    destroy_msg(req);
    free(req);
    return -1;
  }
  memcpy(req->payload, path, len);
  req->header.checksum = calculate_crc(req->payload, len);

  if (proto_send_msg(fd, req) < 0) {
    destroy_msg(req);
    free(req);
    return -1;
  }
  destroy_msg(req);
  free(req);

  proto_message_t resp = {0};
  if (proto_recv_msg(fd, &resp) < 0) {
    return -1;
  }
  printf("delete: %s\n", status_to_string(resp.header.status));
  int ret = (resp.header.status == STATUS_OK) ? 0 : -1;
  destroy_msg(&resp);
  return ret;
}

static int do_rename(int fd, const char *old_path, const char *new_path) {
  size_t old_len = strlen(old_path) + 1;
  size_t new_len = strlen(new_path) + 1;
  size_t total_len = old_len + new_len;

  proto_message_t *req = create_msg(MSG_TYPE_REQUEST, CMD_RENAME, total_len);
  if (!req)
    return -1;

  req->payload = malloc(total_len);
  if (!req->payload) {
    destroy_msg(req);
    free(req);
    return -1;
  }
  memcpy(req->payload, old_path, old_len);
  memcpy(req->payload + old_len, new_path, new_len);
  req->header.checksum = calculate_crc(req->payload, total_len);

  if (proto_send_msg(fd, req) < 0) {
    destroy_msg(req);
    free(req);
    return -1;
  }
  destroy_msg(req);
  free(req);

  proto_message_t resp = {0};
  if (proto_recv_msg(fd, &resp) < 0) {
    return -1;
  }
  printf("rename: %s\n", status_to_string(resp.header.status));
  int ret = (resp.header.status == STATUS_OK) ? 0 : -1;
  destroy_msg(&resp);
  return ret;
}

static char **parse_args(const char *line, int *argc) {
  static char *args[MAX_ARGS];
  static char line_copy[4096];

  *argc = 0;
  strncpy(line_copy, line, sizeof(line_copy) - 1);
  line_copy[sizeof(line_copy) - 1] = '\0';

  char *token = strtok(line_copy, " \t\n");
  while (token && *argc < MAX_ARGS - 1) {
    args[(*argc)++] = token;
    token = strtok(NULL, " \t\n");
  }
  args[*argc] = NULL;

  return args;
}

static void print_shell_help(void) {
  printf("Commands:\n"
         "  upload <local> <remote> [local2 remote2 ...]  Upload file(s)\n"
         "  download <remote> <local>                     Download file\n"
         "  mkdir <dirname>                               Create directory\n"
         "  list [path]                                   List directory\n"
         "  delete <path>                                 Delete file or directory\n"
         "  rename <old> <new>                            Rename file or directory\n"
         "  help                                          Show this help\n"
         "  exit                                          Exit shell\n");
}

static int execute_command(int fd, int argc, char **argv) {
  if (argc < 1)
    return 0;

  const char *cmd = argv[0];

  if (strcmp(cmd, "upload") == 0) {
    if (argc < 3) {
      fprintf(stderr, "upload: need <local> <remote> [more pairs...]\n");
      return -1;
    }
    int ret = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
      if (do_upload(fd, argv[i], argv[i + 1]) < 0) {
        ret = -1;
      }
    }
    return ret;
  } else if (strcmp(cmd, "download") == 0) {
    if (argc < 3) {
      fprintf(stderr, "download: need <remote_path> <local_file>\n");
      return -1;
    }
    return do_download(fd, argv[1], argv[2]);
  } else if (strcmp(cmd, "mkdir") == 0) {
    if (argc < 2) {
      fprintf(stderr, "mkdir: need <dirname>\n");
      return -1;
    }
    return do_mkdir(fd, argv[1]);
  } else if (strcmp(cmd, "list") == 0) {
    const char *path = (argc > 1) ? argv[1] : NULL;
    return do_list(fd, path);
  } else if (strcmp(cmd, "delete") == 0) {
    if (argc < 2) {
      fprintf(stderr, "delete: need <path>\n");
      return -1;
    }
    return do_delete(fd, argv[1]);
  } else if (strcmp(cmd, "rename") == 0) {
    if (argc < 3) {
      fprintf(stderr, "rename: need <old_path> <new_path>\n");
      return -1;
    }
    return do_rename(fd, argv[1], argv[2]);
  } else if (strcmp(cmd, "help") == 0) {
    print_shell_help();
    return 0;
  } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
    return -999;
  } else {
    fprintf(stderr, "Unknown command: %s (type 'help' for usage)\n", cmd);
    return -1;
  }
}

static void interactive_shell(int fd) {
  char line[4096];

  printf("Connected to server. Type 'help' for commands, 'exit' to quit.\n");

  while (1) {
    printf("fshell> ");
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
      break;
    }

    if (line[0] == '\n' || line[0] == '\0') {
      continue;
    }

    int argc;
    char **argv = parse_args(line, &argc);

    if (argc == 0) {
      continue;
    }

    int ret = execute_command(fd, argc, argv);
    if (ret == -999) {
      break;
    }
  }

  printf("Goodbye.\n");
}

int main(int argc, char *argv[]) {
  proto_init();

  const char *host = "127.0.0.1";
  int port = 8888;
  int opt;

  while ((opt = getopt(argc, argv, "h:p:")) != -1) {
    switch (opt) {
    case 'h':
      host = optarg;
      break;
    case 'p':
      port = atoi(optarg);
      break;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  int fd = connect_server(host, port);
  if (fd < 0) {
    return 1;
  }

  if (optind < argc) {
    int ret = execute_command(fd, argc - optind, argv + optind);
    close(fd);
    return (ret < 0 && ret != -999) ? 1 : 0;
  } else {
    interactive_shell(fd);
    close(fd);
    return 0;
  }
}
