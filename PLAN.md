# C 文件服务器 — 完善计划

## Context

课程设计基础功能已全部实现（自定义 TCP 协议 + 5 个命令 + 进度条 + CRC32 校验）。本计划将项目从"能用"提升到"完善"，涵盖 Bug 修复、功能增强、协议改进、工程健壮性、交互体验等方面。

技术栈：C11 + POSIX sockets + pthread + 自定义二进制协议

---

## Phase 1: Bug 修复 ✅ 已完成

### 1.1 内存泄漏 ✅
- 已修复 `handle_mkdir`、`handle_list`、`handle_upload`、`handle_download` 的内存泄漏
- 统一使用 `destroy_msg(resp); free(resp);` 模式

### 1.2 SIGPIPE 崩溃 ✅
- 已添加 `signal(SIGPIPE, SIG_IGN)`

### 1.3 残留清理 ✅
- 已删除 `include/client.h`、`storage/plan.md2`、`storage/test1.txt`
- 已移除 `protocol.h` 中未实现的 `deserialize_msg` 声明

---

## Phase 2: 服务端健壮性 ✅ 已完成

### 2.1 服务端命令行参数 ✅
- 已实现 `-p <port>` 和 `-r <root_dir>` 选项
- 默认值：port=8888, root=./storage
- 支持 `-h` 显示帮助

### 2.2 信号处理 + 优雅退出 ✅
- 已实现 SIGINT 处理（Ctrl+C 优雅退出）
- 退出时打印 "Server shutting down..."

### 2.3 socket 超时 ✅
- 已添加 SO_RCVTIMEO / SO_SNDTIMEO（30秒）
- 每个客户端连接设置超时

### 2.4 编译期断言 ✅
- 已添加 `_Static_assert(sizeof(proto_header_t) == 15, ...)`

### 2.5 消除代码重复 ✅
- 已将 `le_to_uint64` 和 `uint64_to_le` 提取到 utils.c

---

## Phase 3: 功能增强（部分完成）

### 3.1 list 子目录支持 ✅
- 已实现 `list [path]` 支持可选路径参数
- 服务端 `handle_list` 接受 payload 中的路径
- 路径校验照旧，限制在 root_dir 范围内

### 3.2 文件详情 ✅
- 已实现列表格式：`D/F size mtime name`
- 示例：`D - 2026-05-13 10:46 testdir` / `F 28B 2026-05-13 10:46 test.txt`
- 服务端 `stat()` 取大小和修改时间，格式化输出

### 3.3 重命名命令 ✅
- 已添加 `CMD_RENAME = 0x02` 命令码
- payload: `old_path\0new_path`（两个以 `\0` 分隔的字符串）
- 服务端 `rename()` + 路径校验
- 客户端 `rename <old> <new>` 命令

### 3.4 断点续传（未实现）
- 协议层：DOWNLOAD_REQUEST 的 payload 中加 8 字节 offset
- 服务端 `lseek` 到 offset 后开始发数据
- 客户端：下载前检查本地已有文件大小，从该偏移继续
- UPLOAD 同理：客户端发 offset，服务端 `lseek` 后写入
- 需要在 REQ payload 中增加 offset 字段（向后兼容：0 表示从头开始）

---

## Phase 4: 交互式客户端 ✅ 已完成

### 4.1 交互模式 ✅
- 不带子命令启动时进入交互 shell：`./client [-h host] [-p port]`
- 提示符：`fshell> `
- 支持命令：`upload`、`download`、`mkdir`、`list`、`delete`、`rename`、`help`、`exit`
- 连接复用：进入时建立一次连接，所有操作在同一连接上完成，`exit` 时关闭

### 4.2 多文件上传 ✅
- `upload local1 remote1 local2 remote2 ...` 支持多对文件上传
- 交互模式下同样支持

### 4.3 命令行模式保持 ✅
- 带子命令时仍走原来的单命令模式（兼容原有用法）
- `./client upload file.txt remote.txt` 行为不变

---

## Phase 5: 集成测试 ✅ 已完成

### 5.1 测试脚本 ✅
- `test.sh`：自动启动服务端 → 运行客户端测试 → 关闭服务端
- 覆盖：mkdir、上传单文件、多文件、下载、list 根目录和子目录、rename、delete、路径遍历、大文件(2MB)
- 使用 `cmp` 比较上传/下载文件内容完整性
- 10/10 测试全部通过

### 5.2 并发测试（未实现）
- 多个客户端同时上传不同文件
- 验证 SIGPIPE 不会崩溃服务端

---

## 当前进度

| Phase | 内容 | 状态 |
|-------|------|------|
| 1 | Bug 修复（内存泄漏、SIGPIPE、残留清理） | ✅ |
| 2 | 服务端健壮性（参数解析、信号、超时、编译断言） | ✅ |
| 3 | 功能增强（list子目录、文件详情、rename） | ✅ |
| 3.4 | 断点续传 | 未实现 |
| 4 | 交互式客户端（shell模式、连接复用、多文件上传） | ✅ |
| 5 | 集成测试 | ✅ |

---

## 剩余工作

- 断点续传（协议层增加 offset 字段）
- 并发测试脚本
