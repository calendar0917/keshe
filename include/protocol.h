#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// 常量宏
#define PROTO_MAGIC 0xF1F2
#define PROTO_VERSION 0x01
#define PROTO_HEADER_SIZE 15
#define CHUNK_SIZE 8192
#define MAX_PATH_LEN 4096
#define MAX_FILE_LEN 255

typedef enum {
  MSG_TYPE_REQUEST = 0,
  MSG_TYPE_RESPONSE,
  MSG_TYPE_DATA
} msg_type_t;

typedef enum {
  CMD_MKDIR = 0x01,
  CMD_RENAME = 0x02,
  CMD_UPLOAD = 0x10,
  CMD_DOWNLOAD,
  CMD_LIST,
  CMD_DELETE = 0x20
} cmd_t;
typedef enum {
  STATUS_OK = 0x0000,
  STATUS_FILE_NOT_FOUND = 0x0001,
  STATUS_EXISTS = 0x0002,         // 文件或目录已存在
  STATUS_INVALID_PATH = 0x0003,   // 路径遍历/非法路径
  STATUS_IO_ERROR = 0x0004,       // 磁盘读写失败
  STATUS_CHECKSUM_ERROR = 0x0005, // CRC 校验失败
  STATUS_INTERNAL_ERROR = 0x00FF, // 兜底，未分类错误
} status_t;

// 消息头结构体
typedef struct {
  uint16_t magic;
  uint8_t version;
  uint8_t type;
  uint8_t command;
  uint16_t status;
  uint32_t payload_len;
  uint32_t checksum;
} __attribute__((packed)) proto_header_t;

// payload
typedef struct {
  char filename[MAX_FILE_LEN];
  uint64_t file_size;
} upload_req_payload_t;

// 消息包装
typedef struct {
  proto_header_t header;
  uint8_t *payload; // 指向堆上的 payload
} proto_message_t;

typedef struct {
  FILE *log_file;
  pthread_mutex_t mutex;
  int log_level;
  int console_output;
} logger_t;

// 函数声明
// 初始化
void proto_init();
// crc 校验

uint32_t calculate_crc(const uint8_t *data, size_t len);
// 消息生命周期管理
proto_message_t *create_msg(msg_type_t type, cmd_t cmd, uint32_t payload_len);
void destroy_msg(proto_message_t *msg);
// 序列化
int serialize_msg(const proto_message_t *msg, uint8_t *buffer);
// TCP I/O
int send_all(int sockfd, const void *buf, size_t len);
int recv_all(int sockfd, void *buf, size_t len);
int proto_recv_msg(int sockfd, proto_message_t *msg);
int proto_send_msg(int sockfd, const proto_message_t *msg);
// 辅助函数
const char *status_to_string(status_t status);
#endif // !PROTOCOL_H
