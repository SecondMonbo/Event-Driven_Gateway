# Gateway 投递前收尾清单

> 目标：在 **11/05 前**把 gateway 冻结到“clone 能构建、测试能过、README 不吹、演示不挂”。
> 本阶段只修问题，不新增功能；未完成项写进 `KNOWN_LIMITATIONS.md`，面试时主动讲。
>
> 最后更新：2026-10-25
> 当前 HEAD：`d80b2b6`
> 未提交：`src/core/ClientConnection.cpp`、`src/llm/AgentLoop.cpp`、`src/protocol/SseHandler.cpp`

---

## 一、本轮已验证（/tmp 副本，端口 12011）

- [x] 当前代码 `cmake + make` 成功，`-Wall -Wextra` 下 0 warning。
- [x] `GET /`：只返回一个 `HTTP/1.1 200`，双响应已修。
- [x] `GET /index.html`：200，静态文件恢复。
- [x] `GET /../../etc/passwd`：403，路径穿越被挡住。
- [x] `POST /v1/chat` 无 Key：收到 `event: error` + `event: done`，客户端退出。
- [x] mock SSE 成功：收到 `event: content` + `event: done`，客户端退出。
- [ ] 未验证：并发 8/16 请求、TSan、Keep-Alive 连续请求、`/events` 心跳、达到最大轮次、连接/fd 回收。

---

## 二、P0：投递前必须闭环

| 项 | 状态 | 剩余动作 |
|---|---|---|
| P0-1 新克隆可构建 | 🟡 | `json.hpp` 已入库；还需在干净目录 clone 验证，并补 README 依赖（libcurl/pkg-config） |
| P0-2 GET 双响应 | ✅ | 已修并实测单响应 |
| P0-3 路径穿越 | ✅ | 已修并实测 403；可选补 URL decode 与符号链接测试 |
| P0-4 并发 CURL | 🟡 | 每请求新建 handle + `curl_global_init` 已做；待 8/16 并发 + TSan；`HttpClient::error_msg` 还没透传 |
| P0-5 SSE 终止语义 | 🟡 | 成功/失败已有 error/done；待收尾 max 轮次、错误信息、chat 握手 |
| P0-6 SSE 心跳/队列 | ❌ | 心跳仍注释，`pending_events_` 没有消费者；README 仍宣称 3 秒心跳 |
| P0-7 README 对齐 | ❌ | 未更新；依赖、`/v1/chat`、API Key、LICENSE、bench 数据都缺 |

### P0-5 收尾（最优先，约 30 分钟）

- [ ] 删除 `AgentLoop.cpp` 达到最大轮次后的 `emit(ChunkType::CONTENT, "\n[Error: 达到最大轮次]")`：
      此时已经发过 `done`，再发 content 属于“终止后还有事件”，应只保留 `error + done`。
- [ ] `SseHandler` 对聊天流握手用 `Connection: close`，`/events` 才用 `keep-alive`；
      现在聊天流也写 `keep-alive`，语义不对。
- [ ] 决定错误信息暴露级别：
      - 客户端：稳定 `code` + 安全 `message`；
      - 日志：`request_id`、HTTP 状态码、curl 错误、session_id。
      目前 `HttpClient` 设置了 `ctx.error_msg`，但 `on_complete` 还是 `bool`，所以客户端只收到
      `"DeepSeek request failed"`。至少把 `HTTP 401/429/500` 映射成可读 message。
- [ ] 用“永远返回 tool_calls”的 mock 测 `MAX_ITERATIONS`，确认最终是 `error + done`，不是 `content`。
- [ ] 补一条集成脚本：无 Key + mock 成功 + mock 失败，三条路径都断言有 `done`。

### P0-4 并发验证

- [ ] 启动 mock SSE（或本地 DeepSeek 兼容服务），并发 8/16 个 `/v1/chat`：
      确认响应不串、不崩、会话不混。
- [ ] `-fsanitize=thread` 跑一遍并发路径，记录结果。
- [ ] 顺便确认 `curl_global_cleanup` 只在退出时调用，`curl_global_init` 在启动时调用一次。

### P0-6 心跳/队列（二选一）

- [ ] 方案 A（推荐，但工作量大）：
      - 先修 `TimerManager`：锁内只取节点，锁外执行回调；`add_timer` 后 eventfd 唤醒主循环。
      - `SseHandler::process()`/`flush()` drain `pending_events_`。
      - 启用 `start_heartbeat()`，`/events` 每 3 秒发 heartbeat。
- [ ] 方案 B（省时间）：
      - 删除 `start_heartbeat()`、`pending_events_`、`push_event()`；
      - README 删除“心跳”描述。
- 验收：`curl -N /events` 3 秒一条心跳；断开后定时器取消，无泄漏。

### P0-7 README 与仓库

- [ ] README 补：`libcurl4-openssl-dev`、`pkg-config`、`cmake`、`g++`、`DEEPSEEK_API_KEY`。
- [ ] README 补 `/v1/chat` 的 curl 示例与 SSE 事件表（content/reasoning/tool_result/error/done）。
- [ ] 修正项目树、clone URL；说明 `call_llama` 未实现。
- [ ] 加 `LICENSE`（MIT）。
- [ ] 加 `bench/README.md`：commit、CPU/内存/OS、`-O2`、wrk 命令、文件大小、连接数、原始输出。
- [ ] 删除 `find.txt`、`test/test_tool_executor` 二进制；`www/test.bin` 换小 fixture 或脚本生成。
- [ ] 提交当前 3 个未提交文件，保证 `git status` 干净。

---

## 三、P1：强烈建议（按影响排序）

| 项 | 状态 | 说明 / 下一步 |
|---|---|---|
| P1-8 SSE 跨 chunk 行缓冲 | ❌ | `LLMService::call_deepseek` 的 `on_data` 仍按每块 `getline`，跨 TCP 包会丢 token；需 `pending_line` |
| P1-9 reasoning fallthrough | ✅ | switch 已补 `break` |
| P1-10 epoll 健壮性 | ❌ | 未处理 `EPOLLERR/EPOLLHUP/EPOLLRDHUP`；`EINTR` 会退出主循环；`send` 缺 `MSG_NOSIGNAL` |
| P1-11 发送失败连接泄漏 | 🟡 | `request_close + schedule_close` 已加；但 `send_data/handle_write` 发送失败时仍只 `close_connection()`，没 `schedule_close` |
| P1-12 全量 flush O(N) | ❌ | 每个 token 唤醒后遍历所有连接；需要 ready list / 完成回调携带 fd |
| P1-13 定时器 | ❌ | 回调持锁；`add_timer` 不唤醒 epoll；`clean_canceled_timers` 未调度 |
| P1-14 资源上限 | ❌ | 无 header/body/读写缓冲上限、无 idle timeout、无最大连接数 |
| P1-15 会话与工具 | ❌ | `cleanup_expired_sessions` 未调用；裁剪可能切断 tool_calls/tool 配对；`localtime` 非线程安全；工具不存在仍 `success=true` |
| P1-16 AgentLoop 取消 | ❌ | 客户端断开后 LLM 请求仍跑满 60s；需要 `atomic<bool> canceled` + curl 中止 |

### 建议顺序

1. P0-5 收尾（30 分钟）
2. P0-4 并发 + TSan（1 小时）
3. P0-6（2–3 小时）
4. P0-7 + 仓库卫生（半天）
5. P1-8、P1-10、P1-11（半天）
6. 测试/CI（1–2 天）
7. P1-12 ~ P1-16 有余力再做，或写进 `KNOWN_LIMITATIONS.md`

---

## 四、测试与 CI

- [ ] 引入 GoogleTest 或 Catch2，`enable_testing()` + `add_test`；`test/` 纳入构建。
- [ ] 修 `test/test_threadpool.cpp` 的 include（`core/ThreadPool.hpp`）。
- [ ] 单测：HTTP parser（分片/Keep-Alive/坏请求/大小限制）、SSE formatter、Timer、ThreadPool、路径规范化、工具调用。
- [ ] 集成：`tests/integration.sh` 覆盖 `/`、`/index.html`、404、`/events`、`/v1/chat`（mock）、路径穿越、双响应、error/done。
- [ ] Sanitizer：ASan+UBSan 跑单测/集成；TSan 跑并发 SSE/线程池。
- [ ] GitHub Actions：build + ctest + ASan；README 加 badge。
- [ ] 删除已提交的测试二进制；`.gitignore` 忽略测试产物。

**当前回归测试优先级：**

1. GET `/` 只出现一个 HTTP 响应。
2. 路径穿越返回 403，`/index.html` 返回 200。
3. 无 Key：`error + done`，`timeout 5` 内退出。
4. mock 成功：`content + done`，`timeout 5` 内退出。
5. mock 返回 `tool_calls`：`tool_result + content + done`。
6. `MAX_ITERATIONS`：`error + done`，不出现 done 后 content。

---

## 五、P2：有时间再做（不阻塞投递）

- [ ] HTTP 细节：HTTP/1.1 默认 Keep-Alive；`Connection` 值大小写不敏感；query string 不影响静态路径；405/HEAD；错误响应 Content-Type。
- [ ] 安全：API Key 鉴权、按 IP/Key 限流、请求体大小、CORS；`/v1/chat` 不暴露上游原始错误。
- [ ] 可观测性：`/metrics`（连接数、QPS、延迟、LLM 调用失败数）、结构化日志、`request_id`。
- [ ] 部署：Dockerfile / compose；端口、模型、上游 URL 从 env/CLI 读取；优雅停机。
- [ ] 演示：`www/chat.html` 用 `EventSource` 展示 content/reasoning/tool_result/done；README 放 GIF/截图。
- [ ] 代码清理：删 `ToolExecutor` 调试 `std::cout`；`HttpClient.hpp` 补 `<unordered_map>`；避免全局 `using json`；统一日志。
- [ ] 压测脚本：`bench/wrk.sh` + `bench/README.md`；Release `-O2`；对比 `-O0`；记录 commit/环境。

---

## 六、验收命令（投递前跑一遍）

```bash
# 1. 干净构建
rm -rf /tmp/gw-clean && git clone <repo-url> /tmp/gw-clean && cd /tmp/gw-clean
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build --output-on-failure

# 2. 启动 + 冒烟
./build/gateway &
curl -i http://127.0.0.1:12010/
curl -i http://127.0.0.1:12010/index.html
curl -i http://127.0.0.1:12010/nope
curl -i --path-as-is http://127.0.0.1:12010/../../etc/passwd
curl -N -H 'Accept: text/event-stream' http://127.0.0.1:12010/events
curl -N -X POST -H 'Content-Type: application/json' \
  -d '{"session_id":"s1","message":"现在几点"}' http://127.0.0.1:12010/v1/chat
```

Expected：

- GET `/` 只有一个响应；
- `/index.html` 200，路径穿越 403；
- `/events` 有 3 秒心跳；
- `/v1/chat` 成功有 `content`/`tool_result`/`done`；
- 无 Key 有 `error` + `done`；
- 所有请求在 `timeout 5` 内结束，`connections_` 不增长。

---

## 七、Definition of Done（冻结标准）

- [ ] 干净目录 clone 后能构建。
- [ ] `ctest` 全绿，ASan/TSan 至少跑过一次。
- [ ] 上述 6 条回归测试全过。
- [ ] README 的每条命令都能直接执行。
- [ ] `git status` 干净，无二进制/空文件/License 缺失。
- [ ] 写进简历的每个数字都有 `bench/` 出处。

> 如果 11/05 前做不完：优先保 P0-5、P0-6、P0-7、测试和 README；其余 P1 写进 `KNOWN_LIMITATIONS.md`，不要拖到 11 月中。
