#ifndef TRANSFER_H
#define TRANSFER_H

#include "protocol.h"
#include <stdint.h>

/**
 * 进度条结构体
 */
typedef struct {
  uint64_t total;    // 总字节数
  uint64_t current;  // 当前已传输字节数
  double start_time; // 开始时间（秒）
  int last_percent;  // 上一次显示的百分比，用于避免重复刷新
  const char *label; // 显示的标签（如 "Uploading" /"Downloading"）
} progress_t;

// 进度条函数
void progress_init(progress_t *p, uint64_t total, const char *label);
int progress_update(progress_t *p, uint64_t current);
void progress_finish(progress_t *p);

// 上传与下载主函数
int transfer_upload(int sockfd, const char *local_path, const char *remote_name,
                    logger_t *logger);
int transfer_download(int sockfd, const char *remote_path,
                      const char *local_path, logger_t *logger);

#endif // TRANSFER_H
