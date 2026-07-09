
```markdown
# 🚀 Event-Driven High-Performance Gateway

一个基于 **epoll 边缘触发** 和 **非阻塞 I/O** 的事件驱动型 HTTP/SSE 网关，支持自定义协议、静态文件服务、线程池异步任务、定时器管理，以及 **Server-Sent Events (SSE)** 长连接推送。可作为 API 网关、智能体代理或轻量级 Web 服务器的底层骨架。

## 📌 项目定位

- **高性能**：单线程事件循环 + 非阻塞 socket，可支撑数万并发连接。
- **可扩展**：基于 `ProtocolHandler` 抽象基类，支持 HTTP、SSE、自定义协议动态切换。
- **生产级特性**：写缓冲区、Keep-Alive、线程池、定时器、Valgrind 内存检测、wrk 压测。
- **AI 就绪**：已集成 SSE 推送与线程池，可快速对接大模型流式 API。

---

## ✨ 主要特性

| 模块             | 功能                                                                                                   |
| ---------------- | ------------------------------------------------------------------------------------------------------ |
| **事件驱动引擎** | epoll 边缘触发 + 非阻塞 socket，正确处理 `EAGAIN`                                                      |
| **HTTP/1.1**     | 状态机解析请求行/头部/ `Content-Length` Body；支持 GET/POST、Keep-Alive、静态文件服务（MIME 类型映射） |
| **SSE**          | 握手响应、事件队列、周期性心跳（定时器）、连接关闭自动取消任务                                         |
| **自定义协议**   | 行协议 `TYPE                                                                                           |
| **线程池**       | 任务队列 + 完成队列，工作线程执行阻塞/耗时操作，回调在主线程安全执行                                   |
| **定时器**       | 基于 `std::priority_queue` + 懒删除，支持一次性/周期性任务，与 epoll 事件循环集成                      |
| **写缓冲区**     | 非阻塞发送 + `EPOLLOUT` 驱动，支持大文件分块传输                                                       |
| **内存安全**     | Valgrind 检测：`definitely lost: 0`，无内存泄漏                                                        |
| **压测**         | `wrk` 测试：QPS ≈ 2.8 万，平均延迟 3.5ms                                                               |

---

## 🧱 项目结构

```bash
Event-Driven_System_for_Agent_Gateways/
├── CMakeLists.txt
├── README.md
├── include/
│ ├── ConnectionContext.hpp
│ ├── ClientConnection.hpp
│ ├── EpollLoop.hpp
│ ├── ThreadPool.hpp
│ ├── TimerManager.hpp
│ └── protocol/
│ ├── ProtocolHandler.hpp
│ ├── HttpChannel.hpp
│ ├── SseHandler.hpp
│ └── CustomLineHandler.hpp
├── src/
│ ├── main.cpp
│ ├── ClientConnection.cpp
│ ├── EpollLoop.cpp
│ ├── ThreadPool.cpp
│ ├── TimerManager.cpp
│ └── protocol/
│ ├── HttpChannel.cpp
│ ├── SseHandler.cpp
│ └── CustomLineHandler.cpp
└── www/ # 静态文件根目录（需手动创建）
└── index.html
````
---

## 🚀 快速开始

### 环境要求

- Linux / WSL 2（Ubuntu 22.04 或更高）
- GCC 9+ / Clang 10+
- CMake 3.10+

### 编译

```bash
git clone https://github.com/YourName/Event-Driven_Gateway.git
cd Event-Driven_Gateway
mkdir build && cd build
cmake ..
make
````

### 运行

```bash
./gateway
```

默认监听端口：`12010`（可在 `main.cpp` 中修改）

### 测试

#### 1. HTTP 静态文件

在 `www/` 目录下放置 `index.html`，然后访问：

```bash
curl -v http://localhost:12010/
```

#### 2. HTTP Keep-Alive

```bash
curl -v -H "Connection: keep-alive" http://localhost:12010/
```

#### 3. SSE 心跳

```bash
curl -N -H "Accept: text/event-stream" http://localhost:12010/events
```

每 3 秒收到一条当前时间。

#### 4. 自定义行协议

```bash
echo "PING|hello" | nc localhost 12010
```

#### 5. 压力测试

```bash
wrk -t4 -c100 -d30s http://localhost:12010/
```

---

## 🧩 架构设计

```
┌─────────────────────────────────────────────────────────────┐
│                        EpollLoop                           │
│  ┌───────────────────────────────────────────────────────┐ │
│  │  connections_ (fd → shared_ptr<ClientConnection>)   │ │
│  └───────────────────────────────────────────────────────┘ │
│                           │                               │
│                           ▼                               │
│  ┌───────────────────────────────────────────────────────┐ │
│  │               ClientConnection                        │ │
│  │  - read_buffer_ / write_buffer_                      │ │
│  │  - handler_ (unique_ptr<ProtocolHandler>)            │ │
│  │  - send_data() / handle_write() / wakeup()           │ │
│  └───────────────────────────────────────────────────────┘ │
│                           │                               │
│                           ▼                               │
│  ┌───────────────────────────────────────────────────────┐ │
│  │              ProtocolHandler (抽象基类)              │ │
│  ├───────────┬───────────────┬───────────────────────────┤ │
│  │ HttpChannel│ CustomLineHandler│    SseHandler          │ │
│  │ (HTTP/1.1)│ (自定义协议)   │   (SSE + 心跳定时器)   │ │
│  └───────────┴───────────────┴───────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### 主要组件说明

- **`EpollLoop`**：事件循环核心，管理 epoll 实例、监听 socket、连接映射表，驱动 `epoll_wait` 和定时器。
- **`ClientConnection`**：代表一个客户端连接，持有读写缓冲区、协议处理器、写事件标志，提供 `send_data`、`wakeup`、`flush` 等接口。
- **`ProtocolHandler`**：协议处理抽象基类，所有协议（HTTP、SSE、自定义）均继承自它，实现 `process()` 和 `reset()`。
- **`ConnectionContext`**：上下文聚合结构，传递 `ClientConnection*`、`ThreadPool&`、`TimerManager&`、`EpollLoop&`，避免层层转发。
- **`ThreadPool`**：异步任务线程池，支持有/无返回值的任务，回调在主线程执行。
- **`TimerManager`**：定时器管理器，基于最小堆 + 懒删除，与 `epoll_wait` 超时集成。

---

## 📊 性能数据

- **压测工具**：`wrk -t4 -c100 -d30s http://localhost:12010/`
- **QPS**：约 28,000 req/s
- **平均延迟**：3.5 ms
- **最大延迟**：< 15 ms
- **内存检测**：Valgrind `definitely lost: 0`

---

## 🛠️ 后续扩展方向

- [ ] 接入 WebSocket 协议（基于 HTTP Upgrade）
- [ ] 实现反向代理 / 负载均衡（连接后端多个服务）
- [ ] 集成 LLM API（OpenAI / Ollama），实现流式转发
- [ ] 引入 `spdlog` 日志库，支持日志级别和文件输出
- [ ] 完善错误处理与单元测试
- [ ] 支持 HTTP/2（可选）

---

## 🤝 贡献

目前为个人学习项目，欢迎 Fork 和 Star。

---

## 📜 许可证

MIT License

---
