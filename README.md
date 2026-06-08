# Event-Driven Gateway

一个基于 **epoll** 边缘触发（ET）模式和非阻塞 I/O 的高性能事件驱动网关，支持 **自定义行协议** 和 **HTTP/1.1 协议** 的动态切换。该项目可作为智能体网关、API 网关或微服务边车的基础骨架。

---

## ✨ 特性

- ✅ **事件驱动架构**：单线程 epoll + 非阻塞 socket，高并发低延迟。
- ✅ **多协议支持**：自动识别 HTTP/1.1（GET/POST）和自定义行协议（`TYPE|payload\n`）。
- ✅ **HTTP/1.1 解析**：状态机解析请求行、头部、`Content-Length` Body，正确处理半包/粘包。
- ✅ **Keep-Alive 长连接**：基于 `Connection` 头部，支持连接复用。
- ✅ **模块化设计**：`EpollLoop`、`ClientConnection`、`HttpChannel`、`ProtocolParser` 等职责清晰。
- ✅ **错误处理**：解析失败返回 400，连接异常自动清理。
- ✅ **CMake 构建**，头文件/实现分离，易于扩展。

---

## 📦 技术栈

- **C++17**（GCC / Clang）
- **Linux epoll**（边缘触发）
- **非阻塞 socket**
- **CMake** 构建系统
- **标准库**（无第三方依赖）

---

## 🚀 快速开始

### 环境要求

- Linux 操作系统（推荐 Ubuntu 22.04+ 或 WSL 2）
- CMake >= 3.10
- GCC >= 9 或 Clang >= 10

### 编译

```bash
git clone https://github.com/SecondMonbo/Event-Driven_Gateway.git
cd Event-Driven_Gateway
mkdir build && cd build
cmake ..
make
```
