# Mermaid 图表代码汇总

> 以下为课程设计报告中的所有 Mermaid 图表代码，供截图使用。

## 图 2-1：系统架构图截图

```mermaid
graph LR
    subgraph 客户端
        A1[交互模式]
        A2[命令模式]
    end
    subgraph 服务器
        B1[主线程<br/>监听连接]
        B2[工作线程<br/>处理请求]
        B3[文件存储目录]
    end
    客户端 <-->|"TCP连接<br/>自定义二进制协议"| 服务器
```

## 图 2-2：协议格式图截图

```mermaid
block-beta
    columns 8
    block:header:8
        columns 8
        a["Magic<br/>2B"]:2
        b["Version<br/>1B"]:1
        c["Type<br/>1B"]:1
        d["Command<br/>1B"]:1
        e["Status<br/>2B"]:2
        f["Payload Length<br/>4B"]:4
        g["Checksum CRC32<br/>4B"]:4
    end
    block:body:8
        h["Payload（变长）"]:8
    end
```

## 图 2-3：多线程模型图截图

```mermaid
graph TD
    A[主线程 main] --> B["accept（） → 客户端1连接"]
    A --> C["accept（） → 客户端2连接"]
    A --> D["accept（） → 客户端3连接"]
    B --> E["pthread_create（） → 线程1"]
    C --> F["pthread_create（） → 线程2"]
    D --> G["pthread_create（） → 线程3"]
```

## 图 2-4：系统模块结构图截图

```mermaid
graph TB
    subgraph 应用层
        A["server.c<br/>命令处理 / 多线程管理"]
        B["client.c<br/>交互式 Shell / 命令行模式"]
    end
    subgraph 传输层
        C["transfer.c<br/>上传 / 下载 / 进度条"]
    end
    subgraph 协议层
        D["protocol.c<br/>协议序列化 / 反序列化 / CRC32"]
    end
    subgraph 工具层
        E["utils.c<br/>路径校验 / 日志 / 字节序转换"]
    end
    A --> C
    B --> C
    C --> D
    D --> E
```

## 图 3-1：消息序列化与反序列化流程图截图

```mermaid
graph TD
    subgraph 发送流程
        A1["proto_message_t"] --> A2["serialize_msg（）<br/>Magic → htons<br/>Status → htons<br/>Payload_len → htonl<br/>Checksum → htonl"]
        A2 --> A3["send_all（） 发送 header + payload"]
    end
    subgraph 接收流程
        B1["recv_all（） 接收 15 字节 header"] --> B2["parse_header（）<br/>ntohs/ntohl 还原各字段<br/>验证 Magic == 0xF1F2"]
        B2 --> B3["recv_all（） 接收 N 字节 payload"]
        B3 --> B4["calculate_crc（payload）<br/>比较 CRC 值"]
        B4 --> B5{"CRC 匹配？"}
        B5 -->|"是"| B6["返回消息"]
        B5 -->|"否"| B7["返回 CHECKSUM_ERROR"]
    end
```

## 图 3-2：文件上传流程图截图

```mermaid
sequenceDiagram
    participant C as 客户端
    participant S as 服务器
    C->>S: UPLOAD_REQ<br/>payload: filename（255B） + filesize（8B）
    activate S
    S->>S: 路径校验
    S->>S: 创建文件（O_WRONLY | O_CREAT | O_EXCL）
    S-->>C: UPLOAD_RESP（STATUS_OK）
    deactivate S
    loop 逐块传输（每块 8KB）
        C->>S: DATA chunk（含 CRC32 校验和）
        activate S
        S->>S: 验证 CRC32 → 写入磁盘
        deactivate S
    end
    C-->>S: 传输完成
    activate S
    S->>S: 校验总字节数
    S-->>C: TRANSFER_RESP（最终状态）
    deactivate S
```

## 图 3-3：服务端工作线程处理流程图截图

```mermaid
graph TD
    A["客户端连接建立"] --> B["设置 socket 超时（30秒）"]
    B --> C["proto_recv_msg（） 接收请求"]
    C --> D{"switch（command）"}
    D -->|"CMD_MKDIR"| E1["handle_mkdir（）"]
    D -->|"CMD_UPLOAD"| E2["handle_upload（）"]
    D -->|"CMD_DOWNLOAD"| E3["handle_download（）"]
    D -->|"CMD_LIST"| E4["handle_list（）"]
    D -->|"CMD_DELETE"| E5["handle_delete（）"]
    D -->|"CMD_RENAME"| E6["handle_rename（）"]
    E1 --> F["destroy_msg（） 释放消息"]
    E2 --> F
    E3 --> F
    E4 --> F
    E5 --> F
    E6 --> F
    F -->|"循环"| C
    C -->|"客户端断开"| G["close（client_fd）"]
```
