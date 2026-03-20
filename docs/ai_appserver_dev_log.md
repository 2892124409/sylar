# AI 应用层开发笔记

## 项目目标（持续更新）
- 作为 AI 应用层的阶段化开发日志，持续记录从 V1 最小通路到后续生产化能力建设的全过程。
- 每个阶段固定记录：目标、实现细节、关键设计取舍、踩坑与复盘。
- 当前进度：第一阶段（最小通路）、第二阶段（Provider 切换 + 上下文摘要 + 并发能力增强）、第三阶段（用户体系与鉴权）、第四阶段（记忆检索增强 RAG）已完成。

---

## 当前范围声明
这份笔记是多阶段持续维护文档，不只记录第一阶段。

- 已落地并已详细展开：第一阶段、第二阶段、第三阶段、第四阶段。
- 已列出但未展开实现细节：第五阶段及之后。
- 当前未实现能力：写路径消息队列化、限流熔断、观测告警、发布治理等。
- 当前运行时支持通过 `ai.provider.type` 切换 `openai_compatible` / `anthropic`。

---

## 统一学习路线（按依赖顺序）
- 0) `main.cc`（先建立全局装配与生命周期视角）。
- 1) `ai_types.h` + `chat_interfaces.h`（先看业务对象与接口契约）。
- 2) `ai_app_config.h/.cc`（看 `ChatSettings` 等配置来源）。
- 3) `ai_utils.h/.cc`（看请求解析与统一响应工具）。
- 4) `ai_http_api.h`（看 API 模块对外入口）。
- 5) `ai_http_api.cc`（再看 `BuildChatRequest` 和 4 条路由）。
- 6) `chat_service.h/.cc`（看业务编排核心）。
- 7) `llm_client.h` + `openai_compatible_client.h/.cc` + `anthropic_client.h/.cc`（看模型调用实现）。
- 8) `chat_repository.h/.cc` + `async_mysql_writer.h/.cc` + `init_ai_chat.sql`（看持久化闭环）。
- 9) `chat_client.cc`（看联调入口）。
- 10) `CMakeLists.txt` + `tests/test_ai_chat_service.cc`（看工程闭环）。

---

## 第一阶段：AI 应用层最小通路（按统一学习路线展开）

### 0. 启动入口：`src/ai/main.cc`
#### 文件作用
- 启动装配器：加载配置、校验配置、装配依赖、启动 HTTP、注册信号、优雅停机。

#### 关键点
- `use_caller=true` 时走 caller 模式（单 IOM）。
- `use_caller=false` 时走主线程轮询停机（双 IOM）。
- 停机时会先停 server/worker，再 `async_writer->Stop()` 尽量刷盘。

---

### 1. 数据契约层

### 1.1 `src/ai/common/ai_types.h`
#### 文件作用
定义应用层统一数据对象：
- `ChatMessage`
- `ChatCompletionRequest`
- `ChatCompletionResponse`
- `PersistMessage`

#### 你当前最该关注
- `BuildChatRequest(..., out, ...)` 里的 `out` 类型就是 `ChatCompletionRequest`。
- API 层、Service 层、LLM 层、Storage 层都围绕这组对象协作。

#### 当前阶段注意点
- `role` 是字符串（灵活，但缺少 enum 约束）。
- 全链路统一毫秒时间戳字段。

### 1.2 `src/ai/service/chat_interfaces.h`
#### 文件作用
定义 `ChatService` 对外部能力的抽象接口：
- `ChatStore`：读历史
- `MessageSink`：写入队

#### 当前阶段注意点
- `ChatRepository` 实现了 `ChatStore`。
- `AsyncMySqlWriter` 实现了 `MessageSink`。

#### 学习问答记录（SID / conversation_id）
**Q1：一个浏览器或客户端，不管多少次对话都是同一个 SID 吗？**

**A：** 通常在同一浏览器配置和 Cookie 未清理的情况下，SID 会保持一段时间不变；但不是永久不变。清 Cookie、换设备、换无痕窗口都会得到新 SID。

**Q2：SID 和 conversation_id 有什么区别？**

**A：**
- `SID` 标识“是谁在用”（客户端会话主体）。
- `conversation_id` 标识“这个主体下的哪一条对话线程”。
- 一个 SID 可以对应多个 conversation_id（例如每次 `/new` 开新线程）。

**Q3：上下文是通过 SID 还是 conversation_id 区分？**

**A：** 通过两者组合区分。当前实现里上下文 key 是 `sid#conversation_id`，数据库查询条件也是 `sid + conversation_id` 组合，单独用其中一个都不够精确。

---

### 2. 配置来源层：`src/ai/config/ai_app_config.h/.cc`
#### 文件作用
把 `ai.*` 配置收敛为强类型 settings，并在启动前校验。

#### 关键对象
- `ServerSettings`
- `OpenAICompatibleSettings`
- `ChatSettings`
- `MysqlSettings`
- `PersistSettings`

#### 你当前最该关注
- `BuildChatRequest(..., const ChatSettings& chat_settings, ...)` 的默认值来源：
  - `default_temperature`
  - `default_max_tokens`
  - 以及历史查询相关 limit 上限

#### 当前阶段注意点
- `Validate()` 是 fail-fast，关键配置不合法会拒绝启动。
- OpenAI-Compatible API key 支持配置项优先，环境变量兜底（`OPENAI_COMPATIBLE_API_KEY` / `OPENAI_API_KEY`）。
- 当前配置键统一为 `ai.openai_compatible.*`（V1 已去除 `ai.deepseek.*` 配置回退）。

---

### 3. 公共工具层：`src/ai/common/ai_utils.h/.cc`
#### 文件作用
提供 API 层和 Service 层共享的工具函数。

#### 当前学习 API 必看函数
- `ParseJsonBody()`：把请求体解析成 JSON。
- `ParseLimit()`：处理 `limit` 参数，带默认值和上限截断。
- `WriteJson()` / `WriteJsonError()`：统一 JSON 输出。
- `GenerateRequestId()`：生成链路 request_id，贯穿日志与响应定位。

#### 当前阶段注意点
- API 层的错误返回形态基本都通过 `WriteJsonError()` 保持一致。

#### 学习问答记录（为什么传输使用 JSON）
**Q：为什么 HTTP 传输层选择 JSON 格式？**

**A：**
- JSON 是结构化键值格式，天然适合表达 `message/model/temperature/max_tokens` 这类请求参数。
- 客户端生态兼容性高（浏览器、curl、Python、C++ 都容易构造和解析）。
- 与主流大模型接口风格一致，向上/向下游对接成本低。
- 错误响应可以统一成固定结构（如 `ok/code/message/request_id`），便于前后端约定与排障。

---

### 4. API 入口声明层：`src/ai/http/ai_http_api.h`
#### 文件作用
对外只暴露一个入口：
- `RegisterAiHttpApi(...)`

#### 语义
这个函数不是业务逻辑本身，而是“把 AI 路由模块挂到 HTTP Server”。

---

### 5. API 路由实现层：`src/ai/http/ai_http_api.cc`
#### 文件作用
完成两件事：
- 注册路由
- 做 HTTP <-> 业务对象的协议转换

#### 5.1 辅助函数
- `GetRequestId()`：取链路 ID。
- `BuildChatRequest()`：把 HTTP 请求翻译成 `ChatCompletionRequest`。
- `WriteSuccessJson()`：统一成功响应体。

#### 5.2 V1 路由清单与详细行为

##### 5.2.1 `GET /api/v1/healthz`
- 作用：健康检查与探活接口，不触发业务层或数据库读写。
- 执行步骤：
1. 构建固定成功 JSON：`ok=true`、`status="up"`、`timestamp_ms=NowMs()`。
2. 若请求头存在 `x-request-id`，原样回传 `request_id` 字段。
3. 统一通过 `WriteJson(..., 200)` 返回。
- 成功响应关键字段：`ok`、`status`、`timestamp_ms`、可选 `request_id`。
- 失败路径：无业务失败分支，异常通常只会来自框架层（极少见）。

##### 5.2.2 `POST /api/v1/chat/completions`
- 作用：同步聊天接口，返回一次性完整回答 JSON。
- 入参来源：
1. `BuildChatRequest()` 从 Cookie/Set-Cookie 提取 `sid`。
2. 从 JSON body 读取 `message`（必填），可选读取 `conversation_id/model/temperature/max_tokens`。
3. 可选字段缺失时使用配置默认值（如 `default_model`、`default_temperature`、`default_max_tokens`）。
- 执行步骤：
1. 先做 HTTP -> `ChatCompletionRequest` 协议转换。
2. 调用 `chat_service->Complete(...)` 执行业务编排（上下文、LLM、入队持久化）。
3. 成功时 `WriteSuccessJson()` 统一输出。
- 成功响应关键字段：`ok`、`conversation_id`、`reply`、`model`、`finish_reason`、`created_at_ms`、可选 `request_id`。
- 失败路径：
1. 参数/JSON 解析失败：返回 `400` JSON 错误。
2. 业务失败：按 `ChatService` 返回的状态码输出统一 JSON 错误（常见 `503`）。

##### 5.2.3 `POST /api/v1/chat/stream`
- 作用：流式聊天接口，使用 SSE 逐段返回模型增量输出。
- 入参与转换：与 `/chat/completions` 相同，先调用 `BuildChatRequest()`。
- 执行步骤：
1. 设置 SSE 响应头：`Content-Type: text/event-stream`、`Cache-Control: no-cache`、`X-Accel-Buffering: no`。
2. 设置 `response->setStream(true)`，并先 `session->sendResponse(response)` 发送响应头建立流通道。
3. 创建 `SSEWriter`，把 `ChatService::StreamComplete` 的事件回调映射为 `writer.sendEvent(data, event)`。
4. 事件序列由 service 发出：`start -> delta* -> done`，异常时发 `error`。
- 成功语义：连接中收到完整 `done` 事件，Servlet 返回 `0`。
- 失败路径：
1. 请求参数无效：返回 `400` JSON（尚未进入 SSE）。
2. SSE 头发送失败：直接返回 `-1`（连接通常已断开）。
3. 业务失败：尽量再发一个 `error` 事件，然后返回 `-1`。

##### 5.2.4 `GET /api/v1/chat/history/:conversation_id`
- 作用：按会话线程读取历史消息列表。
- 入参来源：
1. `sid` 来自 Cookie/Set-Cookie。
2. `conversation_id` 来自路径参数 `:conversation_id`。
3. `limit` 来自 query 参数，经过 `ParseLimit()` 做默认值与上限归一化。
- 执行步骤：
1. 调用 `chat_service->GetHistory(...)` 查询历史。
2. 把 `vector<ChatMessage>` 转成 JSON 数组 `messages`。
3. 返回 `ok=true`、`conversation_id`、`messages` 与可选 `request_id`。
- 成功响应关键字段：`ok`、`conversation_id`、`messages[{role, content, created_at_ms}]`。
- 失败路径：按 service 返回状态码统一输出 JSON 错误（如 `400/500`）。

##### 5.2.5 默认路由（未命中）
- 作用：兜底处理未注册路径。
- 返回：统一 `404` JSON 错误，错误信息 `route not found`。

#### 你当前最该关注（下一步重点）
- `BuildChatRequest()` 的默认值合并和必填校验。
- `/chat/completions` 的状态码映射与统一错误响应模型。
- `/chat/stream` 的“先发 header 再发事件”以及 `start/delta/done/error` 时序。

---

### 6. 业务编排核心：`src/ai/service/chat_service.h/.cc`
#### 文件作用
把上下文加载、模型调用、持久化写入、历史查询串成统一业务编排层；对 API 层暴露稳定接口，对下游依赖做统一错误收敛。

#### 6.1 依赖注入与职责边界
- 构造函数注入四类依赖：
1. `ChatSettings`：业务策略配置（`require_sid`、上下文窗口、默认系统提示词等）。
2. `LlmClient`：模型调用抽象（同步/流式）。
3. `ChatStore`：历史读取抽象（`LoadRecentMessages/LoadHistory`）。
4. `MessageSink`：异步写入抽象（入队持久化）。
- 边界约定：
1. `ChatService` 负责流程编排，不直接写 SQL。
2. `ChatService` 不关心具体模型厂商协议，只依赖 `LlmClient` 接口。

#### 6.2 `Complete()`（同步对话）详细流程
- 入口目标：接收一条用户消息，返回完整 assistant 回复 JSON 所需字段。
- 执行步骤：
1. 参数与依赖校验：`sid/message`、`m_llm_client/m_store/m_sink`。
2. 会话 ID 处理：无 `conversation_id` 则自动生成。
3. 上下文准备：`EnsureContextLoaded()` + `SnapshotContext()`。
4. 组装 LLM 请求：`system_prompt + 历史消息 + 当前 user`。
5. 调用 `LlmClient::Complete()` 获取完整回答。
6. 组装 user/assistant 两条 `PersistMessage` 并入队持久化。
7. `AppendContextMessages()` 更新内存上下文窗口。
8. 回填 `ChatCompletionResponse` 并返回 `200`。
- 状态码语义：
1. `400`：参数缺失（如 `sid/message`）。
2. `500`：服务内部依赖未初始化或历史读取失败。
3. `503`：模型调用失败或持久化入队失败。

#### 6.3 `StreamComplete()`（流式对话）详细流程
- 入口目标：边生成边下发 SSE 事件，同时在完成后落持久化与更新上下文。
- 事件时序：`start -> delta* -> done`，异常分支发 `error`。
- 执行步骤：
1. 与同步接口一致的参数校验与依赖校验。
2. 会话 ID 与上下文准备（同 `Complete()`）。
3. 发 `start` 事件，告知 `conversation_id/model/created_at_ms`。
4. 调用 `LlmClient::StreamComplete()`，每个增量回调都转成 `delta` 事件。
5. 聚合最终文本 `assembled`，构造 assistant 消息。
6. user/assistant 入异步写队列，失败发 `error` 事件并返回失败。
7. 更新上下文后发送 `done`（含 `finish_reason/usage`）。
8. 回填统一响应对象，便于上层保留最终聚合结果。
- 一个关键细节：
1. 若 `done` 发送失败，仅记录日志，不回滚已完成的业务流程（通常是客户端提前断开）。

#### 6.4 `GetHistory()`（历史查询）详细流程
- 入口目标：按 `sid + conversation_id` 返回最近 `limit` 条消息。
- 执行步骤：
1. 校验 `sid`（受 `require_sid` 控制）与 `conversation_id`。
2. 先查内存缓存 `SnapshotContext()`，命中则直接返回（必要时截断到 `limit`）。
3. 缓存未命中再调用 `ChatStore::LoadHistory()` 回源查询。
4. 成功返回 `200`，失败返回 `400/500`。
- 当前实现特征：
1. 读路径优先缓存，减轻数据库压力。
2. 缓存与数据库结果均按时间正序返回给 API 层。

#### 6.5 上下文缓存机制（V1）
- 缓存 key：`sid#conversation_id`，保证“同主体 + 同线程”隔离。
- 关键函数：
1. `EnsureContextLoaded()`：未命中时从存储加载 recent messages 并入缓存。
2. `SnapshotContext()`：返回消息副本并刷新 `touched_at_ms`。
3. `AppendContextMessages()`：追加本轮 user/assistant，并按 `max_context_messages` 裁剪旧消息。
- 并发模型：通过 `m_mutex` 保护 `m_contexts`，避免竞态访问。

#### 6.6 持久化语义（V1 要点）
- `PersistMessage()` 成功语义是“成功入异步队列”，不是“请求返回前已落盘”。
- 实际落库由后台 `AsyncMySqlWriter` 批量事务完成。
- 这意味着：
1. API 成功返回后，极短窗口内数据库可能暂时查不到最新消息（最终一致）。
2. 若进程在刷盘前异常退出，可能存在少量已入队未落盘消息。

#### 6.7 当前阶段关键限制与后续方向
- 限制：
1. 上下文按“消息条数”裁剪，可能丢失早期关键事实。
2. 只有单会话窗口裁剪，没有全局会话淘汰策略。
- 方向（已在后续阶段标注优先级）：
1. 第二阶段优先升级为“token 预算 + 摘要记忆 + 最近窗口”。
2. 第四阶段接入检索记忆（RAG）补足长期事实召回。

---

### 7. 模型接入层

### 7.1 `src/ai/llm/llm_client.h`
- 定义统一接口：`Complete/StreamComplete`。

### 7.2 `src/ai/llm/openai_compatible_client.h/.cc`
- 基于 `libcurl` 对接 OpenAI-Compatible `chat/completions` 接口。
- 当前运行时由 `main.cc` 注入该实现，支持通过 `base_url + api_key + model` 切换不同兼容厂商。
- 流式场景通过行缓冲解析 SSE `data:` 增量。

### 7.3 `src/ai/llm/anthropic_client.h/.cc`
- 已完成 Anthropic Messages API 的同步/流式实现（含 `x-api-key` 与 `anthropic-version` 头）。
- 第二阶段已接入运行时 provider 选择，可通过配置直接切换。

#### 当前阶段注意点
- 回调中断会触发 `stream callback aborted`。
- 流式 JSON 解析失败会返回 `parse_error`。
- 第二阶段起支持 `ai.provider.type=openai_compatible|anthropic` 动态选择 provider。

#### 学习问答记录（chunk 边界与上下游保证）
**Q：什么是 chunk 边界？为什么会把 JSON“切坏”？**

**A：**
`CURLOPT_WRITEFUNCTION` 每次收到的是“任意大小的一段字节”（chunk），不是协议语义上的完整行或完整 JSON。

1. chunk 边界由网络/TCP/缓冲策略决定，不等于 SSE 事件边界
2. 一条 `data: {...}\n` 可能被拆成多次回调
3. 如果按“每次回调”直接 `json::parse`，就会因为半截 JSON 触发解析失败

**Q：当前实现如何避免 chunk 边界把 JSON 切坏？**

**A：**
核心原则是“按协议边界解析，不按网络 chunk 解析”。

1. 上游（LLM -> 本服务）解析保证
   - 在 `OpenAICompatibleClient::StreamWriteCallback` 里先把字节追加到 `line_buffer`
   - 仅当发现 `\n` 时才切出“完整一行”交给 `HandleStreamDataLine`
   - 仅解析 `data:` 行 payload，尾部半行残片保留到下一次回调继续拼接
   - 这样即使单个 JSON 被拆成多个 chunk，也会在拼成完整行后再解析

2. 下游（本服务 -> 客户端）发送保证
   - `ChatService::StreamComplete` 统一输出结构化事件：`start/delta/done/error`
   - `SSEWriter::sendEvent` 负责按 SSE 协议编码：`event:` + 多行 `data:` + 空行终止
   - 底层 `writeFixSize` 保证单次事件帧 payload 完整写出
   - 但网络仍可能把一个事件帧拆成多个 TCP 包，客户端也必须按 SSE 协议边界重组，不能按一次 read 解析

结论：上游和下游都采用“协议边界驱动”的处理方式，避免了 chunk 边界导致的半包 JSON 解析问题。

#### 7.4 Provider 状态（阶段演进）
- 第一阶段：运行时固定 `OpenAICompatibleClient`。
- 第二阶段：运行时支持 `openai_compatible/anthropic` 二选一配置切换。
- 对 OpenAI-Compatible 厂商的切换方式（不改代码）：
1. 修改 `ai.openai_compatible.base_url` 指向目标厂商兼容网关。
2. 修改 `ai.openai_compatible.api_key` 为目标厂商密钥。
3. 修改 `ai.openai_compatible.default_model` 为目标厂商模型名。
- 第二阶段已完成项：
1. 新增并启用 `ai.provider.type`。
2. `main.cc` 按配置创建对应 `LlmClient` 实现。
3. `ChatService` 与 API 层保持无感。

#### 7.5 `OpenAICompatible` vs `Anthropic` 协议差异（面试高频）
下面按“协议层”而不是“业务层”对比，便于面试时说明为什么需要两个 client。

##### 1) 接口入口与鉴权头
- OpenAI-Compatible：
1. 典型路径：`/v1/chat/completions`
2. 鉴权头：`Authorization: Bearer <api_key>`
- Anthropic Messages：
1. 典型路径：`/v1/messages`
2. 鉴权头：`x-api-key: <api_key>`
3. 版本头：`anthropic-version: 2023-06-01`（或厂商要求版本）

##### 2) 请求体结构差异
- OpenAI-Compatible：
1. 核心字段：`model/temperature/max_tokens/stream/messages`
2. `messages` 常见形态：`[{role, content}]`
3. `system` 通常作为一条 `role=system` 消息混在 `messages` 内
- Anthropic Messages：
1. 核心字段同样有 `model/max_tokens/temperature/stream`，但结构命名与语义不完全一致
2. 当前实现把所有 `role=system` 消息聚合为顶层 `system` 字段
3. 非 system 消息写入 `messages`，角色主要是 `user/assistant`

##### 3) 同步响应结构差异
- OpenAI-Compatible（当前实现消费）：
1. 文本：`choices[0].message.content`
2. 结束原因：`choices[0].finish_reason`
3. token 统计：`usage.prompt_tokens / usage.completion_tokens`
- Anthropic（当前实现消费）：
1. 文本：`content[]` 数组中 `type=text` 的 `text` 字段
2. 结束原因：`stop_reason`
3. token 统计：`usage.input_tokens / usage.output_tokens`

##### 4) 流式响应（SSE chunk）差异
- OpenAI-Compatible（当前实现）：
1. 主要增量字段：`choices[0].delta.content`
2. 结束标记常见为 `[DONE]` 或 `finish_reason` 收尾
- Anthropic（当前实现）：
1. 主要增量字段：`delta.text` 或 `content_block.text`
2. 同时需要处理更多事件形态（message/content block 的阶段性事件）

##### 5) 错误模型差异
- OpenAI-Compatible：常见 `{"error": {"message": "..."} }`
- Anthropic：既可能有 `error` 对象，也可能在流事件中出现 `type=error`
- 工程含义：错误分支不能只做“一个 JSON 结构假设”，要按 provider 做解析适配。

##### 6) 为什么不能只靠一个 parser（面试回答点）
- 表面上两者都“HTTP + JSON + SSE”，但字段路径、事件语义、鉴权头、版本头都不同。
- 统一抽象层应放在 `LlmClient` 接口；具体协议差异放在各自 adapter/client。
- 当前项目做法：`ChatService` 只依赖 `LlmClient`，`OpenAICompatibleClient/AnthropicClient` 各自吸收协议差异。

##### 7) 当前 V1 实施状态（避免答偏）
- 第一阶段主通路：`OpenAICompatibleClient`
- 第二阶段能力：运行时按 `ai.provider.type` 在 `OpenAICompatibleClient/AnthropicClient` 间切换
- 统一入口保持不变：`ChatService` 只依赖 `LlmClient` 抽象

---

### 8. 持久化层

### 8.1 `src/ai/storage/chat_repository.h/.cc`
- 读路径：加载历史、补上下文。
- 关键行为：查询倒序，返回前 reverse 成正序。

### 8.2 `src/ai/storage/async_mysql_writer.h/.cc`
- 写路径：异步队列 + 批量事务刷盘。
- `FlushBatch()` 事务序列：`BEGIN -> UPSERT conversations -> INSERT chat_messages -> COMMIT`。

### 8.3 `src/ai/sql/init_ai_chat.sql`
- 两张表：`conversations`（会话索引）+ `chat_messages`（消息明细）。

---

### 9. 联调客户端：`src/ai/client/auth_chat_client.cc`
#### 文件作用
提供注册/登录/鉴权状态查询 + 同步/流式/历史查询的一体化调试入口。

#### 关键命令
- `/register <u> <p>`
- `/login <u> <p>`
- `/logout`
- `/me`
- `/new`
- `/history [limit]`
- `/stream on|off`
- `/exit`

#### 当前阶段注意点
- 本地地址默认禁用代理（避免 127.0.0.1 被代理劫持）。
- JSON 打印采用 UTF-8 replace 策略，避免非法字节崩溃。

---

### 10. 构建与测试
#### 目标产物
- `ai_chat_server`
- `ai_auth_chat_client`
- `test_ai_chat_service`

#### 意义
形成“服务端 + 客户端 + 单测”的最小工程闭环。

---

### 第一阶段调用链

#### 同步链路
```text
Client
  -> POST /api/v1/chat/completions
  -> ai_http_api::BuildChatRequest
  -> ChatService::Complete
      -> EnsureContextLoaded
      -> ChatRepository::LoadRecentMessages
      -> OpenAICompatibleClient::Complete
      -> AsyncMySqlWriter::Enqueue(user)
      -> AsyncMySqlWriter::Enqueue(assistant)
      -> AppendContextMessages
  <- JSON(reply, conversation_id, model, finish_reason)

(后台线程)
AsyncMySqlWriter::Run -> FlushBatch -> BEGIN/UPSERT/INSERT/COMMIT
```

#### 流式链路
```text
Client
  -> POST /api/v1/chat/stream
  -> SSE headers sent
  -> ChatService::StreamComplete
      -> emit(start)
      -> OpenAICompatibleClient::StreamComplete
          -> delta callback
      -> AsyncMySqlWriter::Enqueue(user/assistant)
      -> emit(done)
  <- SSE events: start/delta/done (or error)
```

---

#### 阶段总结
##### 一句话总结
第一阶段的核心目标是打通 AI 应用层最小通路：HTTP 路由可用、业务编排可跑、模型调用可达、消息可入库、客户端可联调。

##### 当前结论
到第一阶段结束时，最小闭环已经稳定可用；但仍存在明显工程边界：

1. 暂无登录体系，仅以 `SID` 区分会话。
2. 落库语义为“入队成功”，不是“请求返回前强一致落盘”。
3. 内存上下文没有全局淘汰策略（当前只裁剪单会话消息长度）。
4. 缺少生产级限流、熔断、审计和观测能力。

#### 下一阶段
##### 第二阶段会怎么做
第二阶段围绕“可扩展、可并发、可持续对话”推进，重点包括：

1. 上下文策略升级为“token 预算 + 摘要记忆 + 最近窗口”。
2. API 层解耦，`ai_http_api` 仅保留路由装配。
3. 引入 `ai.provider.type`，支持 `openai_compatible/anthropic` 运行时切换。
4. 存储层改为 MySQL 连接池模型，并推进 LLM/DB IO 协程友好化。

---

## 第二阶段：Provider 切换、上下文摘要与并发能力增强

### 0. 第二阶段改动类速览
#### 0.1 配置与启动装配
- `src/ai/config/ai_app_config.h/.cc`
- `src/ai/config/ai_server.example.yml`
- `src/ai/main.cc`

本阶段在配置层新增了 provider 选择（但不支持热重载，服务器启动后就不能更改）、摘要策略参数和 MySQL 连接池参数，并在 `main.cc` 完成按配置装配依赖。

#### 0.2 API 层解耦
- 新增：`src/ai/http/ai_http_handlers.h/.cc`
- 调整：`src/ai/http/ai_http_api.cc`

`RegisterAiHttpApi` 只保留路由装配，具体路由处理下沉到 `AiHttpHandlers`。

#### 0.3 业务编排升级
- `src/ai/service/chat_service.h/.cc`
- `src/ai/service/chat_interfaces.h`

完成上下文策略从“按条数裁剪”升级为“token 预算 + 摘要记忆 + 最近窗口”。

#### 0.4 LLM 客户端并发模型升级
- 新增：`src/ai/llm/fiber_curl_session.h/.cc`
- 调整：`src/ai/llm/openai_compatible_client.cc`
- 调整：`src/ai/llm/anthropic_client.cc`

将阻塞式 `curl_easy_perform` 改为 `curl_multi + IOManager` 协程化驱动。

#### 0.5 持久化与并发读写升级
- 新增：`src/ai/storage/mysql_connection_pool.h/.cc`
- 调整：`src/ai/storage/chat_repository.h/.cc`
- 调整：`src/ai/storage/async_mysql_writer.h/.cc`
- 调整：`src/ai/sql/init_ai_chat.sql`

存储层从“单连接 + 串行锁”升级为连接池模型，并补充摘要字段持久化。

#### 0.6 构建与测试
- 调整：`CMakeLists.txt`
- 调整：`tests/test_ai_chat_service.cc`

新增源码纳入构建，补齐第二阶段关键路径测试。

---

### 1. 本阶段目标
在第一阶段最小通路可用的基础上，第二阶段目标是把 AI 应用层推进到“可扩展、可并发、可持续对话”的状态：

- 运行时可按配置切换 LLM provider（而不是编译期写死）
- 上下文不再只按条数裁剪，支持摘要记忆与 token 预算控制
- API 路由从单文件堆叠改为装配与处理分离
- LLM 调用与 DB 读写不再占死 IO 工作线程
- 持久化结构支持摘要长期记忆

这一阶段仍然是“应用层能力增强”，未进入用户体系、鉴权、限流、观测等生产治理主题。

---

### 2. 本阶段完成项

#### 1. 配置体系升级与 provider 可切换装配
配置层新增并落地：

- `ProviderSettings`（`ai.provider.type`）
- `AnthropicSettings`（`ai.anthropic.*`）
- `ChatSettings` 扩展：
  - `max_context_tokens`
  - `recent_window_messages`
  - `summary_trigger_tokens`
  - `summary_max_tokens`
  - `summary_prompt`
- `MysqlSettings` 扩展：
  - `pool_min_size`
  - `pool_max_size`
  - `pool_acquire_timeout_ms`

`Validate()` 由“固定校验 OpenAI-Compatible”改为“按 `ai.provider.type` 分支校验”，在启动期 fail-fast。

`main.cc` 装配逻辑升级为：

1. 读取 provider 配置
2. 按类型构建 `OpenAICompatibleClient` 或 `AnthropicClient`
3. 注入统一 `LlmClient` 到 `ChatService`
4. 同时初始化 MySQL 连接池并注入仓储与异步写入器

这样 `ChatService` 和 HTTP API 层不需要感知 provider 差异，仍只依赖抽象接口。

##### 学习问答记录（中间件边界）
**Q：当前有哪些中间件？分别干了什么？**

**A：**
当前 AI 应用层在 `main.cc` 里只注入了一个中间件：

1. `request_id` 注入中间件（`CallbackMiddleware::AfterCallback`）
   - 位置：`src/ai/main.cc` 中 `server->addMiddleware(...)`
   - 行为：为每个请求生成 `X-Request-Id`，同时写入 request/response 头
   - 目的：统一链路追踪，便于日志排障和客户端问题定位
   - 特征：不拦截请求，不改变业务语义，只做横切增强

**Q：为什么现在的数据库操作不属于中间件？**

**A：**
因为数据库操作属于业务/存储层职责，不在 HTTP 中间件执行链上：

1. 中间件的定义是“请求管线横切逻辑”
   - 典型入口是 `before/after`，运行在路由处理前后
   - 关注点是鉴权、追踪、限流、统一补头、审计等

2. 当前数据库读写发生在业务层
   - 入口是 `ChatService` 调用 `ChatRepository` / `AsyncMySqlWriter`
   - 这些调用是“具体业务动作”，不是“通用请求横切动作”

3. 最关键区别
   - 中间件：对多路由通用、与具体业务无关、可插拔
   - DB 操作：强业务语义、依赖会话与消息模型、不可简单复用到所有路由

#### 2. API 层解耦：`ai_http_api` 只负责装配
本阶段把原先集中在 `ai_http_api.cc` 的路由实现拆出到 `AiHttpHandlers`：

- `ai_http_api.cc`：只做“路由 -> handler 成员函数”的绑定
- `ai_http_handlers.cc`：承接 health/completions/stream/history 四条路由逻辑

收益：

- 单文件复杂度下降，后续新增 API 路径不再挤在一个注册函数里
- handler 可独立阅读与测试，更符合 controller 风格

#### 3. ChatService 上下文策略升级：token 预算 + 摘要记忆 + 最近窗口
核心结构变化：

- `ConversationContext` 新增 `summary` 与 `summary_updated_at_ms`
- `SnapshotContext()` 返回完整上下文对象（而非仅消息数组）

核心算法变化：

1. `BuildBudgetedContextMessages(...)`
   - 先拼接 `summary + recent messages`
   - 使用启发式 token 估算（`bytes/4 + overhead`）
   - 按预算从近到远选择可携带上下文

2. `MaybeRefreshSummary(...)`
   - 当累计 token 超过阈值，且历史长度超过最近窗口时触发摘要
   - 通过 `LlmClient::Complete` 生成新摘要
   - 内存上下文收缩为“摘要 + 最近窗口消息”
   - 摘要通过 `ChatStore::SaveConversationSummary(...)` 落库

3. 失败语义
   - 摘要刷新失败只记日志，不阻断主回复
   - 主对话链路优先保证可用性

#### 4. LLM 网络 IO 协程化：`FiberCurlSession`
本阶段新增 `FiberCurlSession`，把一次 curl 请求纳入 sylar 协程调度：

- 在 IOManager 存在时：
  - 用 `curl_multi` 驱动
  - 通过 socket/timer 回调接入 IOManager 事件和定时器
  - 当前 fiber 在等待期间 `YieldToHold()`，不阻塞工作线程
- 在无 IOManager 场景（如部分测试线程）：
  - 自动退化到 `curl_easy_perform()`

`OpenAICompatibleClient` 与 `AnthropicClient` 的同步/流式请求都切换为 `FiberCurlSession::Perform()`。

##### 学习问答记录（FiberCurlSession 协程语义）
**Q：`FiberCurlSession` 挂起的是哪个协程？**

**A：**
是“执行到 `FiberCurlSession session(curl);` / `session.Perform()` 这一行的当前协程”。

1. 构造函数里会捕获当前上下文
   - `m_iom = sylar::IOManager::GetThis()`
   - `m_wait_fiber = sylar::Fiber::GetThis()`
2. 在 `Perform()` 的等待阶段调用 `Fiber::YieldToHold()` 挂起该协程
3. 当 socket/timer 事件就绪时，再由 IOManager `schedule(m_wait_fiber, ...)` 恢复同一个协程继续执行

结论：`FiberCurlSession` 不是“为 curl 新建一个协程”，而是“把当前协程在网络等待期间挂起，事件就绪后恢复”。

**Q：为什么 `FiberCurlSession` 里有两个 `private static` 静态成员函数？为什么这样设计？**

**A：**
这是为了同时满足 libcurl 回调机制和类封装边界：

1. 两个函数对应两类不同回调
   - `SocketCallback` 对应 `CURLMOPT_SOCKETFUNCTION`（socket 读写事件）
   - `TimerCallback` 对应 `CURLMOPT_TIMERFUNCTION`（超时事件）

2. 必须使用 `static`
   - libcurl 是 C 风格回调，要求普通函数指针签名
   - 非 static 成员函数带隐式 `this`，签名不匹配，不能直接传给 libcurl
   - static 成员函数无隐式 `this`，可直接作为回调入口

3. 放在 `private` 是为了封装
   - 这两个函数只是内部“回调入口（trampoline）”
   - 外部不应直接调用；真正处理逻辑回到对象成员函数（通过 `userp` 还原 `FiberCurlSession*`）执行

结论：两个函数是“按事件类型分工”，`static` 是“为兼容 C 回调签名”，`private` 是“保持内部实现不外泄”。

**Q：`curl_multi` 是怎么取代 `curl_easy_perform` 的？为什么这是项目亮点？**

**A：**
关键点不是“换了一个 libcurl 接口”，而是把“网络状态机驱动权”从 libcurl 内部拿到应用层，并接入 sylar 的协程调度。

先看两种模型的本质差异：

1. `curl_easy_perform`（阻塞模型）
   - libcurl 在函数内部自己执行一整套循环：
     - 等待 socket 可读/可写
     - 推进连接/TLS/发送/接收状态
     - 判断超时与完成
   - 调用方线程会被持续占用，直到请求结束才返回

2. `curl_multi`（外部驱动模型）
   - libcurl 不再“自己阻塞跑完”，而是把关键事件要求告诉调用方：
     - 需要监听哪个 fd（读/写）
     - 需要多久后触发一次 timeout 驱动
   - 调用方在事件到来时再调用 `curl_multi_socket_action(...)` 推进状态机
   - 完成状态通过 `curl_multi_info_read(...)` 拉取 `CURLMSG_DONE`

在本项目中的具体落地（`FiberCurlSession::Perform`）：

1. 初始化 multi 并注册回调
   - `CURLMOPT_SOCKETFUNCTION` -> `SocketCallback`
   - `CURLMOPT_TIMERFUNCTION` -> `TimerCallback`
   - 代码：`src/ai/llm/fiber_curl_session.cc`

2. 由回调把 libcurl 事件桥接到 sylar
   - `SocketCallback` 内部调用 `RegisterSocketWatch`，把 fd 注册到 IOManager
   - `TimerCallback` 内部调用 `UpdateTimer`，把 timeout 注册到 IOManager 定时器

3. 当前 Fiber 在无事件时主动让出线程
   - `WaitForSignal()` 中调用 `Fiber::YieldToHold()`
   - 当前线程可以去运行其他 Fiber，不被单个上游请求占住

4. 事件到来时恢复“同一个 Fiber”
   - fd/timer 触发后走 `OnSocketEvent(...)`
   - 事件写入 `m_pending_actions`
   - 通过 `schedule(m_wait_fiber, m_wait_thread)` 恢复原 Fiber

5. 恢复后继续推进 curl 状态机
   - 循环弹出 pending action
   - 每次调用 `curl_multi_socket_action(...)`
   - 每轮用 `DrainMessages()` 检查是否收到 `CURLMSG_DONE`

可以把它理解为以下时序：

1. `Perform()` 启动 multi 状态机
2. libcurl 告诉“监听哪些 fd / 何时超时”
3. 当前 Fiber 挂起（`YieldToHold`）
4. IOManager 等待 epoll/timer 事件
5. 事件到来后恢复该 Fiber
6. Fiber 继续推进 multi 状态机
7. 直到读到 `CURLMSG_DONE`，返回最终 `CURLcode`

为什么这是本项目亮点（应用层与网络层融合）：

1. 业务层收益
   - `LlmClient` 接口保持不变，上层业务不用改调用方式
   - 同步/流式请求都复用同一套协程化网络驱动

2. 运行时收益
   - 从“线程阻塞等待上游 API”升级为“Fiber 挂起等待 IO”
   - 显著降低 IO worker 被长请求占死的概率
   - 高并发下线程利用率与吞吐更稳定

3. 架构收益
   - 这是 AI 应用层（LLM 请求）第一次真正贴合 sylar 网络层调度模型
   - 后续做 `curl_multi + 连接复用 + 更细粒度超时/熔断` 都有明确演进基础

补充：兼容性与兜底策略

1. 若当前线程不在 IOManager/Fiber 上，自动回退 `curl_easy_perform`
2. 统一 `Cleanup()` 保证 timer/fd/multi 资源可重复安全清理
3. 该设计兼顾“性能路径”与“非协程环境可用性”

结论：`curl_multi` 在这里不是“简单替换函数调用”，而是把 LLM 网络请求改造成 sylar 可调度、可挂起、可恢复的事件驱动执行模型，是第二阶段最核心的工程增强点之一。

##### 面试表达模板（可直接复用）
**30 秒版本（电梯表达）**

我们把 `curl_easy_perform` 换成了 `curl_multi + IOManager`。  
本质上是把 libcurl 内部的阻塞状态机驱动，改成应用层事件驱动：socket/timer 事件接入 sylar，当前 Fiber 在等待期间 `YieldToHold` 挂起，事件到来后恢复同一 Fiber 继续推进。  
这样上层 `LlmClient` 接口不变，但并发模型从“线程阻塞”等上游 API，升级成“协程挂起等待 IO”。

**2 分钟版本（展开说明）**

这个改造的关键不是 API 替换，而是“驱动权迁移”：

1. 以前 `curl_easy_perform` 是黑盒阻塞调用，线程会一直被占用到请求结束。
2. 现在 `curl_multi` 会告诉我们“该监听哪些 fd、多久后触发超时”，我们把这些事件接到 sylar 的 IOManager。
3. 在 `FiberCurlSession::Perform` 里，无事件就 `WaitForSignal -> YieldToHold`，让当前 Fiber 主动让出线程。
4. 当 fd 可读写或超时到达时，回调把 action 入队，并 `schedule` 恢复原 Fiber。
5. 恢复后继续 `curl_multi_socket_action` 推进状态机，直到 `curl_multi_info_read` 拿到 `CURLMSG_DONE`。

这个方案的工程价值是：

1. 对业务层无侵入：`ChatService`/`LlmClient` 调用方式不变。
2. 对并发模型收益明显：避免 IO worker 被上游长请求占死。
3. 与 sylar 架构一致：把应用层 LLM 调用真正纳入 Fiber 调度体系。

##### 线上故障复盘（2026-03-18，已修复）
**现象：**
1. 客户端流式/同步调用过程中，服务端偶发直接退出。
2. 日志关键报错：
   - `addEvent assert fd=xx event=1 fd_ctx.event=1`
   - 断言点在 `sylar::IOManager::addEvent(...)`。
3. 退出信号为 `Aborted (core dumped)`，不是业务返回错误。

**定位过程（证据链）**：
1. 从客户端症状入手  
   - 现象是 `Stream status=0`、`Couldn't connect to server`。  
   - 这类报错本身不能证明“网络不通”，也可能是“服务端刚刚崩溃”。  
2. 立刻转到服务端日志确认进程状态  
   - 看到明确断言：`addEvent assert fd=12 event=1 fd_ctx.event=1`。  
   - 结论：不是上游 API 慢，不是鉴权失败，而是进程内部断言触发导致退出。  
3. 用回溯地址反查源码行号（关键一步）  
   - 用 `addr2line -e ./bin/ai_chat_server <addr...>` 把栈里的地址映射到源码。  
   - 命中 `fiber_curl_session.cc` 的 `RegisterSocketWatch(...)` / `SocketCallback(...)` / `Perform(...)`。  
   - 证据把问题范围收敛到“curl 协程化桥接层”。  
4. 结合 sylar 事件模型验证框架约束  
   - `IOManager::addEvent` 对同一 fd 同一事件位重复注册会断言失败。  
   - sylar 的 IO 事件是一次性触发语义（触发后位会清掉）。  
5. 第一次假设：缺失 rearm 导致事件丢失 -> 超时  
   - 先补了 rearm 后，超时问题缓解，但出现新问题：偶发断言崩溃。  
   - 说明方向对了一半：确实跟一次性事件模型相关，但实现还有竞态。  
6. 第二次假设：`SocketCallback` 与 `rearm` 交错，重复 `addEvent`  
   - 崩溃行固定在 `RegisterSocketWatch` 的 `addEvent(READ)`，且 `fd_ctx.event` 已含 READ。  
   - 结论成立：重挂载逻辑没有跟踪“已挂载状态”，存在重复注册窗口。  
7. 最终修复  
   - 增加 `m_watch_events`（目标位）+ `m_armed_events`（已挂载位）双状态。  
   - 改成 `to_add / to_del` 增量更新，避免无差别删加。  
   - 在 `OnSocketEvent` 里先清已触发位，再按目标位补挂载。  
8. 回归验证  
   - 连续多轮同步 + 流式请求压测。  
   - 检查日志无 `addEvent assert` / `ASSERTION`，服务进程持续存活。

**根因：**
1. `IOManager` 的 fd 事件是一次性触发语义，触发后该事件位会被移除。
2. `FiberCurlSession` 在“socket 回调 + rearm 重挂载”交错场景下，可能对同一 fd 的同一事件位重复执行 `addEvent`。
3. 重复注册命中 `IOManager` 的防御性断言，导致进程崩溃。

**修复方案：**
1. 在 `FiberCurlSession` 增加“目标监听位 + 已挂载位”双状态跟踪：
   - `m_watch_events`：libcurl 希望监听的目标事件位；
   - `m_armed_events`：当前已在 IOManager 成功挂载的事件位。
2. `RegisterSocketWatch(...)` 改为增量更新：
   - 先算 `to_del / to_add`；
   - 只删除需要删除的位，只添加需要添加的位；
   - 避免无差别先删后加导致竞态窗口扩大。
3. `OnSocketEvent(...)` 在处理触发事件前，先把本次触发位从 `m_armed_events` 清掉，再按目标位做重挂载。
4. `CancelSocketWatch/Cleanup` 同步清理 `m_watch_events` 和 `m_armed_events`，防止脏状态残留。

**验证结果：**
1. 回归后同步/流式多轮请求可持续运行。
2. 未再出现 `addEvent assert` / `ASSERTION` 崩溃日志。
3. 服务端保持存活，返回链路正常。

##### 面试高频问答：项目里最难排查的 bug
**Q：你在这个项目里遇到最难解决的 bug 是什么？怎么定位并修复的？**

**A（可直接复述）：**
最难的是把 `curl_easy_perform` 协程化为 `curl_multi + Fiber` 后出现的线上崩溃问题。  
当时客户端表现是偶发 `Couldn't connect to server`，但真正原因是服务端被断言打崩：`IOManager::addEvent` 发现同一 fd 的同一事件被重复注册。

我按“证据链”定位：
1. 先从客户端异常切到服务端日志，确认是进程崩溃而不是业务返回。
2. 用回溯地址 + `addr2line` 映射到 `fiber_curl_session.cc`，把范围收敛到 curl 协程桥接层。
3. 结合 sylar 的一次性事件语义和 `addEvent` 断言约束，复盘出竞态窗口：`SocketCallback` 与 rearm 交错导致重复注册。

修复上，我没有继续“补 if”，而是把状态机补全：
1. 增加“目标监听位 + 已挂载位”双状态；
2. 重挂载改为 `to_add/to_del` 增量更新；
3. 触发事件后先清已触发位再补挂载；
4. 清理路径保证两个状态同步回收。

最后用同步/流式多轮回归验证，确认不再触发断言，服务稳定运行。  
这个 bug 难点不在 API 调用，而在“把 libcurl 事件模型和 sylar 一次性事件模型正确对齐”。

#### 5. MySQL 连接池化与摘要持久化
本节按当前学习目标，聚焦“主服务读路径并发能力”，不展开主服务写库流程。

##### 5.1 为什么要引入连接池（读路径视角）
1. 避免每次查询都 `mysql_init + mysql_real_connect`，降低延迟与抖动。
2. 限制并发连接上限，避免读高峰把数据库连接打满。
3. 池满时使用 fiber 挂起等待，避免阻塞 IO 工作线程。

##### 5.2 主服务读路径调用链（本阶段重点）
1. `ChatService::GetHistory(...)`
2. `ChatRepository::LoadHistory(...)`
3. `ScopedMysqlConn` 构造时向池借连接（`Acquire`）
4. 执行查询并返回结果
5. `ScopedMysqlConn` 析构自动归还连接（`Release`）

同类读接口还包括 `LoadRecentMessages(...)`、`LoadConversationSummary(...)`。

##### 5.3 连接池在读路径下的并发策略
`Acquire(timeout_ms, error)` 的三分支策略：

1. 有空闲连接：直接返回
2. 无空闲但未达上限：新建连接后返回
3. 已达上限：当前 fiber 进入等待队列并 `YieldToHold` 挂起，等待 `Release` 或超时定时器唤醒

补充机制：

1. 空闲连接超过阈值会做 `mysql_ping` 健康检查
2. 失效连接会关闭并从池计数中扣减，再继续尝试获取可用连接
3. `Shutdown()` 会唤醒等待者，避免协程永久挂起

##### 5.4 摘要字段与结构准备（与读路径相关）
`conversations` 已包含：

1. `summary_text`
2. `summary_updated_at_ms`

建表/迁移脚本保持幂等（`ADD COLUMN IF NOT EXISTS`），便于重复部署与升级。

##### 5.5 后续 MQ 架构声明（写路径迁移）
1. 当前仓库仍有主服务写库实现代码（如 `AsyncMySqlWriter` 等），但本节不再作为学习重点。
2. 第五阶段引入 MQ 后，主服务进程目标是“只读库 + 生产消息”，不再使用主服务的 MySQL 连接池执行写库。
3. 写库职责迁移到 MQ 消费者进程，由消费者独立维护自己的 MySQL 连接池并批量落库。

---

### 3. 测试与验证
本阶段已完成的验证如下：

1. 编译验证
   - `cmake --build build -j8 --target ai_chat_server test_ai_chat_service`
   - 结果：通过

2. 单测验证
   - `./bin/test_ai_chat_service`
   - 结果：通过
   - 新增覆盖：`TestSummaryRefresh()`（摘要触发与窗口收缩）

3. 运行验证（OpenAI-Compatible）
   - 启动服务后 `GET /api/v1/healthz` 返回 `ok=true`
   - `/api/v1/chat/completions` 可进入新调用链（无有效上游 key 时按预期返回上游超时/错误）

4. 运行验证（Anthropic）
   - 切换 `ai.provider.type=anthropic` 后服务可启动
   - `GET /api/v1/healthz` 正常返回

5. 崩溃回归验证（FiberCurlSession）
   - 场景：连续多轮 `/api/v1/chat/completions` 与 `/api/v1/chat/stream`
   - 目标：验证不会再触发 `IOManager::addEvent` 重复注册断言
   - 结果：服务端持续存活，日志中未出现 `addEvent assert`/`ASSERTION`

6. 数据库结构验证
   - `conversations` 表已存在 `summary_text`、`summary_updated_at_ms` 字段

说明：
- 全量 `ctest` 中大量 `Not Run` 来自仓库其他未构建测试目标，不属于本阶段回归失败。

---

### 4. 当前实现与后续优化
#### 当前取舍
- token 计数采用启发式估算，优先轻量落地，不引入 tokenizer 依赖
- provider 切换粒度是进程级（启动时选择一个 provider）
- 摘要刷新在主流程后执行，失败降级为告警日志

#### 已知风险点
1. `MysqlConnectionPool` 的高并发边界仍需专项压测（超时、唤醒顺序、shutdown 竞态）
2. `FiberCurlSession` 在极端流式并发下仍需长期稳定性观察
3. 摘要质量受提示词和模型稳定性影响，需后续评估迭代

#### 后续可优化方向
1. 引入真实 tokenizer（提高 token 预算精度）
2. 支持 provider 级别更细粒度路由（按模型/租户分流）
3. 将摘要与长时记忆扩展到检索增强（RAG）
4. HTTP 响应封装可继续优化（统一 `setJsonBody(...)`、移动语义减少拷贝）

---

### 5. 阶段总结
第二阶段的核心价值，是把第一阶段“可跑通”升级为“可扩展 + 可并发 + 可持续对话”：

- provider 不再写死，具备运行时可配置切换能力
- 上下文管理从固定条数进入“预算 + 摘要 + 窗口”策略
- API、LLM、Storage 三层都完成了面向后续扩展的边界重构
- 并发模型从“阻塞风险明显”转向“协程友好”

到这里，AI 应用层第二阶段主目标已经完成。

### 6. 下一阶段
第三阶段建议优先推进：

1. 用户体系与鉴权（账号、token、会话归属），优先解决跨端连续性问题。
2. 记忆检索增强（RAG/向量召回）。
3. 写路径消息队列化（MQ）：主服务只负责读库 + 生产消息，消费者负责批量落库并独立维护连接池。
4. 稳定性治理（限流、重试、熔断、降级）。
5. 可观测性（指标、日志、trace、告警）。

---

## 第三阶段：用户体系与鉴权（强制登录）

### 0. 第三阶段改动类速览
#### 0.1 配置与启动装配
- `src/ai/config/ai_app_config.h/.cc`
- `src/ai/config/ai_server.example.yml`
- `src/ai/main.cc`

本阶段新增 `AuthSettings` 与 `ai.auth.*` 配置项，并在 `main.cc` 完成账号仓库、鉴权服务、中间件的装配。

#### 0.2 账号存储层
- 新增：`src/ai/storage/auth_repository.h/.cc`
- 调整：`src/ai/sql/init_ai_chat.sql`

新增 `users`、`auth_tokens` 两张表，提供注册/登录/鉴权/登出的持久化支撑。

#### 0.3 认证业务服务层
- 新增：`src/ai/service/auth_service.h/.cc`

实现注册、登录、Bearer 鉴权、登出撤销，并统一输出身份主体 `principal_sid`。

#### 0.4 API 路由与中间件接入
- 调整：`src/ai/http/ai_http_api.h/.cc`
- 调整：`src/ai/http/ai_http_handlers.h/.cc`
- 新增：`src/ai/middleware/auth_middleware.h/.cc`
- 新增：`src/ai/middleware/request_id_middleware.h/.cc`
- 调整：`src/ai/main.cc`

新增 4 条鉴权路由，并把聊天请求会话主体统一为 `X-Principal-Sid`（登录用户主体）。
中间件实现从 `main.cc` 内联 Lambda 解耦为独立类，`main` 只做中间件装配调用。

#### 0.5 构建与链接
- 调整：`CMakeLists.txt`

新增 `auth_service/auth_repository` 与 `ai/middleware/*` 编译单元，并补充 `OpenSSL::Crypto` 链接用于 PBKDF2/SHA256。

---

### 1. 本阶段目标
在第二阶段“可扩展 + 可并发 + 可持续对话”的基础上，第三阶段目标是解决身份连续性问题：

- 引入账号体系，支持用户名密码注册/登录
- 会话主体统一为用户维度（`u:<user_id>`），支持跨端延续历史
- 非公共 API 一律要求 Bearer 鉴权，移除游客访问分支
- 保持现有 chat/history API 兼容，不破坏第一、二阶段链路
- 为第四阶段 RAG 和后续权限治理提供统一身份锚点

---

### 2. 本阶段完成项

#### 1. 身份模型升级：`SID` 到 `principal_sid`
核心语义：

1. 登录请求：
   - 通过 Bearer token 鉴权后注入 `X-Principal-Sid = u:<user_id>`
2. ChatService 维度：
   - 继续使用 `sid + conversation_id` 作为上下文与存储键
   - 其中 `sid` 统一为登录用户主体 SID（`u:<user_id>`）

这样做到“业务层代码结构不变、身份语义升级”。

#### 2. 配置体系新增 `ai.auth.*`
新增并落地：

- `token_ttl_seconds`（默认 `2592000`）
- `password_pbkdf2_iterations`（默认 `150000`）

`AiAppConfig::Validate()` 增加了 auth 配置合法性校验（TTL 和 PBKDF2 迭代次数必须大于 0）。

配置语义补充：

1. `token_ttl_seconds` 决定登录 token 的有效期。登录成功后服务端写入过期时间（`expires_at_ms = now + ttl`），超时后受保护接口返回 401；主动 `/logout` 会提前使 token 失效。
2. `password_pbkdf2_iterations` 决定密码哈希的工作因子。注册与登录校验都会使用该参数；值越大，抗暴力破解能力越强，但登录 CPU 开销也越高。

#### 3. 存储层新增账号与令牌仓库
`AuthRepository` 提供以下能力：

1. 用户创建与查询
2. 令牌保存、查询、撤销

新增数据表：

1. `users`
2. `auth_tokens`

数据库执行模型补充（除异步入库 `AsyncMySqlWriter` 外）：

1. `AuthRepository` 的 `CreateUser/GetUserByUsername/SaveToken/GetToken/RevokeToken` 基于 `mysql_query/mysql_store_result`，属于同步阻塞调用。
2. `ChatRepository` 读路径（`LoadRecentMessages/LoadHistory/LoadSummary`）同样基于同步查询，也是阻塞调用。
3. 启动阶段 `ChatRepository::EnsureSchema` 与 `AuthRepository::EnsureSchema` 的建表 SQL 为同步执行。
4. 连接池层的 `mysql_real_connect`（建连）和 `mysql_ping`（连接健康检查）也是同步调用。

当前阶段取舍与后续优化方向：

1. 现阶段保持“写异步（`AsyncMySqlWriter`）+ 读同步（连接池）”的最小复杂度方案，优先保证第三阶段可用性与稳定性。
2. 若后续读 QPS 上升，可先增加独立 DB 线程池隔离读请求，避免 HTTP worker 被慢 SQL 长时间占用。
3. 更高并发场景可评估 MySQL 非阻塞接口 + `IOManager` 的协程化 DB I/O（类似 `FiberCurlSession`）。
4. 连接池可继续优化为预热连接、后台健康检查与分级超时，降低请求路径上的建连/探测抖动。

#### 4. 认证业务服务落地（`AuthService`）
类概括：

`AuthService` 是第三阶段认证模块的业务编排核心，负责把“账号规则”从 HTTP 协议层中抽离出来。  
它的职责是：参数校验、密码哈希派生、token 生成与校验、错误语义收敛、身份主体构造；  
它不直接操作 HTTP 请求/响应，也不直接管理数据库连接，数据读写通过 `AuthRepository` 完成。

实现接口与职责：

1. `Register(username, password, user_id, error, status)`
   - 作用：注册新用户。
   - 核心流程：校验用户名/密码格式 -> 生成随机 salt -> `PBKDF2-HMAC-SHA256` 派生哈希 -> 调用 `CreateUser` 落库。
   - 输出语义：成功返回 `user_id`；用户名冲突映射为 `409`；参数不合法为 `400`。

2. `Login(username, password, access_token, identity, error, status)`
   - 作用：校验账号密码并签发登录态 token。
   - 核心流程：按用户名查用户 -> 校验用户状态与密码哈希 -> 生成随机 `access_token` -> 计算 `token_hash` 后落库（含过期时间）。
   - 输出语义：成功返回 token 明文与 `identity`（含 `principal_sid`）；失败统一返回认证失败语义，避免账号枚举。

3. `AuthenticateBearerToken(token, identity, error, status)`
   - 作用：对 Bearer token 做服务端鉴权。
   - 核心流程：对 token 明文做 SHA256 -> 查 `auth_tokens` -> 校验“存在 + 未过期 + 未撤销 + 用户状态有效”。
   - 输出语义：成功返回身份快照 `identity`；失败返回 `401` 或 `500`（存储异常）。

4. `Logout(token, error, status)`
   - 作用：使当前 token 失效（主动登出）。
   - 核心流程：对 token 做 SHA256 -> 调用 `RevokeToken` 写入撤销时间。
   - 输出语义：撤销成功后该 token 立即不可用于后续鉴权。

5. `BuildPrincipalSid(user_id)`
   - 作用：构造统一主体标识，格式为 `u:<user_id>`。
   - 价值：把下游 chat/history 的主体键统一为可跨端复用的用户维度，而不是临时会话维度。

安全策略（当前实现）：

1. 密码哈希：`PBKDF2-HMAC-SHA256 + 随机 salt`
2. token 形态：opaque token（随机 32 bytes -> hex）
3. 数据库存储：仅存 `token_hash`（SHA256），不存 token 明文
4. 鉴权校验：检查“存在 + 未过期 + 未撤销 + 用户状态有效”

##### 学习问答记录（登录/注册安全策略）
**Q：当前是怎么保证登录/注册信息安全的？详细说明当前策略**

**A：**

当前实现从“传输、密码、令牌、鉴权入口、返回与错误语义”五个层面做安全控制：

1. 传输层（防窃听）：
   - 生产环境要求走 HTTPS/TLS，避免用户名、密码、token 在链路中被明文嗅探。
   - 本地调试可用 HTTP，但这不属于安全部署形态。
2. 密码层（防明文落库）：
   - 注册/登录阶段会接收明文密码参与校验，但不会明文存储。
   - 注册时使用 `PBKDF2-HMAC-SHA256 + 随机 salt` 生成密码哈希。
   - 数据库仅保存 `password_hash + password_salt`，不保存明文密码。
   - PBKDF2 迭代次数由 `ai.auth.password_pbkdf2_iterations` 控制（当前默认 150000）。
3. 令牌层（防会话泄露扩大）：
   - 登录成功后签发随机 `access_token`（opaque token，非 JWT，不内嵌用户信息）。
   - 数据库只存 `token_hash(SHA256)`，不存 token 明文。
   - 鉴权时校验：token 是否存在、是否过期、是否已撤销、用户状态是否有效。
4. 鉴权入口层（防未授权访问）：
   - 非公共接口统一强制 Bearer 鉴权。
   - 已移除游客回退路径，未登录请求直接 401。
   - 鉴权成功后由中间件注入 `X-Principal-Sid`，下游业务统一按登录主体处理。
5. 返回与错误语义层（减少信息泄露）：
   - 登录失败统一返回“invalid username or password”，避免暴露“用户名是否存在”。
   - 响应不返回 `password_hash`、`salt`、`token_hash` 等敏感内部字段。
   - 仅登录成功时返回一次 token 明文给客户端保存。

当前边界（已知待增强）：
1. 还未实现登录限流/失败锁定。
2. 还未实现 refresh token 与多端会话治理。
3. 还需补充更完善的安全审计日志。

#### 5. API 路由扩展与鉴权中间件
模块概括：

第三阶段在 API 层做了两件关键事：

1. 路由能力扩展：新增账号体系路由（register/login/logout/me），形成登录态闭环。
2. 入口鉴权前移：通过中间件在“进入路由前”统一做 request_id 注入与 Bearer 鉴权，避免每个 handler 重复写同类逻辑。

代码职责划分：

1. `ai_http_api.cc`：只负责“路由注册与分发绑定”。
2. `ai_http_handlers.cc`：负责 HTTP 参数解析、调用 Service、组装 JSON/SSE 响应。
3. `RequestIdMiddleware` / `AuthMiddleware`：负责横切能力（链路追踪、鉴权）。

路由清单与职责：

1. `GET /api/v1/healthz`
   - 作用：健康检查，返回服务存活状态与时间戳。
   - 鉴权：公共路由，不要求 Bearer。
2. `POST /api/v1/auth/register`
   - 作用：创建账号。
   - 入参：`username`、`password`（JSON）。
   - 鉴权：公共路由，不要求 Bearer。
3. `POST /api/v1/auth/login`
   - 作用：账号登录并签发 `access_token`。
   - 出参：`token_type`、`access_token`、`principal_sid`、`user`。
   - 鉴权：公共路由，不要求 Bearer。
4. `POST /api/v1/auth/logout`
   - 作用：撤销当前 token，使其立即失效。
   - 鉴权：必须 Bearer。
5. `GET /api/v1/auth/me`
   - 作用：校验当前 token 并返回当前登录身份信息。
   - 鉴权：必须 Bearer。
6. `POST /api/v1/chat/completions`
   - 作用：同步对话请求。
   - 鉴权：必须 Bearer，主体来自 `X-Principal-Sid`。
7. `POST /api/v1/chat/stream`
   - 作用：SSE 流式对话请求。
   - 鉴权：必须 Bearer，主体来自 `X-Principal-Sid`。
8. `GET /api/v1/chat/history/:conversation_id`
   - 作用：按会话查询历史消息。
   - 鉴权：必须 Bearer，主体来自 `X-Principal-Sid`。

中间件执行顺序与行为：

1. `RequestIdMiddleware`（先执行）
   - 为请求生成 `X-Request-Id`，同时写入 request/response。
   - 作用：统一问题定位锚点。
2. `AuthMiddleware`（后执行）
   - 对公共路由直接放行。
   - 对非公共路由解析 `Authorization: Bearer <token>` 并鉴权。
   - 鉴权成功后注入 `X-Principal-Sid`、`X-Auth-User-Id`、`X-Auth-Username`。
   - 鉴权失败时直接返回 401/500，不再进入路由处理。

鉴权矩阵（第三阶段）：

1. 公共路由：`/api/v1/healthz`、`/api/v1/auth/register`、`/api/v1/auth/login`
   - 无 Bearer：允许访问
2. 受保护路由：`/api/v1/auth/me`、`/api/v1/auth/logout`、`/api/v1/chat/*`
   - Bearer 合法：放行到对应 handler
   - Bearer 非法/过期/撤销：401
   - 无 Bearer：401

实现细节说明：

1. 中间件已经做了统一鉴权，但 `chat/history` handler 仍保留 `X-Principal-Sid` 为空时返回 401 的防御式检查。
2. 路由层错误响应统一走 `WriteJsonError`，保证错误结构一致（`ok/code/message/request_id`）。

##### 请求生命周期时序图（第三阶段）
```text
Client
  -> TCP连接进入 HttpServer
  -> 解析 HTTP 请求 (method/path/header/body)
  -> Middleware.before #1: RequestIdMiddleware
       - 生成 X-Request-Id 并写入 request/response
  -> Middleware.before #2: AuthMiddleware
       - 公共路由(/healthz,/auth/register,/auth/login)直接放行
       - 非公共路由:
           - 解析 Authorization: Bearer <token>
           - 缺失/非法 -> 401 直接返回（不进路由）
           - 调用 AuthService::AuthenticateBearerToken
           - 失败 -> 401/500 直接返回
           - 成功 -> 注入 X-Principal-Sid / X-Auth-User-Id 等头
  -> ServletDispatch 按 method + path 匹配路由
  -> AiHttpHandlers::HandleXXX
       - /auth/*: 调 AuthService (Register/Login/Logout/Me)
       - /chat/*: 构建请求 -> ChatService -> LlmClient -> 存储层
  -> 组装 JSON 或 SSE 响应
  -> sendResponse 发回客户端
  -> 连接保持/关闭（取决于 keep-alive/stream）
```

#### 6. 对话链路身份接入升级
`AiHttpHandlers::BuildChatRequest()` 与 `HandleHistory()` 已改为：

1. 仅从 `X-Principal-Sid` 读取主体 SID
2. 未携带该头直接返回 401

因此 chat/history 统一走“登录用户主体”调用链。

---

### 3. 测试与验证
本阶段已完成的验证如下：

1. 编译验证
   - `cmake -S . -B build -DBUILD_AI_CHAT_SERVER=ON`
   - `cmake --build build -j8 --target ai_chat_server`
   - 结果：通过

2. 回归单测
   - `cmake --build build -j8 --target test_ai_chat_service`
   - `ctest --test-dir build -R test_ai_chat_service --output-on-failure`
   - 结果：通过

3. 结构验证
   - `init_ai_chat.sql` 已包含 `users` / `auth_tokens` 建表语句
   - `main.cc` 已完成 request_id/auth 中间件装配，以及 auth 路由接入

说明：
- 本轮主要完成“第三阶段最小闭环”落地，端到端压测与专项安全测试留到后续阶段补强。

---

### 4. 当前实现与后续优化
#### 当前取舍
- 采用 Opaque token + DB 校验，优先可撤销与实现简单
- 密码策略先做长度与字符合法性基础校验，复杂度策略暂未增强
- 鉴权中间件先按路径规则控制，未引入细粒度 ACL/RBAC
- 非公共接口统一强制 Bearer，简化权限语义与路由行为

#### 已知风险点
1. 目前无登录失败限流/锁定机制，抗暴力破解能力有限
2. token 生命周期管理较基础（无 refresh token、多设备管理能力弱）
3. `/api/v1/auth/*` 路由目前未单独做安全审计日志落盘

#### 后续可优化方向
1. 引入 refresh token 与多端会话管理
2. 增加登录限流、失败惩罚、密码强度策略
3. 增加 token 活跃刷新与后台清理任务
4. 基于用户主体增加更细粒度权限控制

---

### 5. 阶段总结
第三阶段核心价值，是把“会话连续性”从 Cookie 维度提升到用户维度：

- 不破坏现有 chat API 协议，完成账号体系平滑接入
- 通过 `principal_sid` 把 chat/history 统一到单一鉴权调用链
- 移除游客访问分支，避免“有 token/无 token”双轨行为造成语义分叉
- 为第四阶段 RAG 的“用户级长期记忆”提供了稳定身份锚点

到这里，AI 应用层第三阶段主目标已经完成（最小闭环版本）。

### 6. 下一阶段
第四阶段建议优先推进（现已完成落地，详见下文第四阶段章节）：

1. 记忆检索增强（RAG/向量召回），以用户主体维度召回长期事实。
2. 写路径消息队列化（MQ）工程化设计收敛与迁移准备（按规划在第五阶段落地）。
3. 稳定性治理（限流、重试、熔断、降级）。
4. 可观测性（指标、日志、trace、告警）。

---

## 第四阶段：记忆检索增强（RAG/向量召回）

### 1. 阶段目标
第四阶段目标是把“只看当前会话短期上下文”的能力升级为“用户级长时记忆召回”：

1. 在不破坏现有 chat API 协议的前提下，补齐语义检索记忆能力。
2. 让同一登录用户在不同 `conversation_id` 下也能召回历史事实。
3. 保持主请求链路稳定，索引写入采用异步增量，不阻塞对话响应。

---

### 2. 代码改动总览
本阶段新增模块：`src/ai/rag/*`

1. `rag_http_client.*`
   - 统一封装 RAG 依赖服务（Ollama / Qdrant）的 HTTP 调用。
   - 复用 `FiberCurlSession`，保证网络 IO 走协程化调度。

2. `embedding_client.*`
   - 新增 `EmbeddingClient` 抽象。
   - 新增 `OllamaEmbeddingClient`（调用 `/api/embed`）。

3. `vector_store.*`
   - 新增 `VectorStore` 抽象。
   - 新增 `QdrantVectorStore`（建集合、Upsert、按 `sid` 过滤检索）。

4. `rag_retriever.*`
   - 负责“问题 -> embedding -> 向量检索 -> 返回命中片段”。

5. `rag_indexer.*`
   - 负责异步索引流水线：`PersistMessage -> embedding -> qdrant upsert`。

---

### 2.1 RAG 工具栈与职责
第四阶段这套 RAG 不是“单组件实现”，而是由多个工具协同完成。当前实际使用的工具如下：

1. Embedding 工具：`Ollama`
   - 作用：把文本（query / 历史消息）转换成向量。
   - 在调用链中的位置：
     - 检索时：`query -> embedding`
     - 建索引时：`message -> embedding`
   - 本项目接入点：
     - `OllamaEmbeddingClient` 调用 `/api/embed`。
   - 没有它时的影响：
     - 无法把文本映射到向量空间，检索和索引都无法进行。

2. 向量数据库：`Qdrant`
   - 作用：存储向量点并执行近邻检索（ANN）。
   - 在调用链中的位置：
     - 写路径：`Upsert(points)`
     - 读路径：`Search(query_vector, filter=sid, top_k, score_threshold)`
   - 本项目接入点：
     - `QdrantVectorStore` 封装建集合、Upsert、Search。
   - 没有它时的影响：
     - 只能做会话内记忆，无法做用户级跨会话语义召回。

3. 网络调用工具：`libcurl + FiberCurlSession`
   - 作用：承载 Ollama/Qdrant 的 HTTP 请求，并把网络 IO 挂到协程调度器上。
   - 在调用链中的位置：
     - `rag_http_client` 统一发 HTTP
     - 通过 `FiberCurlSession` 避免工作线程被阻塞
   - 设计价值：
     - RAG 读写链路与主服务协程模型一致，提升并发场景下的吞吐稳定性。

4. 检索编排工具：`RagRetriever`
   - 作用：把“自然语言问题”串成完整检索流程。
   - 流程：
     - 先用 EmbeddingClient 计算 query 向量
     - 再调用 VectorStore 按 `sid` 过滤检索
     - 返回命中片段给 `ChatService` 注入上下文
   - 角色定位：
     - 读路径编排器（query-time）。

5. 索引编排工具：`RagIndexer`
   - 作用：把“持久化消息”异步转成向量索引点。
   - 流程：
     - `PersistMessage` 入队
     - 后台批处理 embedding
     - 批量 upsert 到 Qdrant
   - 当前增强点：
     - 支持 assistant 选择性入库（`all/fact_like/none`）
     - 支持去重窗口（`dedup_ttl_ms + dedup_max_entries`）
   - 角色定位：
     - 写路径编排器（index-time）。

6. 业务拼装工具：`ChatService`
   - 作用：把 RAG 检索结果转成可注入 prompt 的 memory 消息。
   - 在调用链中的位置：
     - `BuildRagMemoryMessages()` 中执行召回并格式化片段
     - 再与 `summary + recent` 一起进入预算裁剪
   - 角色定位：
     - 上下文最终装配器（prompt assembly）。

#### 小结（工具协作关系）
1. `Ollama` 解决“文本 -> 向量”。
2. `Qdrant` 解决“向量存储 + 近邻检索”。
3. `libcurl + FiberCurlSession` 解决“协程化网络 IO”。
4. `RagRetriever` 负责读路径编排，`RagIndexer` 负责写路径编排。
5. `ChatService` 负责把召回结果真正纳入模型上下文。

### 2.2 核心类详解
#### 2.2.1 `rag_http_client`（RAG 通用 HTTP 适配层）
`rag_http_client` 是第四阶段 RAG 子系统最底层的网络适配器，核心目标是：

1. 对上层屏蔽 `libcurl` 细节（`EmbeddingClient` / `VectorStore` 不直接写 curl 代码）。
2. 统一 RAG 外部依赖（Ollama / Qdrant）的 HTTP 请求入口。
3. 复用 `FiberCurlSession`，使网络等待不阻塞 IO worker 线程。

##### 类与接口位置
1. 头文件：`src/ai/rag/rag_http_client.h`
2. 实现文件：`src/ai/rag/rag_http_client.cc`
3. 对外唯一函数：
   - `bool PerformHttpRequest(const HttpRequestOptions& options, long& http_status, std::string& response_body, std::string& error)`

##### `HttpRequestOptions` 参数语义（调用方视角）
1. `method`
   - HTTP 方法（`GET/POST/PUT/...`）。
   - `POST` 走 `CURLOPT_POST`，其他非 GET 方法走 `CURLOPT_CUSTOMREQUEST`。
2. `url`
   - 完整 URL（由上层拼好，例如 Ollama `/api/embed`、Qdrant `collections/...`）。
3. `headers`
   - 请求头字符串数组（如 `Content-Type: application/json`）。
4. `body`
   - 请求体字符串（当前主要是 JSON 序列化文本）。
5. `connect_timeout_ms` / `request_timeout_ms`
   - 分别控制建连超时与总超时。

##### `PerformHttpRequest` 执行链路
1. `EnsureCurlGlobalInit()`
   - 使用 `std::call_once` 做进程级 `curl_global_init`，避免重复初始化。
2. `curl_easy_init()`
   - 创建本次请求的 easy handle。
3. 组装请求
   - 填 URL、header、method、body、timeout、写回调。
4. 执行请求（关键点）
   - 不直接 `curl_easy_perform`。
   - 而是 `FiberCurlSession session(curl); code = session.Perform();`
   - 含义：当前协程在网络等待时挂起，IO 就绪后恢复，线程可去执行其他协程任务。
5. 收尾
   - 读取 `CURLINFO_RESPONSE_CODE` 到 `http_status`。
   - 释放 header 链表与 easy handle。
6. 返回语义
   - `code != CURLE_OK`：返回 `false`（网络/执行层失败，如超时、连接失败）。
   - `code == CURLE_OK`：返回 `true`，并把响应体返回给上层。
   - 注意：即便 HTTP 是 4xx/5xx，这里也可能返回 `true`，由上层按 `http_status` 做业务判定。

##### 错误边界与职责边界
1. `rag_http_client` 负责：
   - “请求是否发出去、网络是否成功、响应体是否拿到”。
2. `rag_http_client` 不负责：
   - 业务语义判断（例如 Qdrant 404 是否按空结果处理、409 是否按幂等成功）。
   - JSON 解析与字段校验（由 `embedding_client` / `vector_store` 处理）。
3. 这样分层的好处：
   - 传输层稳定复用；业务策略留在各自上层模块，不耦合。

##### 为什么它是第四阶段关键基础设施
1. RAG 的两个外部依赖（Ollama/Qdrant）都靠 HTTP 调用。
2. 若这里仍用阻塞式 `curl_easy_perform`，高并发时容易占死工作线程。
3. 使用 `FiberCurlSession` 后：
   - 网络等待期间会让出线程；
   - 与主服务协程调度模型一致；
   - RAG 检索/索引链路更容易与主链路并发共存。

##### 面试可用总结（30 秒版本）
在第四阶段里我们专门抽了 `rag_http_client` 做 RAG 的统一 HTTP 适配层。  
上层模块只关心“传什么、拿什么”，不关心 curl 细节。底层执行不走阻塞 `curl_easy_perform`，而是通过 `FiberCurlSession` 把网络等待挂到 `IOManager`，所以即使 RAG 访问 Ollama/Qdrant 有网络抖动，也不会长期占死工作线程。  
它把网络层职责与业务层职责拆开：网络成功/失败由 `rag_http_client` 负责，HTTP 状态语义和 JSON 解析由上层模块负责。

#### 2.2.2 `embedding_client`（文本向量化适配层）
`embedding_client` 位于 RAG 读写链路的共同入口，核心目标是：

1. 对上层统一暴露“文本 -> 向量”的稳定接口（`EmbeddingClient`）。
2. 把具体 embedding 服务差异（当前是 Ollama）封装在实现类内部。
3. 复用 `rag_http_client` 发 HTTP 请求，保持网络层逻辑一致。

##### 类与接口位置
1. 头文件：`src/ai/rag/embedding_client.h`
2. 实现文件：`src/ai/rag/embedding_client.cc`
3. 核心抽象：
   - `class EmbeddingClient`
   - `virtual bool Embed(const std::string& input, std::vector<float>& embedding, std::string& error) const = 0;`
4. 当前实现：
   - `class OllamaEmbeddingClient : public EmbeddingClient`

##### 配置项语义（`EmbeddingSettings`）
1. `base_url`
   - embedding 服务地址（如 `http://127.0.0.1:11434`）。
2. `model`
   - embedding 模型名（如 `mxbai-embed-large`）。
3. `connect_timeout_ms` / `request_timeout_ms`
   - 分别控制建连超时与总超时。

##### `BuildEmbedUrl()` 做了什么
1. 把 `base_url` 规范化为可请求的 `/api/embed` 完整地址。
2. 同时兼容两种配置形式：
   - `http://host:11434`
   - `http://host:11434/`
3. 未配置 `base_url` 时回退：
   - `http://127.0.0.1:11434/api/embed`

##### `Embed()` 执行链路
1. 输入校验
   - `input.empty()` 直接返回失败，避免无意义请求。
2. 构造请求体
   - JSON：`{"model": "...", "input": "..."}`
3. 构造 HTTP 请求参数
   - `method=POST`、`url=BuildEmbedUrl()`、`Content-Type: application/json`、超时。
4. 发请求
   - 调 `PerformHttpRequest(...)`（底层走 `FiberCurlSession` 协程化网络 IO）。
5. 校验状态码
   - 非 2xx 直接失败，并把 `status + body` 拼到 `error`。
6. 解析响应
   - 要求是合法 JSON，且不存在 `error` 字段异常。
7. 提取向量
   - 必须包含 `embeddings` 数组；
   - 取 `embeddings[0]` 并逐项转为 `float`；
   - 任意非数字值都判失败。

##### 返回与错误语义
1. `true`
   - 成功产出非空/可用向量，写入 `embedding`。
2. `false`
   - 输入非法、网络失败、HTTP 非 2xx、JSON 非法、字段缺失、向量元素类型错误。
3. `error`
   - 明确携带失败原因，便于上层记录日志和降级处理。

##### 在 RAG 中的调用位置
1. 检索读链路（query-time）
   - `RagRetriever::Retrieve()` 里先 `Embed(query)` 再向量检索。
2. 索引写链路（index-time）
   - `RagIndexer::FlushBatch()` 里对每条消息 `Embed(content)` 后 upsert。
3. 结论
   - `embedding_client` 是读写共用的向量化入口，属于 RAG 的“基础能力层”。

##### 面试可用总结（30 秒版本）
`embedding_client` 是我们 RAG 里的“文本向量化门面层”：上层只调用 `Embed()`，不用关心 Ollama 协议细节。  
实现上它把请求参数和响应校验做了完整收口（状态码、JSON 结构、数值类型），并通过 `rag_http_client` 复用协程化网络调用能力。  
这样无论是检索链路还是索引链路，都能用同一套向量化接口，便于后续替换 embedding 服务提供方。

#### 2.2.3 `vector_store`（向量存储与检索适配层）
`vector_store` 是 RAG 中“向量落库 + 语义检索”的协议适配层，核心目标是：

1. 对上层统一暴露存储接口（`EnsureCollection / Upsert / Search`）。
2. 屏蔽 Qdrant HTTP/JSON 协议细节。
3. 把检索边界控制在“用户主体 SID”范围内，避免跨用户召回污染。

##### 类与接口位置
1. 头文件：`src/ai/rag/vector_store.h`
2. 实现文件：`src/ai/rag/vector_store.cc`
3. 核心抽象：
   - `class VectorStore`
   - `EnsureCollection(...) / Upsert(...) / Search(...)`
4. 当前实现：
   - `class QdrantVectorStore : public VectorStore`

##### 配置与数据结构语义
1. `VectorStoreSettings`
   - `base_url`：Qdrant 地址（默认回退 `http://127.0.0.1:6333`）。
   - `collection`：集合名。
   - `request_timeout_ms`：请求超时。
2. `MemoryPayload`
   - 业务元数据：`sid / conversation_id / role / content / created_at_ms`。
3. `VectorPoint`
   - 写入点：`id + vector + payload`。
4. `SearchHit`
   - 命中结果：`id + score + payload`。

##### `EnsureCollection()` 执行链路（建集合）
1. 校验 `vector_size > 0`。
2. 组装 Qdrant 请求体：
   - `vectors.size = vector_size`
   - `vectors.distance = Cosine`
3. 请求：
   - `PUT /collections/{collection}`
4. 状态码策略：
   - `2xx`：成功
   - `409`：已存在，也按成功处理（幂等）
   - 其他：失败并带上响应体错误信息

##### `Upsert()` 执行链路（批量写入）
1. 空点集直接成功返回（避免无意义请求）。
2. 组装 points 数组：
   - 每个点包含 `id/vector/payload`。
3. 请求：
   - `PUT /collections/{collection}/points?wait=false`
4. 状态码策略：
   - `2xx`：成功
   - 其他：失败并返回 `qdrant upsert http status ...`

##### `Search()` 执行链路（核心）
1. 输入保护
   - `sid` 空、`query` 空、`top_k=0` 时直接返回空结果成功。
2. 构造检索请求
   - `vector = query`
   - `limit = top_k`
   - `with_payload = true`
   - `filter.must` 加 `sid` 精确匹配（用户隔离关键点）
   - 可选 `score_threshold`（>0 才携带）
3. 发请求
   - `POST /collections/{collection}/points/search`
4. 状态码策略
   - `404`：集合不存在，按空结果成功（fail-open）
   - `2xx`：继续解析
   - 其他：失败
5. 结果解析
   - 解析 `result[]`，逐项抽取 `id/score/payload`。
   - 额外本地二次阈值过滤：`score < threshold` 再次剔除。
   - `payload.content` 为空的命中剔除（避免无意义注入）。
6. 输出
   - 产出 `std::vector<SearchHit>` 给上层 `RagRetriever/ChatService` 使用。

##### 返回与错误语义
1. `true`
   - 包括“成功但无结果”（例如空入参、404、命中为空）。
2. `false`
   - 网络失败、HTTP 非预期、JSON 非法等。
3. 设计意图
   - 尽量不因“无检索结果”阻断主对话链路；
   - 仅在“真实故障”时返回失败。

##### 为什么它是第四阶段关键基础设施
1. 它是“长期记忆”的真正落点（索引写入）与读点（语义召回）。
2. `sid` 过滤是在该层固化的，保证多用户隔离。
3. `409/404` 的幂等与 fail-open 策略，显著提升了工程可用性。

##### 面试可用总结（30 秒版本）
`vector_store` 是 RAG 的向量存储门面层：对上游只暴露 `EnsureCollection/Upsert/Search` 三个稳定接口，对下游封装 Qdrant 的 HTTP/JSON 协议细节。  
它在检索时强制带 `sid` 过滤，确保召回结果不会跨用户串数据；在工程策略上把 `409 已存在` 和 `404 集合缺失` 做成幂等/降级处理，尽量保证主聊天链路可用。  
因此它既承担“向量能力落地”，又承担“可用性与隔离性边界”的实现责任。

#### 2.2.4 `rag_retriever`（检索编排层）
`rag_retriever` 负责把“自然语言问题”串成完整语义检索链路，核心目标是：

1. 把 query 先向量化（调用 `EmbeddingClient`）。
2. 再按 `sid` 边界做向量检索（调用 `VectorStore`）。
3. 向上游返回结构化命中结果（`SearchHit`），不关心 prompt 拼装细节。

##### 类与接口位置
1. 头文件：`src/ai/rag/rag_retriever.h`
2. 实现文件：`src/ai/rag/rag_retriever.cc`
3. 核心接口：
   - `bool Retrieve(const std::string& sid, const std::string& query, size_t top_k, double score_threshold, std::vector<SearchHit>& out, std::string& error) const`

##### `Retrieve()` 执行链路（核心）
1. 初始化输出
   - 先 `out.clear()`，避免调用方读到脏数据。
2. 输入短路
   - `sid/query/top_k` 任一无效时，返回“成功但无结果”。
3. 依赖检查
   - `EmbeddingClient` / `VectorStore` 任一未注入则返回失败。
4. 文本向量化
   - `Embed(query, query_embedding, error)`。
5. 向量检索
   - `Search(sid, query_embedding, top_k, score_threshold, out, error)`。
6. 返回
   - 上游拿到的是纯检索结果，不带格式化文本。

##### 为什么要单独抽 `rag_retriever`
1. 把“检索编排逻辑”从 `ChatService` 解耦出来，业务层不直接操作 embedding/vector store 细节。
2. 后续若替换向量库或 embedding 提供方，只要接口兼容，`ChatService` 基本不改。
3. 检索策略（例如是否触发、top_k、阈值）可在上层配置，检索执行留在本层收口。

##### 在主调用链中的位置
1. 入口调用点：
   - `ChatService::BuildRagMemoryMessages()` 中调用 `m_rag_retriever->Retrieve(...)`。
2. 上游职责：
   - `ChatService` 负责“何时召回、如何拼成 memory prompt、如何进预算裁剪”。
3. 本层职责：
   - 仅负责“把 query 转成 hits”，不做内容拼装策略。

##### 返回与错误语义
1. `true`
   - 检索执行成功（包括空结果）。
2. `false`
   - 依赖缺失、embedding 失败、vector search 失败。
3. 工程取舍
   - “空结果”是业务常态，不应作为错误；
   - “链路故障”才作为失败向上传递。

##### 面试可用总结（30 秒版本）
`rag_retriever` 是我们 RAG 的读路径编排器：它只做两件事——先把 query 向量化，再按 `sid` 去向量库检索，最后返回结构化命中结果。  
这样 `ChatService` 不需要关心 embedding 和向量库协议细节，只负责策略与上下文拼装。  
这种分层让系统在换 embedding 模型、换向量库时改动范围更小，也更方便做检索链路的单测和故障定位。

#### 2.2.5 `rag_indexer`（异步索引编排层）
`rag_indexer` 是第四阶段写路径的核心：把“持久化消息事件”异步转换为向量点并写入 Qdrant。核心目标是：

1. 不阻塞主请求链路（异步后台线程处理）。
2. 对消息做筛选与去重，减少无效向量写入。
3. 统一“消息 -> embedding -> upsert”流水线，形成稳定索引能力。

##### 类与接口位置
1. 头文件：`src/ai/rag/rag_indexer.h`
2. 实现文件：`src/ai/rag/rag_indexer.cc`
3. 入口接口：
   - `Start(error)`：启动后台索引线程。
   - `Enqueue(message, error)`：投递索引任务。
   - `Stop()`：停止线程并收尾。

##### 配置项语义（`RagIndexerSettings`）
1. `queue_capacity`
   - 入队上限，防止内存无限增长。
2. `batch_size`
   - 每次 flush 最大处理条数。
3. `flush_interval_ms`
   - 空闲等待周期（无新消息时定时唤醒）。
4. `assistant_index_mode`
   - `all / fact_like / none`，控制 assistant 消息是否索引。
5. `assistant_min_chars`
   - `fact_like` 模式下最小长度门槛。
6. `dedup_ttl_ms / dedup_max_entries`
   - 去重窗口与缓存容量控制。

##### 生命周期：`Start -> Enqueue -> Run -> FlushBatch -> Stop`
1. `Start()`
   - 校验依赖（embedding/vector store）与配置有效性。
   - 设置 `m_running=true`，启动后台线程执行 `Run()`。
2. `Enqueue()`
   - 先做消息筛选（`ShouldIndexMessage`）。
   - 再做去重判断（`ShouldSkipByDedup`）。
   - 队列满则返回错误；否则入队并 `notify_one`。
3. `Run()`
   - 后台循环按 `batch_size` 拉取任务。
   - 队列空时 `wait_for(flush_interval_ms)`。
   - 每批调用 `FlushBatch()`；单批失败只记日志，不让线程退出。
4. `FlushBatch()`
   - 对每条消息执行 `Embed(content)`。
   - 首次成功 embedding 时 `EnsureCollection(vector_dim)` 并缓存维度。
   - 维度不一致的点直接跳过。
   - 最后批量 `Upsert(points)` 写入 Qdrant。
5. `Stop()`
   - 设置 `m_running=false`，唤醒等待线程，`join` 回收线程。

##### 消息筛选策略（为什么能减少无效索引）
1. `user` 消息默认入索引。
2. `assistant` 消息按模式控制：
   - `none`：不索引 assistant；
   - `all`：全部索引；
   - `fact_like`：只索引“信息密度高”的回答。
3. `fact_like` 判定信号：
   - 噪声短语过滤（寒暄模板）；
   - 事实信号词命中（配置/接口/代码块/技术词）；
   - 包含数字或列表结构时倾向保留。

##### 去重策略（`ShouldSkipByDedup`）
1. 归一化文本：小写 + 空白折叠。
2. 去重维度：`sid + normalized_content`。
3. 哈希缓存：
   - `unordered_map` 记录最近出现时间；
   - `deque` 维护顺序用于 TTL/容量淘汰。
4. 命中 TTL 窗口内重复内容：
   - 直接跳过，不重复写向量库。

##### 与 `ChatService` 的对接点
1. 调用入口：
   - `ChatService::PersistMessage()` 中，先入异步 MySQL 写队列，再尝试 `m_rag_indexer->Enqueue(...)`。
2. 失败语义：
   - RAG 入队失败只打 `WARN`，不阻断主对话成功路径（fail-open）。
3. 工程意义：
   - 主链路可用性优先，RAG 是增强能力，不是硬依赖。

##### 返回与错误语义
1. `Enqueue` 返回 `true`
   - 代表“入队成功”或“被策略性跳过”。
2. `Enqueue` 返回 `false`
   - 仅在索引器未运行或队列已满等真实异常场景。
3. `Run/FlushBatch`
   - 单批失败记录日志，后续批次继续处理，避免线程自杀。

##### 面试可用总结（30 秒版本）
`rag_indexer` 是我们 RAG 写路径的异步编排器：主线程只负责把消息投递进去，后台线程按批做 embedding 并 upsert 到 Qdrant。  
它做了三层治理：消息筛选（减少低价值 assistant 文本）、去重（sid + 归一化文本 + TTL）、以及批处理（控制吞吐与资源）。  
并且我们采用 fail-open 策略：索引失败不影响主对话链路，保障可用性优先。

---

### 3. 当前上下文策略（重点）
第四阶段后，发送给 LLM 的上下文策略从“摘要 + 最近窗口”升级为：

`system_prompt + summary + recent + semantic_recall + 当前用户消息`

其中每个部分的职责与实现细节如下：

#### 3.0.1 `system_prompt`（全局行为约束层）
1. 数据来源：
   - 配置项 `ai.chat.system_prompt`。
2. 触发条件：
   - 非空才注入；为空则跳过。
3. 注入位置：
   - 固定放在请求最前面，优先级最高。
4. 作用边界：
   - 用于定义回复风格、边界、安全规则等“全局规则”。
   - 不承担记忆功能，不参与摘要更新和向量检索。
5. 预算关系：
   - 作为请求消息的一部分，会占用模型上下文窗口（虽然当前预算函数主要对 `summary/recent/recall` 做裁剪）。

#### 3.0.2 `summary`（会话长期压缩记忆层）
1. 数据来源：
   - 当前会话 `conversations.summary_text`（持久化层）+ 内存镜像。
2. 触发更新时机：
   - 当会话消息超过 `recent_window_messages`，且估算 token 超过 `summary_trigger_tokens` 时触发。
3. 生成机制：
   - 由 `MaybeRefreshSummary()` 额外发起一次 LLM 调用生成/更新摘要。
   - 输入由“现有摘要 + 旧对话片段”构成，输出新摘要文本。
4. 更新后处理：
   - 内存中的 `context.summary` 与 `summary_updated_at_ms` 立即更新。
   - 旧消息会被裁剪，仅保留最近 `recent_window_messages` 条原文。
   - 新摘要写回数据库，后续冷启动可恢复。
5. 核心价值：
   - 以较小 token 成本保留“人物偏好、事实、约束、未完成任务”等高价值信息。
6. 当前取舍：
   - 摘要质量依赖模型；这是当前策略里唯一会额外消耗一次模型调用的环节。

#### 3.0.3 `recent`（会话短期连续性层）
1. 数据来源：
   - 首选内存上下文 `m_contexts[sid#conversation_id].messages`。
   - 若未命中或冷启动，`EnsureContextLoaded()` 从存储层加载 `history_load_limit` 条 recent 消息补齐。
2. 更新方式：
   - 每次对话成功后，`AppendContextMessages()` 追加 user/assistant 两条消息。
3. 长度控制：
   - 内存消息上限由 `max_context_messages` 控制，超过后从最旧端裁剪。
4. 核心价值：
   - 保证“刚刚说了什么”不丢失，是连贯多轮对话的主要来源。
5. 与 `summary` 的分工：
   - `recent` 保留原文细节，`summary` 保留跨窗口的长期高价值事实。

#### 3.0.4 `semantic_recall`（用户级跨会话记忆层）
1. 数据来源：
   - `RagRetriever` 基于当前问题做 embedding，并在 Qdrant 里检索历史向量。
   - 检索以 `sid` 为过滤条件，实现“同一用户跨会话召回”。
2. 触发策略（当前实现）：
   - `ai.rag.recall_trigger_mode = always`：每轮请求都尝试召回。
   - `ai.rag.recall_trigger_mode = intent`：仅当用户问题命中“历史/记忆意图”时触发召回（默认）。
   - `intent` 模式下，还要求问题长度达到 `ai.rag.recall_intent_min_chars`。
3. 召回参数：
   - `top_k`：最多召回条数。
   - `score_threshold`：最低相似度阈值。
   - `max_snippet_chars`：单条片段字符截断上限。
4. 入模形态：
   - 多条命中会被整理为一条 `system` memory 消息，包含 `score/conv/role/ts/snippet`。
5. 降级行为：
   - 检索失败或无命中时，直接返回空，不阻断主对话链路（fail-open）。
6. 核心价值：
   - 补足 `summary + recent` 的会话内局限，支持跨 `conversation_id` 的长期事实复用。

#### 3.1 详细执行顺序（`ChatService`）
以 `Complete/StreamComplete` 为例：

1. 入口参数校验：
   - 校验 `sid`（若配置要求）和 `message` 非空。
   - 生成或沿用 `conversation_id`。
2. 会话上下文预热：
   - `EnsureContextLoaded(sid, conversation_id)`。
   - 同步得到 `summary + recent` 的内存快照。
3. 组装当前轮输入：
   - 构造 `user_message`。
   - 若配置了 `system_prompt`，先入请求队列。
4. 条件触发跨会话检索：
   - `BuildRagMemoryMessages(sid, user_message)` 先判断是否满足 recall 触发策略。
   - 满足时执行 embedding + 向量检索，得到 0 或 1 条 memory system 消息。
   - 不满足时直接跳过本轮 recall。
5. 预算裁剪：
   - `BuildBudgetedContextMessages(context, rag_memory, user_message)`。
   - 在 token 预算内选择可带入本轮的 `summary/recent/recall`。
6. 最终消息拼装：
   - `system_prompt`（可选） + 预算后的上下文 + `user_message`。
7. 调用模型：
   - `Complete`（同步）或 `StreamComplete`（流式）。
8. 写后动作（不阻塞主推理链）：
   - user/assistant 消息入异步持久化队列。
   - 同时入 `RagIndexer` 索引队列，后台做 embedding/upsert。
9. 回写会话态：
   - 把新消息 append 到内存上下文。
   - 视阈值触发摘要刷新（可能额外发起一次摘要模型请求）。

#### 3.2 预算裁剪规则（当前实现）
预算函数关键规则如下：

1. 先做用户输入预留：
   - `reserve_user_tokens = estimate(user_message) + 16`。
   - 目的：确保当前用户问题一定有空间进入模型。
2. 计算可用预算：
   - `budget = max_context_tokens - reserve_user_tokens`。
3. `summary` 预保留（新逻辑）：
   - 若存在摘要，先计算 `summary_tokens`。
   - 仅当 `summary_tokens <= budget` 时，先把 `summary` 放入结果并扣减预算。
   - 这一步解决了“摘要在逆序裁剪里被最先丢弃”的问题。
4. 构造剩余候选池：
   - 当前会话 `recent` 消息序列
   - `semantic_recall` memory 消息
5. 剩余预算逆序挑选：
   - 从候选池末尾开始挑选，优先保留“离当前轮最近”的信息。
   - 若某条加入后会超预算，则跳过该条，继续尝试更早消息。
6. 结果整理：
   - 候选池挑中的消息先反转回正序，再拼接到 `summary` 后。
7. 启发式估算说明：
   - `EstimateMessageTokens()` 采用字节近似估算，并非模型官方 tokenizer 精确值。
   - 当前策略目标是工程可控与稳定，而非 token 计算绝对精确。
8. 边界条件说明：
   - 如果摘要本身已经超过 `budget`，当前实现会放弃该摘要，避免请求超出总预算。
   - 该场景通常较少出现（摘要本身一般比原始历史短很多）。

#### 3.3 和第二阶段策略的关系
不是替换，而是增强：

1. 第二阶段核心（`summary + recent + token budget`）仍保留。
2. 第四阶段在预算体系内额外加入 `semantic_recall`。
3. 摘要与最近窗口继续承担“当前会话连续性”，语义召回承担“跨会话长期事实”。

---

### 4. RAG 数据流与异步索引
#### 4.1 写路径（索引）
当前写路径有两条并行异步链：

1. 业务持久化链：`AsyncMySqlWriter` 入库。
2. 记忆索引链：`RagIndexer` 入向量库。

`RagIndexer` 规则：

1. `user` 消息默认入索引；`assistant` 消息按模式选择性入索引：
   - `assistant_index_mode = all`：assistant 全量入索引
   - `assistant_index_mode = fact_like`：仅“事实型/配置型/参数型”assistant文本入索引（默认）
   - `assistant_index_mode = none`：assistant 全不入索引
2. 去重保护（新增）：
   - 基于 `sid + 规范化content` 做去重哈希
   - 在 `dedup_ttl_ms` 窗口内重复内容跳过 embedding/upsert
   - 用 `dedup_max_entries` 控制去重缓存内存上限
3. 消息入队后后台线程批量处理。
4. 每条通过筛选的消息执行 embedding。
5. 首次成功 embedding 后按向量维度建/校验 Qdrant collection。
6. Upsert 点位 payload：`sid / conversation_id / role / content / created_at_ms`。

#### 4.2 读路径（检索）
检索调用链：

1. `RagRetriever::Retrieve(...)`
2. `EmbeddingClient::Embed(query)`
3. `QdrantVectorStore::Search(sid, query_vector, top_k, score_threshold)`
4. `ChatService` 把命中片段序列化成 memory system 消息注入 prompt

---

### 5. 配置项（第四阶段新增）
新增配置段：

1. `ai.rag.*`
   - `enabled`
   - `recall_trigger_mode`（`always` / `intent`）
   - `recall_intent_min_chars`
   - `top_k`
   - `score_threshold`
   - `max_snippet_chars`

2. `ai.embedding.*`
   - `provider`（当前仅 `ollama`）
   - `base_url`
   - `model`
   - `connect_timeout_ms`
   - `request_timeout_ms`

3. `ai.vector_store.qdrant.*`
   - `base_url`
   - `collection`
   - `request_timeout_ms`

4. `ai.rag_indexer.*`
   - `queue_capacity`
   - `batch_size`
   - `flush_interval_ms`
   - `assistant_index_mode`（`all` / `fact_like` / `none`）
   - `assistant_min_chars`
   - `dedup_ttl_ms`
   - `dedup_max_entries`

启动期校验已补齐：字段为空、数值非法会 fail-fast。

---

### 6. 启动装配与降级策略
`main.cc` 中新增 RAG 装配流程：

1. 读取 RAG/Embedding/Qdrant/Indexer 配置。
2. 构建 `OllamaEmbeddingClient + QdrantVectorStore + RagRetriever + RagIndexer`。
3. 启动探测：
   - 用探测文本做一次 embedding
   - 按向量维度 EnsureCollection
   - 启动 `RagIndexer` 线程
4. 任一步失败则：
   - 记录 warning
   - 自动关闭 RAG（fail-open）
   - 主服务继续可用（退化为无 RAG）

---

### 7. 测试与验证
本阶段已完成的验证：

1. 编译验证
   - `cmake --build build --target ai_chat_server -j8`
   - 结果：通过

2. 本地自动化单测（新增）
   - `cmake --build build --target test_rag_indexer -j8`
   - `./bin/test_rag_indexer`
   - 覆盖点：
     - 重复 user 消息在去重窗口内只索引一次
     - `assistant_index_mode=fact_like` 下会过滤寒暄类 assistant 消息
     - `assistant_index_mode=all` 下会放行 assistant 消息
   - 结果：通过（`test_rag_indexer ok`）

3. 启动烟测
   - 服务启动成功，`/api/v1/healthz` 返回 `ok=true`

4. 端到端记忆验证（登录用户跨会话）
   - 会话 A：输入“记住我最喜欢的编程语言是 C++”
   - 会话 B：提问“我最喜欢的编程语言是什么你还记得吗”
   - 结果：成功召回 `C++`

5. 向量库状态验证
   - `GET /collections/chat_memory`
   - `points_count` 增长，说明异步索引写入生效

---

### 8. 当前取舍与已知限制（第四阶段）
#### 当前取舍
1. 先走单路语义召回（embedding + 向量检索），不做多路混合检索。
2. 先不引入 reranker，优先保证链路最小闭环。
3. 召回片段以 `system` memory 文本注入，先保证可解释与可观测。
4. 保持 fail-open：RAG 异常不阻断主对话服务。

#### 已知限制
1. 召回质量仍是 V1：暂无关键词检索融合、重排模型、冲突消解。
2. 暂无离线回填工具：历史数据索引主要依赖增量写入后逐步累积。
3. `assistant=fact_like` 仍是启发式规则，存在误收/漏收概率，后续可升级为结构化事实抽取。
4. 暂无 RAG 专项指标（召回命中率、误召回率、延迟分位）。

---

### 9. 阶段总结
第四阶段把上下文能力从“会话内短时连续”扩展到了“用户级跨会话长时记忆”：

1. 架构上完成了本地 RAG 子系统闭环（Embedding / VectorStore / Retriever / Indexer）。
2. 业务上完成了上下文策略升级：`summary + recent + semantic_recall`。
3. 工程上保持了稳定性：索引异步、启动可降级、主链路可持续服务。

到这里，第四阶段主目标已完成（可用版闭环）。

### 10. 下一阶段
第五阶段建议优先推进：

1. 多 Provider 路由与注册式工厂（请求级模型通道选择）。
2. provider_id 级协议无关 Key 池与故障切换（OpenAI + Anthropic 对齐）。
3. 写路径消息队列化（MQ）工程化落地。

---

## 第五阶段：多 Provider 路由与注册式工厂（含协议无关 provider_id 级 Key 池）

### 1. 阶段目标
第五阶段从“单 Provider 启动模式”升级为“单实例多 Provider 并存模式”，核心目标：

1. 同一个 `ai_chat_server` 进程同时接入多个上游 Provider。
2. 请求级路由：每次请求按 `provider/model` 动态选择客户端。
3. 引入协议无关 key 池容灾能力（OpenAI/Anthropic 都可按需启用）。
4. key 池按 provider 实例维度隔离（`provider_id`）。

---

### 2. 架构升级（设计模式）
#### 2.1 Strategy（策略模式）
保留 `LlmClient` 抽象，具体协议实现继续由策略类承担：

1. `OpenAICompatibleClient`
2. `AnthropicClient`

业务层不感知协议细节，只依赖统一接口。

#### 2.2 Factory + Registry（注册式工厂）
新增注册工厂体系：

1. `LlmClientFactory`
   - 以 `provider.type` 为键注册构造器。
   - 根据类型创建具体客户端实例。
2. `LlmClientRegistry`
   - 以 `provider.id` 存储已创建客户端。
   - 对外提供按 `provider.id` 查找能力。

#### 2.3 Router（请求分发）
新增 `LlmRouter`，路由优先级：

1. 请求显式 `provider`（最高优先级）
2. `model -> provider_id` 映射
3. `default_provider_id`

这样可以做到“启动一次，运行期每个请求走不同 Provider”。

---

### 3. 配置体系重构（`ai.provider.type` -> `ai.llm.*`）
#### 3.1 新配置结构
将单 Provider 配置切换为多 Provider：

1. `ai.llm.providers[]`
   - 每项包含 `id/type/enabled/default_model`
   - OpenAI-Compatible 项包含：`base_url/api_key/timeout/key_pool`
   - Anthropic 项包含：`base_url/api_key/api_version/timeout/key_pool`
2. `ai.llm.routing.default_provider_id`
3. `ai.llm.routing.model_map`

#### 3.2 启动校验升级
`Validate()` 新增多 Provider 规则：

1. providers 不能为空，且至少有一个 enabled。
2. provider.id 唯一且非空。
3. provider.type 必须是 `openai_compatible` 或 `anthropic`。
4. `default_provider_id` 必须指向 enabled provider。
5. `model_map` 里的 provider_id 必须有效且可用。
6. 各 provider 的协议字段与 key 池字段分别校验。

---

### 4. 主流程装配改造（`main.cc`）
主线程启动装配链路改为：

1. 读取 `LlmSettings`（providers + routing）。
2. 若存在任意启用 `key_pool` 的 provider，初始化 `ApiKeyPoolRepository`。
3. 遍历 `providers[]`：
   - 根据 `provider.type` 通过 `LlmClientFactory` 创建客户端
   - provider 若启用 key_pool，则创建 `ProviderKeyPool(provider.id)` 并注入
   - 注册到 `LlmClientRegistry`
4. 基于 registry + routing 构建 `LlmRouter`。
5. 将 `LlmRouter` 注入 `ChatService`。
6. 停机时遍历停止全部 `ProviderKeyPool` 实例。

工程效果：

1. 主流程不再写死 `if (provider == xxx) new Client`。
2. Provider 装配具备可扩展性（新增协议只需注册工厂）。

---

### 5. 请求链路改造（HTTP -> Service -> LLM）
#### 5.1 HTTP 层
`BuildChatRequest()` 新增可选字段：

1. `provider`
2. `model`（保留）

响应中新增 `provider` 回传，便于调试和观测实际命中的上游。

#### 5.2 ChatService 层
`ChatService` 从“单 `m_llm_client`”改为“`m_llm_router`”：

1. 每次请求先调用 router 得到 `route_result`（provider_id/model/client）。
2. 同步与流式分别调用 `route_result.client`。
3. 响应对象回填 `provider`。
4. 摘要调用 `MaybeRefreshSummary` 时复用当前路由命中的 client。

---

### 6. 协议无关 API Key 池设计（provider_id 级隔离）
#### 6.1 存储与查询语义
`ApiKeyPoolRepository` 读取条件采用 `provider_id` 维度：

1. `LoadEnabledKeys(provider_id, ...)`
2. `ApiKeyRecord.provider_id`

`llm_api_keys.provider` 字段在语义上改为“provider_id”。

#### 6.2 运行时池隔离
新增统一抽象与实现：

1. `ApiKeyProvider`：协议无关候选 key 获取/成功失败上报接口。
2. `ProviderKeyPool`：通用运行时 key 池，实现热加载、优先级+权重选择、冷却与状态回写。

`ProviderKeyPool` 构造参数采用 `provider_id`：

1. 每个 provider 对应一个独立 key 池实例（不限协议）。
2. 冷却、失败计数、重试候选都在 provider 内隔离。
3. 不同 provider 之间不会互相抢 key、污染状态。
4. `OpenAICompatibleClient` 与 `AnthropicClient` 都通过同一接口接入重试与切 key。

---

### 7. 联调客户端增强
`ai_auth_chat_client` 增加 provider 指定能力：

1. 启动参数：`--provider <id>`
2. 交互命令：`/provider <id>`
3. 发送 payload 自动带 `provider` 字段（可选）

这样可以直接验证同一服务实例下多 provider 的请求级切换。

---

### 8. 本阶段验证
已完成：

1. 编译验证
   - `cmake --build build --target ai_chat_server ai_auth_chat_client -j8`
   - 结果：通过
2. 静态链路验证
   - `main -> factory -> registry -> router -> chat_service` 接线完整
   - 通用 key 池按 `provider.id` 创建并在停机阶段逐个释放
3. 协议层验证
   - HTTP 请求可选携带 `provider`
   - 响应可回显实际 `provider`
4. 运行时冒烟验证
   - `timeout 12 ./bin/ai_chat_server -c ./src/ai/config/ai_server.example.yml`
   - `curl http://127.0.0.1:8080/api/v1/healthz` 返回 `{"ok":true,...}`

---

### 9. 阶段总结
第五阶段完成了从“单协议单实例”到“多协议多实例路由”的架构跃迁：

1. 把 Provider 选择从“启动时决策”下沉到“请求时决策”。
2. 用注册式工厂收敛了协议扩展点，降低 main 装配耦合。
3. 将 key 池从 OpenAI 特化抽象为协议无关实现，并与 Anthropic 对齐。
4. key 池按 provider_id 隔离，避免跨通道状态污染。
5. 为后续新增协议（如 Gemini）保留统一扩展位：新增 Client + Factory 注册即可复用现有 key 池机制。

### 10. 下一阶段
第六阶段建议优先推进：

1. 写路径消息队列化（MQ）：主服务只读库 + 生产消息。
2. 由独立消费者进程批量落库，支持失败重放与削峰。
3. 收敛最终的数据一致性与重试语义。

---

## 后续阶段（规划）
- 第六阶段：写路径消息队列化（MQ），将主服务进程内异步写入升级为外部 MQ（生产者-消费者），实现写路径解耦、削峰与失败重放；主服务进程只负责读库 + 生产消息，消费者侧批量落库并独立维护连接池。
- 第七阶段：稳定性治理（限流、重试、熔断、降级）。
- 第八阶段：可观测性（指标、日志、trace、告警）。
- 第九阶段：运维发布（灰度、配置分层、备份恢复）。
