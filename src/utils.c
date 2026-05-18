#include "utils.h"

int path_validate(const char *path) {
  if (path == NULL || path[0] == '/') {
    perror("path invalid");
    return -1;
  }
  char path_copy[MAX_PATH_LEN];
  size_t len = strlen(path);
  if (len > MAX_PATH_LEN - 1) {
    perror("path too long");
    return -1;
  }
  strncpy(path_copy, path, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';
  char *segment = strtok(path_copy, "/");
  while (segment != NULL) {
    if (strcmp(segment, "..") == 0) {
      perror("invalid path");
      return -1;
    }
    segment = strtok(NULL, "/");
  }
  return 0;
}

int path_resolve(const char *home, const char *path, char *resolved_out,
                 size_t out_size) {
  int len = snprintf(NULL, 0, "%s/%s", home, path);
  if (len < 0 || len >= MAX_PATH_LEN) {
    perror("path too long");
    return -1;
  }

  if (resolved_out != NULL) {
    snprintf(resolved_out, out_size, "%s/%s", home, path);
    return 0;
  } else {
    perror("malloc resolved_out error");
    return -1;
  }
}

int log_msg(logger_t *logger, int level, const char *fmt, ...) {
  if (logger->log_level < level) {
    return 0;
  }
  pthread_mutex_lock(&logger->mutex);
  time_t now = time(NULL);
  struct tm tm_info;
  localtime_r(&now, &tm_info);
  char time_buf[64];
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
  char msg_buf[2048];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
  va_end(args);
  fprintf(logger->log_file, "[%s] %s\n", time_buf, msg_buf);
  fflush(logger->log_file);
  if (logger->console_output) {
    fprintf(stderr, "[%s] %s\n", time_buf, msg_buf);
  }
  pthread_mutex_unlock(&logger->mutex);
  return 0;
}

void uint64_to_le(uint64_t val, uint8_t out[8]) {
  out[0] = val & 0xFF;
  out[1] = (val >> 8) & 0xFF;
  out[2] = (val >> 16) & 0xFF;
  out[3] = (val >> 24) & 0xFF;
  out[4] = (val >> 32) & 0xFF;
  out[5] = (val >> 40) & 0xFF;
  out[6] = (val >> 48) & 0xFF;
  out[7] = (val >> 56) & 0xFF;
}

uint64_t le_to_uint64(const uint8_t data[8]) {
  return (uint64_t)data[0] | ((uint64_t)data[1] << 8) |
         ((uint64_t)data[2] << 16) | ((uint64_t)data[3] << 24) |
         ((uint64_t)data[4] << 32) | ((uint64_t)data[5] << 40) |
         ((uint64_t)data[6] << 48) | ((uint64_t)data[7] << 56);
}
