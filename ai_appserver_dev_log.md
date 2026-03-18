# AI 应用层开发笔记

## 项目目标（持续更新）
- 作为 AI 应用层的阶段化开发日志，持续记录从 V1 最小通路到后续生产化能力建设的全过程。
- 每个阶段固定记录：目标、实现细节、关键设计取舍、踩坑与复盘。
- 当前进度：第一阶段（最小通路）、第二阶段（Provider 切换 + 上下文摘要 + 并发能力增强）、第三阶段（用户体系与鉴权）已完成。

---

## 当前范围声明
这份笔记是多阶段持续维护文档，不只记录第一阶段。

- 已落地并已详细展开：第一阶段、第二阶段、第三阶段。
- 已列出但未展开实现细节：第四阶段及之后。
- 当前未实现能力：RAG 检索增强、写路径消息队列化、限流熔断、观测告警、发布治理等。
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

#### 3. 存储层新增账号与令牌仓库
`AuthRepository` 提供以下能力：

1. 用户创建与查询
2. 令牌保存、查询、撤销

新增数据表：

1. `users`
2. `auth_tokens`

#### 4. 认证业务服务落地（`AuthService`）
实现接口：

1. `Register`
2. `Login`
3. `AuthenticateBearerToken`
4. `Logout`

安全策略（当前实现）：

1. 密码哈希：`PBKDF2-HMAC-SHA256 + 随机 salt`
2. token 形态：opaque token（随机 32 bytes -> hex）
3. 数据库存储：仅存 `token_hash`（SHA256），不存 token 明文
4. 鉴权校验：检查“存在 + 未过期 + 未撤销 + 用户状态有效”

#### 5. API 路由扩展与鉴权中间件
新增路由：

1. `POST /api/v1/auth/register`
2. `POST /api/v1/auth/login`
3. `POST /api/v1/auth/logout`
4. `GET /api/v1/auth/me`

中间件策略：

1. 当前中间件清单（均在 `src/ai/middleware`）：
   - `RequestIdMiddleware`：前置生成并注入 `X-Request-Id`
   - `AuthMiddleware`：前置鉴权、注入 `X-Principal-Sid` 等身份头
2. 公共路由（不鉴权）：
   - `/api/v1/healthz`
   - `/api/v1/auth/register`
   - `/api/v1/auth/login`
3. 强制鉴权路由：
   - `/api/v1/auth/me`
   - `/api/v1/auth/logout`
4. 业务路由（chat/history）：
   - 有 Bearer 且合法 -> 走用户主体
   - 有 Bearer 但非法/过期/撤销 -> 401
   - 无 Bearer -> 401（强制登录）

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
第四阶段建议优先推进：

1. 记忆检索增强（RAG/向量召回），以用户主体维度召回长期事实。
2. 写路径消息队列化（MQ）工程化设计收敛与迁移准备（按规划在第五阶段落地）。
3. 稳定性治理（限流、重试、熔断、降级）。
4. 可观测性（指标、日志、trace、告警）。

---

## 后续阶段（规划）
- 第四阶段：记忆检索增强（RAG/向量召回），在 summary + recent 之外按语义召回历史关键事实。
- 第五阶段：写路径消息队列化（MQ），将主服务进程内异步写入升级为外部 MQ（生产者-消费者），实现写路径解耦、削峰与失败重放；主服务进程只负责读库 + 生产消息，消费者侧批量落库并独立维护连接池。
- 第六阶段：稳定性治理（限流、重试、熔断、降级）。
- 第七阶段：可观测性（指标、日志、trace、告警）。
- 第八阶段：运维发布（灰度、配置分层、备份恢复）。
