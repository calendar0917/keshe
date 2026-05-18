#include "transfer.h"
#include "utils.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void progress_init(progress_t *p, uint64_t total, const char *label) {
  p->total = total;
  p->current = 0;
  p->start_time = (double)time(NULL);
  p->last_percent = -1;
  p->label = label;
}

int progress_update(progress_t *p, uint64_t current) {
  p->current = current;
  int percent = (p->total > 0) ? (int)(current * 100 / p->total) : 0;
  if (percent == p->last_percent) {
    return 0;
  }
  p->last_percent = percent;

  double elapsed = (double)time(NULL) - p->start_time;
  double speed = (elapsed > 0) ? current / elapsed : 0;

  char speed_str[32];
  if (speed > 1024 * 1024) {
    snprintf(speed_str, sizeof(speed_str), "%.2f MB/s", speed / (1024 * 1024));
  } else if (speed > 1024) {
    snprintf(speed_str, sizeof(speed_str), "%.2f KB/s", speed / 1024);
  } else {
    snprintf(speed_str, sizeof(speed_str), "%.0f B/s", speed);
  }

  int bar_len = 20;
  int filled = (percent * bar_len) / 100;
  printf("\r[");
  for (int i = 0; i < filled; i++)
    putchar('#');
  for (int i = filled; i < bar_len; i++)
    putchar('.');
  printf("] %3d%% %s %s", percent, speed_str, p->label);
  fflush(stdout);
  return 1;
}

void progress_finish(progress_t *p) {
  (void)p;
  printf("\n");
  fflush(stdout);
}

int transfer_upload(int sockfd, const char *local_path, const char *remote_name,
                    logger_t *logger) {
  int fd = open(local_path, O_RDONLY);
  if (fd < 0) {
    if (logger)
      log_msg(logger, 1, "upload: cannot open %s", local_path);
    return -1;
  }

  struct stat st;
  if (fstat(fd, &st) < 0) {
    close(fd);
    return -1;
  }
  uint64_t file_size = st.st_size;

  // 构造 UPLOAD_REQ payload: filename (255字节) + file_size (8字节小端)
  size_t payload_len = MAX_FILE_LEN + 8;
  proto_message_t *req = create_msg(MSG_TYPE_REQUEST, CMD_UPLOAD, payload_len);
  if (!req) {
    close(fd);
    return -1;
  }
  req->payload = malloc(payload_len);
  if (!req->payload) {
    destroy_msg(req);
    close(fd);
    return -1;
  }
  memset(req->payload, 0, MAX_FILE_LEN);
  size_t name_len = strlen(remote_name);
  if (name_len >= MAX_FILE_LEN)
    name_len = MAX_FILE_LEN - 1;
  memcpy(req->payload, remote_name, name_len);
  uint64_to_le(file_size, req->payload + MAX_FILE_LEN);

  if (proto_send_msg(sockfd, req) < 0) {
    destroy_msg(req);
    close(fd);
    return -1;
  }
  destroy_msg(req);

  // 收 UPLOAD_RESP
  proto_message_t resp = {0};
  if (proto_recv_msg(sockfd, &resp) < 0) {
    close(fd);
    return -1;
  }
  if (resp.header.status != STATUS_OK) {
    if (logger)
      log_msg(logger, 1, "upload: server rejected: %s",
              status_to_string(resp.header.status));
    destroy_msg(&resp);
    close(fd);
    return -1;
  }
  destroy_msg(&resp);

  progress_t prog;
  progress_init(&prog, file_size, "Uploading");

  uint8_t buf[CHUNK_SIZE];
  uint64_t sent = 0;
  while (sent < file_size) {
    size_t to_read = CHUNK_SIZE;
    if (sent + to_read > file_size)
      to_read = file_size - sent;

    ssize_t n = read(fd, buf, to_read);
    if (n <= 0) {
      if (logger)
        log_msg(logger, 1, "upload: read error at offset %llu", sent);
      close(fd);
      return -1;
    }

    proto_message_t *data_msg = create_msg(MSG_TYPE_DATA, 0, n);
    if (!data_msg) {
      close(fd);
      return -1;
    }
    data_msg->payload = malloc(n);
    if (!data_msg->payload) {
      destroy_msg(data_msg);
      close(fd);
      return -1;
    }
    memcpy(data_msg->payload, buf, n);
    data_msg->header.checksum = calculate_crc(data_msg->payload, n);

    if (proto_send_msg(sockfd, data_msg) < 0) {
      destroy_msg(data_msg);
      close(fd);
      return -1;
    }
    destroy_msg(data_msg);

    sent += n;
    progress_update(&prog, sent);
  }
  progress_finish(&prog);
  close(fd);

  // 收 TRANSFER_RESP
  proto_message_t fin = {0};
  if (proto_recv_msg(sockfd, &fin) < 0) {
    return -1;
  }
  if (fin.header.status != STATUS_OK) {
    if (logger)
      log_msg(logger, 1, "upload: final status error: %s",
              status_to_string(fin.header.status));
    destroy_msg(&fin);
    return -1;
  }
  destroy_msg(&fin);

  if (logger)
    log_msg(logger, 2, "upload: %s -> %s (%llu bytes)", local_path, remote_name,
            file_size);
  return 0;
}

int transfer_download(int sockfd, const char *remote_path,
                      const char *local_path, logger_t *logger) {
  // 发送 DOWNLOAD_REQ
  size_t path_len = strlen(remote_path) + 1;
  proto_message_t *req = create_msg(MSG_TYPE_REQUEST, CMD_DOWNLOAD, path_len);
  if (!req)
    return -1;
  req->payload = malloc(path_len);
  if (!req->payload) {
    destroy_msg(req);
    return -1;
  }
  memcpy(req->payload, remote_path, path_len);
  req->header.checksum = calculate_crc(req->payload, path_len);

  if (proto_send_msg(sockfd, req) < 0) {
    destroy_msg(req);
    return -1;
  }
  destroy_msg(req);

  // 收 DOWNLOAD_RESP
  proto_message_t resp = {0};
  if (proto_recv_msg(sockfd, &resp) < 0)
    return -1;
  if (resp.header.status != STATUS_OK) {
    if (logger)
      log_msg(logger, 1, "download: server error: %s",
              status_to_string(resp.header.status));
    destroy_msg(&resp);
    return -1;
  }
  if (resp.header.payload_len < 8) {
    destroy_msg(&resp);
    return -1;
  }
  uint64_t file_size = le_to_uint64(resp.payload);
  destroy_msg(&resp);

  int fd = open(local_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    if (logger)
      log_msg(logger, 1, "download: cannot create %s", local_path);
    return -1;
  }

  progress_t prog;
  progress_init(&prog, file_size, "Downloading");

  uint64_t received = 0;
  while (received < file_size) {
    proto_message_t data_msg = {0};
    if (proto_recv_msg(sockfd, &data_msg) < 0) {
      close(fd);
      unlink(local_path);
      return -1;
    }
    if (data_msg.header.type != MSG_TYPE_DATA) {
      destroy_msg(&data_msg);
      close(fd);
      unlink(local_path);
      return -1;
    }
    size_t chunk = data_msg.header.payload_len;
    if (received + chunk > file_size) {
      destroy_msg(&data_msg);
      close(fd);
      unlink(local_path);
      return -1;
    }
    ssize_t n = write(fd, data_msg.payload, chunk);
    if (n != (ssize_t)chunk) {
      destroy_msg(&data_msg);
      close(fd);
      unlink(local_path);
      return -1;
    }
    destroy_msg(&data_msg);
    received += chunk;
    progress_update(&prog, received);
  }
  progress_finish(&prog);
  close(fd);

  // 发完成确认
  proto_message_t *ack = create_msg(MSG_TYPE_RESPONSE, 0, 0);
  ack->header.status = STATUS_OK;
  int ret = proto_send_msg(sockfd, ack);
  destroy_msg(ack); free(ack);
  if (ret < 0)
    return -1;

  if (logger)
    log_msg(logger, 2, "download: %s -> %s (%llu bytes)", remote_path,
            local_path, file_size);
  return 0;
}
