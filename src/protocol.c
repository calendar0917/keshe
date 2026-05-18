#include "protocol.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint32_t crc32_table[256];

// crc 校验
void crc_fill_table() {
  // 移位表，便于计算
  uint32_t target = 0xEDB88320;
  for (uint32_t i = 0; i < 256; i++) {
    // 遍历 8 位的所有可能
    uint32_t crc = i;
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        // 最后一位是 1，要进行取余
        crc = (crc >> 1) ^ target;
      } else {
        // 最后一位是 0，右移
        crc >>= 1;
      }
    }
    crc32_table[i] = crc;
  }
}

void proto_init() { crc_fill_table(); }

uint32_t calculate_crc(const uint8_t *data, size_t len) {
  // 1. 初始化：CRC32 的标准初始值是全 1
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    // 2. 计算索引：将当前余数的低 8 位与新数据字节进行异或
    uint8_t index = (crc ^ data[i]) & 0xFF;
    // 3. 查表并更新：右移 8 位（腾出空间）并异或表中的预算结果
    crc = (crc >> 8) ^ crc32_table[index];
  }
  // 4. 最终反转：结果取反才是最终的 CRC32 值
  return ~crc;
}

// 消息生命周期管理
proto_message_t *create_msg(msg_type_t type, cmd_t cmd, uint32_t payload_len) {
  proto_message_t *msg = (proto_message_t *)malloc(sizeof(proto_message_t));
  msg->header.type = type;
  msg->header.command = cmd;
  msg->header.magic = PROTO_MAGIC;
  msg->header.version = PROTO_VERSION;
  msg->header.status = 0;
  msg->header.checksum = 0;
  msg->payload = NULL;
  msg->header.payload_len = payload_len;
  return msg;
}
void destroy_msg(proto_message_t *msg) {
  if (msg && msg->payload) {
    free(msg->payload);
    msg->payload = NULL;
  }
  // 不 free msg 本身，由调用者决定
}

// 序列化与反序列化
int serialize_msg(const proto_message_t *msg, uint8_t *buffer) {
  if (!msg || !buffer)
    return -1;
  // 将 msg 序列化到 buf 当中
  uint16_t net_magic = htons(msg->header.magic);
  uint16_t status = htons(msg->header.status);
  uint32_t payload_len = htonl(msg->header.payload_len);
  uint32_t check_sum =
      htonl(calculate_crc(msg->payload, msg->header.payload_len));
  memcpy(buffer, &net_magic, 2);
  memcpy(buffer + 2, &msg->header.version, 1);
  memcpy(buffer + 3, &msg->header.type, 1);
  memcpy(buffer + 4, &msg->header.command, 1);
  memcpy(buffer + 5, &status, 2);
  memcpy(buffer + 7, &payload_len, 4);
  memcpy(buffer + 11, &check_sum, 4);
  if (msg->header.payload_len > 0 && msg->payload != NULL) {
    memcpy(buffer + 15, msg->payload, msg->header.payload_len);
  }
  return 0;
}

// TCP I/O
int send_all(int sockfd, const void *buf, size_t len) {
  const uint8_t *ptr = (const uint8_t *)buf;
  size_t total_send = 0; // 要不断重复发送，直到 == len
  while (total_send < len) {
    ssize_t n = send(sockfd, ptr + total_send, len - total_send, 0);
    if (n <= 0) {
      // 没有发送出去
      if (n < 0 && errno == EINTR) {
        // 网络问题，系统调用被中断
        continue;
      }
      // 发生错误，退出
      return -1;
    }
    total_send += n;
  }
  return 0;
}
int recv_all(int sockfd, void *buf, size_t len) {
  size_t total_recv = 0;
  uint8_t *ptr = (uint8_t *)buf;
  // 不断接收
  while (total_recv < len) {
    ssize_t n = recv(sockfd, ptr + total_recv, len - total_recv, 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    if (n == 0) {
      // 对方关闭通道
      return -2;
    }
    total_recv += n;
  }
  return 0;
}

// 协议发送接收
int proto_send_msg(int sockfd, const proto_message_t *msg) {
  // 序列化到 buffer 中
  size_t msg_len = PROTO_HEADER_SIZE + msg->header.payload_len;
  uint8_t *buffer = (uint8_t *)malloc(msg_len);
  if (buffer == NULL) {
    perror("buffer malloc error");
    return -1;
  }
  serialize_msg(msg, buffer);
  // 调用 send_all 发送
  if (send_all(sockfd, buffer, msg_len) < 0) {
    // 发送失败
    perror("msg send error");
    free(buffer);
    return -1;
  }
  free(buffer);
  return 0;
}
int parse_header(const uint8_t *buffer, size_t buf_len,
                 proto_message_t *out_msg) {
  if (buf_len < 15) {
    return -1;
  }
  uint8_t version, type, command;
  uint16_t status, net_magic;
  uint32_t payload_len, check_sum;
  memcpy(&net_magic, buffer, 2);
  memcpy(&version, buffer + 2, 1);
  memcpy(&type, buffer + 3, 1);
  memcpy(&command, buffer + 4, 1);
  memcpy(&status, buffer + 5, 2);
  memcpy(&payload_len, buffer + 7, 4);
  memcpy(&check_sum, buffer + 11, 4);
  // payload 的长度是不确定的

  out_msg->header.magic = ntohs(net_magic);
  out_msg->header.checksum = ntohl(check_sum);
  out_msg->header.payload_len = ntohl(payload_len);
  out_msg->header.command = command;
  if (out_msg->header.magic != PROTO_MAGIC) {
    return -1; // 协议不匹配
  }
  out_msg->header.status = ntohs(status);
  out_msg->header.type = type;
  out_msg->header.version = version;
  return 0;
}
int proto_recv_msg(int sockfd, proto_message_t *msg) {
  // 首先要接收头
  uint8_t head_buf[PROTO_HEADER_SIZE];
  int ret = recv_all(sockfd, head_buf, sizeof(head_buf));
  if (ret < 0) {
    if (ret == -1) {
      perror("recv system error");
    } else if (ret == -2) {
      fprintf(stderr, "client closed connection\n");
    }
    return -1;
  }
  // 取出头中的 payload 大小，继续接收
  if (parse_header(head_buf, PROTO_HEADER_SIZE, msg) < 0) {
    perror("msg head parse error");
    return -1;
  };
  msg->payload = NULL;
  if (msg->header.payload_len > 0) {
    msg->payload = (uint8_t *)malloc(msg->header.payload_len);
    if (msg->payload == NULL) {
      perror("payload malloc error");
      return -1;
    }
    if (recv_all(sockfd, msg->payload, msg->header.payload_len) < 0) {
      free(msg->payload);
      perror("msg data recv error");
      msg->payload = NULL;
      return -1;
    }
    // 校验和
    uint32_t crc = calculate_crc(msg->payload, msg->header.payload_len);
    if (crc != msg->header.checksum) {
      perror("checksum error");
      free(msg->payload);
      return -1;
    }
    return 0;
  } else {
    msg->payload = NULL;
    return 0;
  }
}

// 辅助函数
const char *status_to_string(status_t status) {
  switch (status) {
  case STATUS_OK:
    return "OK";
  case STATUS_FILE_NOT_FOUND:
    return "FILE_NOT_FOUND";
  case STATUS_EXISTS:
    return "EXISTS";
  case STATUS_INVALID_PATH:
    return "INVALID_PATH";
  case STATUS_IO_ERROR:
    return "IO_ERROR";
  case STATUS_CHECKSUM_ERROR:
    return "CHECKSUM_ERROR";
  case STATUS_INTERNAL_ERROR:
    return "INTERNAL_ERROR";
  default:
    return "UNKNOWN";
  }
}
