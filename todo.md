## 本文档用以记录需要做的事

## 9.27

---
1.重构代码，梳理生命周期和内存管理情况，为之前的“裸奔”（引用，裸指针满天飞）还债
- P0	ConnectionContext::conn	weak_ptr
- P0	SseHandler::conn_ / CustomLineHandler::conn_	删除，改用 ctx_.get_conn()
- P0	ThreadPool 完成队列回调	捕获 weak_ptr
- P1	AgentLoop 工作线程回调	改为 shared_ptr 捕获
- P1	定时器回调	捕获 weak_ptr
- P2	ConnectionContext::llm_service	保留原始指针，加注释
- P3	其他	保持现状
---

---

2.重构LLMService,准备实现agent_loop

---

3.重构http，进一步细化
