# Gateway 投递前收尾清单

> 目标：在 **11/05 前**把 gateway 冻结到“clone 能构建、测试能过、README 不吹、演示不挂”。
> 本阶段只修问题，不新增功能；未完成项写进 `KNOWN_LIMITATIONS.md`，面试时主动讲。
> 验收底线：干净目录 clone → cmake → build → ctest → curl 演示，全部通过。

---

## 0. 先定基线（0.5 天）

- [ ] `git status` 干净，提交当前未提交的 `src/llm/HttpClient.cpp`（调试输出注释改动）。
- [ ] 记录当前 commit，创建 `bench/`、`docs/`、`KNOWN_LIMITATIONS.md`。
- [ ] 在干净目录 clone 一次，确认当前构建失败（`json.hpp` 未跟踪），作为修复前基线。
- [ ] 把“修复前/修复后”的 curl 输出各留一份，后面写 README/简历用。

---

## P0：投递前必须修（约 5–7 天）

### P0-1 新克隆的仓库无法编译

- 现象：`.gitignore:10` 忽略了 `include/third_party/json.hpp`，`git ls-files include/third_party` 为空；新 clone 缺少头文件。
- 修复：取消忽略并提交该头文件（`git add -f`），或用 CMake `FetchContent` 拉取 nlohmann/json；README 补 `libcurl4-openssl-dev`、`pkg-config`、`cmake`、`g++`。
- 验收：干净目录 clone 后 `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` 成功；README 命令可直接复制执行。
- 备注：`llama.cpp/` 约 3GB，不要入库；`call_llama` 未实现，README 要标注“未实现/仅占位”。

### P0-2 GET 静态文件返回两个 HTTP 响应

- 位置：`src/protocol/HttpChannel.cpp` 的 `generate_response()`；GET 分支结束后仍会执行 `body = "Hello, HTTP!"` 并发送第二个 200。
- 修复：GET 分支处理完（文件或 404）直接 `return true`；把静态文件响应与普通响应拆成独立函数，避免 fallthrough。
- 验收：raw socket 只出现一个 `HTTP/1.1`；`curl -i` 无尾随响应；Keep-Alive 连续两个 GET 正常；补回归测试断言“一个请求一个响应”。
- 备注：旧压测数据必须重跑；当前代码的双响应会让 wrk 结果不可信。

### P0-3 路径穿越

- 位置：`HttpChannel::generate_response()` 的 `file_path = base_dir + path_`，无规范化、无前缀校验。
- 修复：URL decode（至少处理 `%2e`/`%2f`）→ `std::filesystem::weakly_canonical` / `realpath` → 校验结果以 `base_dir + "/"` 开头；拒绝 `..`、绝对路径、空路径；目录请求只允许 `index.html`。
- 验收：`/../../etc/passwd`、`/%2e%2e/%2e%2e/etc/passwd`、`/..%2f..%2fetc/passwd` 均返回 400/404，不泄露文件；正常 `/index.html` 仍 200；补测试。
- 备注：顺手检查 `www` 下符号链接是否可逃逸。

### P0-4 LLM 并发共享同一个 `CURL*`

- 位置：`include/llm/LLMService.hpp:103` 的 `HttpClient http_client_`；`src/llm/HttpClient.cpp:40,91` 的 `curl_easy_reset/perform`；4 个 worker 并发调用 `LLMService::call`。
- 修复：每次 `call_deepseek` 创建局部 `HttpClient`（RAII），或 `thread_local HttpClient`，或连接池；`main` 启动时调用一次 `curl_global_init`；禁止跨线程共享 easy handle。
- 验收：并发 8/16 个 `/v1/chat`（可指向本地 mock SSE 服务）不崩、不串数据；TSan 无 data race；请求体/响应隔离。
- 备注：这是网关最核心的并发路径，优先级高于任何 UI/文档。

### P0-5 `/v1/chat` 失败与结束语义

- 位置：`src/llm/AgentLoop.cpp` 失败只 `set_error` 不 emit；`SseHandler::send_error_event` 存在但未使用；成功结束后无 `[DONE]`、不关闭/不通知完成。
- 修复：统一 SSE 事件协议：`event: content` / `reasoning` / `tool_result` / `error` / `done`；失败发送 error + done；成功发送 done（`data: [DONE]`）后关闭或明确半关闭；`SseHandler` 记录状态，避免重复发送。
- 验收：无 `DEEPSEEK_API_KEY` 时客户端收到 error + done 并退出，不挂起；正常调用最后收到 done；`curl -N` 能完整结束；补集成测试。

### P0-6 SSE 心跳/事件队列是死代码

- 位置：`SseHandler::start_heartbeat()` 在 `process()` 中被注释；`pending_events_` 只有 `push_event` 写入，没有消费者；README 宣称 3 秒心跳。
- 修复（二选一）：
  - a. 真实现：`process()`/`flush` 中 drain `pending_events_`；启用心跳；`TimerManager::add_timer` 增加 eventfd 唤醒，保证主循环阻塞时也能按时触发。
  - b. 删掉心跳与队列，README 同步删除“心跳”描述。
- 建议选 a（定时器是项目卖点），但要先修 P1-13 的定时器锁问题。
- 验收：`curl -N /events` 每 3 秒收到 heartbeat；客户端断开后定时器取消、无泄漏。

### P0-7 README 与实现对齐

- 现状问题：项目树路径过时；clone URL 是 `YourName` 占位符；未写 `DEEPSEEK_API_KEY`；未写 `/v1/chat`；未写 libcurl/pkg-config；说 MIT 但没有 LICENSE；`call_llama` 未实现；模型名 `deepseek-v4-pro` 需确认/可配置；QPS/Valgrind 无脚本无环境。
- 修复：重写“快速开始 / 接口 / 配置 / 已知限制”；加 `LICENSE`（MIT）；`bench/README.md` 记录 commit、CPU/内存/OS、编译器与 `-O2`、wrk 命令、文件大小、连接数、原始输出；旧数据标注对应 commit，不混用。
- 验收：没看过项目的人按 README 10 分钟内能跑起来并复现一次请求；所有数字有出处。
- 备注：不要再写“生产级”“数万并发”“Valgrind definitely lost: 0”，除非能现场复现。

---

## P1：强烈建议（约 3–5 天，按影响排序）

### P1-8 SSE 跨 chunk 行缓冲

- 位置：`LLMService::call_deepseek` 的 `on_data` 每块直接 `istringstream/getline`；跨 TCP 包的行会被丢弃或解析失败；`data:` 只认带空格。
- 修复：维护 `std::string pending_line`，按 `\n` 切完整行，残片留到下一块；兼容 `data:` / `data:`；`[DONE]` 只处理一次。
- 验收：mock server 按 1 字节/随机大小切块发送 SSE，内容完整；单测覆盖“跨 chunk 的 JSON 行”。

### P1-9 reasoning 事件 fallthrough

- 位置：`AgentLoop::make_emit_callback` 的 `case ChunkType::REASONING` 缺 `break`，会落到 `TOOL_RESULT`，思考内容被标成 `tool_result`。
- 修复：每个 case 加 `break`；建议改成 `if/else` 或查表；补事件类型单测。

### P1-10 epoll 健壮性

- 位置：`EpollLoop::run` 未处理 `EPOLLERR/EPOLLHUP/EPOLLRDHUP`；`epoll_wait` 返回 `EINTR` 直接 `break` 整个循环；`send` 未加 `MSG_NOSIGNAL`，SIGPIPE 可能终止进程。
- 修复：ERR/HUP/RDHUP → 关闭并移除连接；`EINTR` 继续；`send` 用 `MSG_NOSIGNAL` 或启动时 `signal(SIGPIPE, SIG_IGN)`；`accept` 处理 `EMFILE/ENFILE`（预留 fd 或降级）。
- 验收：客户端 RST/半关闭/突然断开，服务不崩、连接被清理；慢客户端不拖垮其他连接。

### P1-11 发送失败后的连接泄漏

- 位置：`ClientConnection::send_data/handle_write` 失败时 `close_connection()` 只把 `fd_ = -1`，连接仍留在 `EpollLoop::connections_`；若客户端不再发数据就永远不回收。
- 修复：发送失败只标记 `closed_`，由主循环统一 `remove_connection(fd)`；或给 `EpollLoop` 增加 `schedule_close(fd)`；`remove_connection` 先 `remove_fd` 再 `erase`，并清理 `fd_events_`。
- 验收：对端 RST 后 `connections_` 大小回落；反复断开/连接 1 万次，fd 数稳定。

### P1-12 唤醒后全量 flush 的 O(N)

- 位置：`EpollLoop::run` 收到 eventfd 后遍历所有连接 `flush()`；每个 token 一次，1 万 SSE 连接会被放大。
- 修复：完成队列回调直接携带连接标识（fd / weak_ptr），只 flush 目标连接；或用 per-connection pending 队列 + ready list。
- 验收：1000 个空闲 SSE + 1 个活跃流，CPU 不随空闲连接数线性增长；记录压测数据。
- 备注：如果时间不够，先写进 `KNOWN_LIMITATIONS.md`，不要为此延期。

### P1-13 定时器

- 位置：`TimerManager::process_expired_timers` 持锁执行回调，回调内 `add_timer/cancel_timer` 会死锁；`add_timer` 不唤醒 `epoll_wait`；`clean_canceled_timers` 未调度。
- 修复：锁内只取到期节点，锁外执行回调；提供 `wakeup` 回调/eventfd，在 `add_timer` 后唤醒；定期调用 `clean_canceled_timers`；明确一次性/周期语义。
- 验收：回调内再添加定时器不挂；主循环空闲时新增定时器按时触发；单测覆盖取消/周期/回调内添加。

### P1-14 资源上限与超时

- 问题：无 header/body/read_buffer/write_buffer 上限、无 idle timeout、无最大连接数；slowloris / 大 body 可打爆内存。
- 修复：`MAX_HEADER_SIZE`（如 8KB）、`MAX_BODY_SIZE`（如 1MB，可配置）、读/写缓冲上限；空闲连接定时清理；超限返回 413/431/408；`/v1/chat` 单独限制请求体。
- 验收：慢速发 header 的连接被超时关闭；超大 body 返回 413；内存不持续增长。

### P1-15 会话与工具

- 问题：`SessionManager::cleanup_expired_sessions()` 从未调用；按条数裁剪可能切断 assistant `tool_calls` 与 `tool` 消息的配对；`TimeTool` 用 `std::localtime` 非线程安全；`ToolResult.success` 对“工具不存在”仍为 true。
- 修复：定时清理；按完整 tool_call 组裁剪（或保留 system + 最近 N 轮）；`localtime_r`；`ToolRegistry::execute` 找不到工具时抛异常或返回明确失败，`execute_single` 据此置 false。
- 验收：长会话不产生非法消息序列；并发工具调用无 TSAN 报告；未知工具 `success=false`。

### P1-16 AgentLoop 取消

- 问题：客户端断开后 `weak_ptr` 只阻止发送，阻塞中的 LLM 请求仍跑满 60 秒，浪费配额；没有取消标志。
- 修复：`AgentLoop` 持有 `std::atomic<bool> canceled`；每轮/每次 emit 前检查；`HttpClient` 支持 `CURLOPT_XFERINFOFUNCTION` 或分段超时；连接断开时置位。
- 验收：断开 SSE 后 LLM 请求尽快中止（日志/指标可证）。

---

## P2：有时间再做（不阻塞投递）

- [ ] HTTP 细节：HTTP/1.1 默认 Keep-Alive；`Connection` 值大小写不敏感；query string 不影响静态路径；405/HEAD；错误响应 Content-Type；请求行/头长度限制。
- [ ] 安全：API Key 鉴权；按 IP/Key 限流；请求体大小；CORS；`/v1/chat` 不暴露上游错误细节。
- [ ] 可观测性：`/metrics`（连接数、QPS、延迟、LLM 调用数/失败数）、结构化日志、请求 ID。
- [ ] 部署：`Dockerfile` / `docker-compose`；配置从 env/CLI 读取（端口、模型、上游 URL、超时）；优雅停机（SIGTERM、drain、停止接收、清理）。
- [ ] 演示：`www/chat.html` 用 `EventSource` 展示 content/reasoning/tool_result/done；README 放 30 秒 GIF/截图。
- [ ] 代码清理：删 `ToolExecutor` 调试 `std::cout`；`HttpClient.hpp` 补 `<unordered_map>`；`ToolDefinition.hpp` 避免全局 `using json`；未用的 `enable_shared_from_this` 可删；统一日志。
- [ ] 压测脚本：`bench/wrk.sh` + `bench/README.md`；Release `-O2`；对比 `-O0`；记录 commit/环境；旧数据标注。

---

## 测试与 CI（与修复同步做）

- [ ] 引入 GoogleTest 或 Catch2，`enable_testing()` + `add_test`；`test/` 纳入构建，不再注释。
- [ ] 修 `test/test_threadpool.cpp` 的 include 路径（`core/ThreadPool.hpp`）。
- [ ] 单测：HTTP parser（分片/Keep-Alive/坏请求/大小限制）、SSE formatter、Timer（周期/取消/回调内添加）、ThreadPool（异常/stop/completion）、路径规范化、工具调用。
- [ ] 集成：`tests/integration.sh` 启动 gateway，curl 验证 `/`、`/index.html`、404、`/events`、`/v1/chat`（mock SSE）、路径穿越、双响应、错误事件。
- [ ] Sanitizer：ASan+UBSan 跑单测/集成；TSan 跑并发 SSE/线程池；记录命令与结果。
- [ ] GitHub Actions：build + ctest + sanitizer（至少 ASan）；README 加 CI badge。
- [ ] 删除已提交的 `test/test_tool_executor` 二进制；`.gitignore` 忽略测试产物。
- [ ] 测试先复现 bug 再修：双响应、路径穿越、SSE 跨 chunk、reasoning 类型、并发 CURL。

---

## 仓库卫生

- [ ] 删除 `find.txt`（空文件）。
- [ ] `www/test.bin` 换成小 fixture 或删除；大文件测试用脚本生成。
- [ ] 添加 `LICENSE`（MIT）。
- [ ] `.gitignore` 修正：不要忽略 `include/third_party/json.hpp`；忽略 `build/`、测试二进制、日志。
- [ ] README 的 clone URL 改成真实仓库地址。
- [ ] 提交当前未提交的 `src/llm/HttpClient.cpp` 调试注释改动。
- [ ] 可选：`.clang-format` + `clang-tidy` 基础配置；统一 4 空格/命名。
- [ ] 可选：`docs/architecture.md` + 时序图（HTTP/SSE/AgentLoop/ThreadPool）。

---

## 验收命令（投递前跑一遍）

```bash
# 1. 干净构建
rm -rf /tmp/gw-clean && git clone <repo >/tmp/gw-clean && cd /tmp/gw-clean
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

Expected：一个请求一个响应；路径穿越 400/404；SSE 有心跳；chat 有 content/tool_result/done；无 key 时 error+done。

---

## 建议顺序与时间盒（10/23–11/05）

- Day 1：基线 + 构建修复 + 双响应 + 路径穿越（P0-1/2/3）
- Day 2：CURL 并发 + SSE 终止/错误（P0-4/5）
- Day 3：心跳/队列（P0-6）+ reasoning break + SSE 行缓冲（P1-8/9）
- Day 4：epoll 健壮性 + 连接泄漏（P1-10/11）
- Day 5：定时器 + 资源上限 + 会话/工具（P1-12/13/14/15）
- Day 6–7：测试 + CI + ASan/TSan
- Day 8–9：README + bench 数据 + 演示
- Day 10：干净 clone 验收 + 冻结 + 写简历 bullet

> 如果超时：优先保 P0 + 测试 + README；P1 未完成项写进 `KNOWN_LIMITATIONS.md`，不要拖到 11 月中。
