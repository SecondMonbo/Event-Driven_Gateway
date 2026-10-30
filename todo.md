# Gateway 投递前收尾清单

> 目标：在 **11/05 前**把 gateway 冻结到“clone 能构建、测试能过、README 不吹、演示不挂”。
> 本阶段只修问题，不新增功能；未完成项写进 `KNOWN_LIMITATIONS.md`，面试时主动讲。
>
> 最后更新：2026-10-30
> 当前 HEAD：`bd87bcf`（fix: 重启心跳）
> 工作区：源码干净；新增 `docs/class-diagram.md`（未跟踪）

---

## 一、本轮已验证（/tmp 副本，端口 12011）

- [x] 当前代码 `cmake + make` 成功，`-Wall -Wextra` 下 0 warning。
- [x] 干净 clone 到 `/tmp`，Release 构建成功。
- [x] `GET /`：一个 `HTTP/1.1 200`，双响应已修。
- [x] `GET /index.html`、`GET /index.html?x=1`：200。
- [x] `GET /../../etc/passwd`：403。
- [x] `DELETE /`、`HEAD /`：405 + `Allow: GET, POST`，不再空回复。
- [x] Keep-Alive：curl 显示 `Re-using existing connection`，连续两个 GET 正常。
- [x] `/v1/chat` 无 Key：chunked + `event: error` + `event: done`。
- [x] `/v1/chat` mock 成功：chunked + `content` + `done`。
- [x] `/events`：每 3 秒 `event: heartbeat`；`timeout 10` 到点断开是客户端被 kill，不是服务端 bug。
- [x] 8 个并发 `/v1/chat`（mock）：8/8 收到 `done`，服务进程存活。
- [ ] 未验证：TSan、`MAX_ITERATIONS`、idle timeout、连接/fd 回收、README 命令可执行性。

> 并发测试说明：mock 是单线程的，8 并发通过只能说明网关没崩、没串流；不能替代 TSan。

---

## 二、P0：投递前必须闭环

| 项 | 状态 | 剩余动作 |
|---|---|---|
| P0-1 新克隆可构建 | ✅ | 干净 clone + Release 构建已通过 |
| P0-2 GET 双响应 | ✅ | 已修并实测单响应 |
| P0-3 路径穿越 | ✅ | 已修并实测 403；可选补 URL decode 与符号链接测试 |
| P0-4 并发 CURL | 🟡 | 每请求新建 handle + `curl_global_init` 已做；8 并发 mock 通过；**TSan 未跑**；`HttpClient::error_msg` 未透传 |
| P0-5 SSE 终止语义 | 🟡 | 成功/失败已有 `error/done`；chat 握手已用 `Connection: close`；待测 `MAX_ITERATIONS`；错误信息仍偏通用 |
| P0-6 SSE 心跳 | 🟡 | 心跳已通；待清调试日志、`push_event` 失败收尾、`flush` 返回值处理、队列上限 |
| P0-7 README/仓库 | ❌ | README 仍是旧版；依赖、`/v1/chat`、API Key、LICENSE、bench 数据都缺 |

### P0-6 收尾（约 30 分钟）

- [ ] 删除 `SseHandler` 心跳回调里的 `std::cerr << "[heartbeat] tick"`。
- [ ] `push_event()` 失败时不要只 `return`；建议：
      ```cpp
      if (!push_event(...)) { close(); return; }
      ```
- [ ] `ClientConnection::flush()` 目前忽略 `process()` 的返回值：
      如果 drain 发送失败返回 `CLOSE`，连接可能还留在 `connections_`。
      先决定策略：让 `flush` 返回 `ProcessResult`，或在 `SseHandler::process` 内 `request_close()`。
- [ ] 给 `pending_events_` 加一个上限（例如 100）；慢客户端不能无限积压心跳。
- [ ] 服务端正常停机时是否要给 `/events` 发最终 `0\r\n\r\n`；当前只在 chat 的 done 路径发。

### P0-5 收尾

- [ ] 用“永远返回 `tool_calls`”的 mock 测 `MAX_ITERATIONS`，确认最终是 `error + done`，且 done 后没有 content。
- [ ] 决定错误信息暴露级别：
      - 客户端：稳定 `code` + 安全 `message`；
      - 日志：HTTP 状态、curl 错误、session_id、耗时。
      目前客户端常见只有 `"DeepSeek request failed"`；至少把 401/429/500 映射成可读 message。

### P0-7 README 与仓库

- [ ] README 补：`libcurl4-openssl-dev`、`pkg-config`、`cmake`、`g++`、`DEEPSEEK_API_KEY`。
- [ ] README 补 `/v1/chat` 的 curl 示例与 SSE 事件表（content/reasoning/tool_result/error/done）。
- [ ] 修正项目树、clone URL；说明 `call_llama` 未实现。
- [ ] 加 `LICENSE`（MIT）。
- [ ] 加 `bench/README.md`：commit、CPU/内存/OS、`-O2`、wrk 命令、文件大小、连接数、原始输出。
- [ ] 删除 `find.txt`、`test/test_tool_executor` 二进制；`www/test.bin` 换小 fixture 或脚本生成。
- [ ] 把 `docs/class-diagram.md` 加入 git；提交 `docs/`。
- [ ] 考虑把 `todo.md` 从 `.gitignore` 里移出来，或者明确它是本地私有笔记。

---

## 三、P1：强烈建议（按影响排序）

| 项 | 状态 | 说明 / 下一步 |
|---|---|---|
| P1-8 SSE 跨 chunk 行缓冲 | ❌ | `LLMService::call_deepseek` 的 `on_data` 仍按每块 `getline`，跨 TCP 包会丢 token；需 `pending_line` |
| P1-9 reasoning fallthrough | ✅ | switch 已补 `break` |
| P1-10 epoll 健壮性 | ❌ | 未处理 `EPOLLERR/EPOLLHUP/EPOLLRDHUP`；`EINTR` 会退出主循环；`send` 缺 `MSG_NOSIGNAL` |
| P1-11 发送失败连接泄漏 | 🟡 | `request_close + schedule_close` 已加；但 `send_data/handle_write` 发送失败时仍只 `close_connection()`，没 `schedule_close` |
| P1-12 全量 flush O(N) | ❌ | 每个心跳/完成回调唤醒后遍历所有连接；需要 ready list / 完成回调携带 fd |
| P1-13 定时器 | ✅ | 锁外回调、eventfd 唤醒、周期 reschedule 已通；可选清理 canceled 堆顶 |
| P1-14 资源上限 | ❌ | 无 header/body/读写缓冲上限、无 idle timeout、无最大连接数 |
| P1-15 会话与工具 | ❌ | `cleanup_expired_sessions` 未调用；裁剪可能切断 tool_calls/tool 配对；`localtime` 非线程安全；工具不存在仍 `success=true` |
| P1-16 AgentLoop 取消 | ❌ | 客户端断开后 LLM 请求仍跑满 60s；需要 `atomic<bool> canceled` + curl 中止 |

### 建议顺序

1. P0-6 收尾（30 分钟）
2. P0-7 + 仓库卫生（半天）
3. P0-5 的 `MAX_ITERATIONS` 测试（30 分钟）
4. P0-4 的 TSan（1 小时）
5. P1-8、P1-10、P1-11（半天）
6. 测试/CI（1–2 天）
7. P1-12 ~ P1-16 有余力再做，或写进 `KNOWN_LIMITATIONS.md`

---

## 四、测试与 CI

- [ ] 引入 GoogleTest 或 Catch2，`enable_testing()` + `add_test`；`test/` 纳入构建。
- [ ] 修 `test/test_threadpool.cpp` 的 include（`core/ThreadPool.hpp`）。
- [ ] 单测：HTTP parser（分片/Keep-Alive/坏请求/大小限制）、SSE formatter、Timer、ThreadPool、路径规范化、工具调用。
- [ ] 集成：`tests/integration.sh` 覆盖 `/`、`/index.html`、404、`/events`、`/v1/chat`（mock）、路径穿越、双响应、error/done、DELETE 405。
- [ ] Sanitizer：ASan+UBSan 跑单测/集成；TSan 跑并发 SSE/线程池。
- [ ] GitHub Actions：build + ctest + ASan；README 加 badge。
- [ ] 删除已提交的测试二进制；`.gitignore` 忽略测试产物。

**当前回归测试优先级：**

1. GET `/` 只出现一个 HTTP 响应。
2. 路径穿越返回 403；`/index.html`、带 query 返回 200。
3. DELETE/HEAD 返回 405 + `Allow`，不是空回复。
4. 无 Key：`error + done`，`timeout 5` 内退出。
5. mock 成功：`content + done`，chunked 帧正常。
6. `/events`：3 秒心跳；`timeout 10` 后客户端断开是预期。
7. `MAX_ITERATIONS`：`error + done`，done 后无 content。

---

## 五、P2：有时间再做（不阻塞投递）

- [ ] HTTP 细节：HTTP/1.1 默认 Keep-Alive（已做）、`Connection` 值大小写不敏感（已做）、query string（已做）、HEAD 支持、错误响应 Content-Type。
- [ ] 安全：API Key 鉴权、按 IP/Key 限流、请求体大小、CORS；`/v1/chat` 不暴露上游原始错误。
- [ ] 可观测性：`/metrics`（连接数、QPS、延迟、LLM 调用失败数）、结构化日志、`request_id`。
- [ ] 部署：Dockerfile / compose；端口、模型、上游 URL 从 env/CLI 读取；优雅停机。
- [ ] 演示：`www/chat.html` 用 `fetch` 读 POST SSE（`EventSource` 不能 POST）；README 放 GIF/截图。
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
curl -i 'http://127.0.0.1:12010/index.html?x=1'
curl -i http://127.0.0.1:12010/nope
curl -i --path-as-is http://127.0.0.1:12010/../../etc/passwd
curl -i -X DELETE http://127.0.0.1:12010/
curl -N -H 'Accept: text/event-stream' http://127.0.0.1:12010/events
curl -N -X POST -H 'Content-Type: application/json' \
  -d '{"session_id":"s1","message":"现在几点"}' http://127.0.0.1:12010/v1/chat
```

Expected：

- GET `/` 只有一个响应；
- `/index.html`、带 query 200；路径穿越 403；
- DELETE 405 + `Allow`；
- `/events` 每 3 秒心跳；
- `/v1/chat` 成功有 `content`/`tool_result`/`done`；
- 无 Key 有 `error` + `done`；
- 所有短请求在 `timeout 5` 内结束，`connections_` 不增长。

---

## 七、Definition of Done（冻结标准）

- [x] 干净目录 clone 后能构建。
- [ ] `ctest` 全绿，ASan/TSan 至少跑过一次。
- [ ] 上述 7 条回归测试全过。
- [ ] README 的每条命令都能直接执行。
- [ ] `git status` 干净（只应包含已确认的 docs/ 变更）。
- [ ] 无二进制/空文件/License 缺失。
- [ ] 写进简历的每个数字都有 `bench/` 出处。

> 如果 11/05 前做不完：优先保 P0-6 收尾、P0-7、README 和测试；其余 P1 写进 `KNOWN_LIMITATIONS.md`，不要拖到 11 月中。
