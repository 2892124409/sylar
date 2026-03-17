# AI 应用层开发笔记

## 项目目标（持续更新）
- 作为 AI 应用层的阶段化开发日志，持续记录从 V1 最小通路到后续生产化能力建设的全过程。
- 每个阶段固定记录：目标、实现细节、关键设计取舍、踩坑与复盘。
- 当前进度：第一阶段（最小通路）已完成，后续阶段按迭代继续补充到同一文档中。

---

## 当前范围声明
这份笔记是多阶段持续维护文档，不只记录第一阶段。

- 已落地并已详细展开：第一阶段（最小通路）。
- 已列出但未展开实现细节：后续阶段（会在落地后继续补写实现记录）。
- 当前未实现能力：用户账户体系、鉴权授权、限流熔断、观测告警、发布治理等。
- 当前 V1 运行时主通路：`OpenAICompatibleClient`；`AnthropicClient` 已落代码但尚未接入“按配置切换 provider”。

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
- `ExtractSid()`：从 Cookie/Set-Cookie 提取 SID。
- `ParseJsonBody()`：把请求体解析成 JSON。
- `ParseLimit()`：处理 `limit` 参数，带默认值和上限截断。
- `WriteJson()` / `WriteJsonError()`：统一 JSON 输出。

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
2. 第三阶段接入检索记忆（RAG）补足长期事实召回。

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
- 当前处于“代码已落地、未接入运行时 provider 选择”的状态，不影响 V1 主通路。

#### 当前阶段注意点
- 回调中断会触发 `stream callback aborted`。
- 流式 JSON 解析失败会返回 `parse_error`。
- V1 启动路径默认注入 `OpenAICompatibleClient`，不做 provider 动态路由。

#### 7.4 V1 Provider 现状（新增）
- 当前运行时实际生效 provider：`OpenAICompatibleClient`。
- 当前可直接支持的厂商范围：只要提供 OpenAI-Compatible `chat/completions` 接口，通常可通过配置直接切换。
- 当前切换方式（不改代码）：
1. 修改 `ai.openai_compatible.base_url` 指向目标厂商兼容网关。
2. 修改 `ai.openai_compatible.api_key` 为目标厂商密钥。
3. 修改 `ai.openai_compatible.default_model` 为目标厂商模型名。
- 已实现但未接入主流程：`AnthropicClient`（代码存在，但 V1 `main.cc` 未做 provider 选择分支）。
- V1 已知边界：
1. 尚未支持“按配置在 OpenAI-Compatible / Anthropic 之间动态切换”。
2. 尚未抽象统一 provider 类型字段（例如 `ai.provider.type=openai_compatible|anthropic`）。
3. 尚未统一不同 provider 的特有参数扩展（例如 Anthropic 版本头、特殊字段）。
- 保持 V1 稳定前提下的后续最小改造入口（第二阶段可做）：
1. 在配置中新增 `ai.provider.type`。
2. 在 `main.cc` 按 `type` 创建对应 `LlmClient` 实现。
3. 保持 `ChatService` 与 `ai_http_api` 无感，继续仅依赖 `LlmClient` 抽象。

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
- 已上线主通路：`OpenAICompatibleClient`
- 已实现未接入主流程：`AnthropicClient`
- 未完成项：运行时按配置选择 provider（计划在第二阶段补上 `ai.provider.type`）。

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

### 9. 联调客户端：`src/ai/client/chat_client.cc`
#### 文件作用
提供同步/流式/历史查询的一体化调试入口。

#### 关键命令
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
- `ai_chat_client`
- `test_ai_chat_service`

#### 意义
形成“服务端 + 客户端 + 单测”的最小工程闭环。

---

## 第一阶段调用链

### 同步链路
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

### 流式链路
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

## 第一阶段已知限制
1. 暂无登录体系，仅以 `SID` 区分会话。
2. 落库语义为“入队成功”，不是“请求返回前强一致落盘”。
3. 内存上下文没有全局淘汰策略（当前只裁剪单会话消息长度）。
4. 缺少生产级限流、熔断、审计和观测能力。

---

## 后续阶段（未实现）
- 第二阶段（关键优先）：上下文策略升级（从“按条数裁剪”升级为“按 token 预算 + 摘要记忆 + 最近窗口”）。
  - 目标：降低关键信息丢失风险，显著提升多轮对话稳定性与回答一致性。
  - 计划：新增 `max_context_tokens`，保留最近 N 轮原文，早期历史滚动摘要为 summary memory。
- 第二阶段补充：API 层解耦重构（`RegisterAiHttpApi` 仅保留路由装配，路由处理逻辑拆分为独立 handler/controller，降低单函数复杂度并提升可测试性）。
- 第二阶段补充：Provider 选择接入（新增 `ai.provider.type`，在 `main.cc` 按配置创建 `OpenAICompatibleClient` 或 `AnthropicClient`，保持 `ChatService` / `ai_http_api` 无感）。
- 第二阶段补充：MySQL 连接池化（将 `ChatRepository` 从单连接 + `std::mutex` 串行模型改为连接池模型，配合 hook 机制实现 DB IO 协程化）。详见下方独立章节。
- 第三阶段：记忆检索增强（RAG/向量召回），在 summary + recent 之外按语义召回历史关键事实。
- 第四阶段：用户体系与鉴权（账号、Token、会话归属）。
- 第五阶段：稳定性治理（限流、重试、熔断、降级）。
- 第六阶段：可观测性（指标、日志、trace、告警）。
- 第七阶段：运维发布（灰度、配置分层、备份恢复）。
- 工程改进项：HTTP 响应封装优化，新增 `setJsonBody(...)` 统一 JSON 输出入口，并让 `setBody(std::string&&)` 支持移动语义，减少一次字符串拷贝。
- 工程改进项：LLM 客户端网络 IO 协程化，建议在第二阶段与 Provider 选择一并落地（改一次 LLM 客户端层、改到位，避免第三阶段 RAG 上线后并发压力暴露线程饥饿问题）。详见下方独立章节。

---

## 待实施优化：LLM 客户端从 curl_easy 迁移到 curl_multi + Fiber 协程调度

### 问题背景
当前 `OpenAICompatibleClient` 和 `AnthropicClient` 使用 `curl_easy_perform()` 发起 HTTP 请求（见 `openai_compatible_client.cc:429` 和 `:518`）。该调用是同步阻塞的：在等待 LLM 上游响应期间，当前 IOManager 工作线程被完全占住。

sylar 框架的 hook 机制（`hook.cc`）已拦截 `socket/connect/read/write/send/recv` 等 POSIX IO 系统调用，在 IOManager 线程上自动转为非阻塞 + fiber yield。但 libcurl 内部使用 `poll()`/`select()` 做多路复用等待，而当前 hook 未拦截这两个调用，导致 fiber 在 `poll()` 上真正阻塞，hook 机制无法生效。

后果：若同时有多个 LLM 请求（尤其是流式请求，耗时可达数十秒），IOManager 的工作线程会被逐个钉死，最终耗尽线程池，整个网络层无法处理新请求。

### 选定方案：curl_multi + IOManager fd 托管（方案 A）

不再使用 `curl_easy_perform()`，改用 `curl_multi` 接口，将 curl 内部的 socket fd 注册到 IOManager 的 epoll 上，IO 就绪时唤醒 fiber 继续驱动 curl 状态机。这样 curl 的网络 IO 完全融入 fiber 调度，单个工作线程可并发处理大量 LLM 请求而互不阻塞。

### 核心实现步骤

#### 1. 封装 FiberCurlSession（新增文件）
新增 `src/ai/llm/fiber_curl_session.h/.cc`，封装单次 curl 请求的 fiber 协程化执行：

```cpp
class FiberCurlSession {
public:
    // 初始化 curl_multi + curl_easy，注册回调
    bool Init(CURL* easy_handle);
    // 阻塞当前 fiber 直到请求完成（内部通过 yield/resume 实现非阻塞）
    CURLcode Perform();
    // 清理资源
    ~FiberCurlSession();
};
```

#### 2. Perform() 内部流程（关键）
```cpp
CURLcode FiberCurlSession::Perform() {
    CURLM* multi = curl_multi_init();
    curl_multi_add_handle(multi, m_easy);

    // 注册 socket 回调：curl 需要监听 fd 时通知我们
    curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, SocketCallback);
    curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, this);

    // 注册 timer 回调：curl 需要超时唤醒时通知我们
    curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, TimerCallback);
    curl_multi_setopt(multi, CURLMOPT_TIMERDATA, this);

    // 启动状态机
    int running = 0;
    curl_multi_socket_action(multi, CURL_SOCKET_TIMEOUT, 0, &running);

    // 主循环：每次 fiber yield 等待 IO 或 timer，被唤醒后继续驱动
    while (running > 0) {
        Fiber::YieldToHold();  // 让出 fiber，等 IOManager 唤醒
        // 唤醒后根据就绪的 fd 或超时调用 socket_action
        curl_multi_socket_action(multi, m_ready_fd, m_ready_action, &running);
    }

    // 提取结果
    CURLMsg* msg = curl_multi_info_read(multi, ...);
    CURLcode result = msg->data.result;

    curl_multi_remove_handle(multi, m_easy);
    curl_multi_cleanup(multi);
    return result;
}
```

#### 3. SocketCallback（fd 事件注册）
当 curl 需要监听某个 fd 的读/写事件时回调此函数：
```cpp
static int SocketCallback(CURL* easy, curl_socket_t fd, int what, void* userp, void* socketp) {
    FiberCurlSession* self = static_cast<FiberCurlSession*>(userp);
    IOManager* iom = IOManager::GetThis();

    // what == CURL_POLL_REMOVE: 移除 fd 上的事件监听
    // what == CURL_POLL_IN:     注册 READ 事件，就绪时 schedule fiber 恢复
    // what == CURL_POLL_OUT:    注册 WRITE 事件
    // what == CURL_POLL_INOUT:  注册 READ + WRITE

    // 先清除旧事件（如果有）
    // 再按 what 注册新事件，回调中 schedule 当前 fiber 恢复
    // 记录 m_ready_fd / m_ready_action 供 Perform() 循环使用
}
```

#### 4. TimerCallback（超时管理）
curl 内部需要定时器时回调：
```cpp
static int TimerCallback(CURLM* multi, long timeout_ms, void* userp) {
    FiberCurlSession* self = static_cast<FiberCurlSession*>(userp);
    IOManager* iom = IOManager::GetThis();

    if (timeout_ms < 0) {
        // 取消定时器
    } else if (timeout_ms == 0) {
        // 立即触发：直接 schedule fiber
    } else {
        // 注册 IOManager timer，到期后 schedule fiber 恢复
    }
}
```

#### 5. 改造 OpenAICompatibleClient / AnthropicClient
将 `Complete()` 和 `StreamComplete()` 中的：
```cpp
CURLcode code = curl_easy_perform(curl);
```
替换为：
```cpp
FiberCurlSession session;
session.Init(curl);
CURLcode code = session.Perform();
```

其余代码（请求构建、响应解析、回调处理）。

### 需要注意的细节

1. **fiber 绑定线程**：`SocketCallback` 中 schedule fiber 恢复时，应尊重 `fiber->getBoundThread()`，避免 ucontext 跨线程恢复崩溃（与 hook.cc 中 sleep 的处理一致）。
2. **curl_multi 生命周期**：每次请求创建独立的 `CURLM*`，避免多 fiber 共享同一个 multi handle 带来的线程安全问题。如果后续需要连接池复用，可以考虑 per-thread multi handle。
3. **WRITEFUNCTION 回调兼容**：流式场景的 `StreamWriteCallback` 不需要改动，它仍然由 curl 在数据到达时调用，只是驱动方式从 `curl_easy_perform` 内部循环变成了 `curl_multi_socket_action` 驱动。
4. **错误处理**：`curl_multi_info_read` 返回的 `CURLMsg` 中包含最终的 `CURLcode`，与原来 `curl_easy_perform` 的返回值语义一致，上层错误处理逻辑无需修改。
5. **退化兼容**：如果当前线程没有 IOManager（如测试线程），`Perform()` 应退化为直接调用 `curl_easy_perform()`，保持向后兼容。

### 曾考虑但未选择的方案

- **方案 B（hook poll/select）**：在 `hook.cc` 补上 `poll` 和 `select` 的 hook。改动小，但 libcurl 内部状态机复杂，poll 的 fd 集合和超时语义与 IOManager 的 per-fd 事件模型不完全匹配，边界问题多，长期维护成本高。
- **方案 C（专用阻塞线程池）**：把 `curl_easy_perform()` 扔到独立线程池，fiber yield 等结果。实现简单，但引入额外线程切换开销和线程池管理复杂度，且线程数仍然是并发上限瓶颈，不如方案 A 彻底。

### 预期收益
- IOManager 工作线程不再被 LLM 请求阻塞，单线程可并发服务大量 LLM 调用。
- 与框架 fiber 调度体系完全融合，无额外线程池开销。
- 对上层 `ChatService` 和 API 层完全透明，无需修改。

---

## 待实施优化：ChatRepository 从单连接改为 MySQL 连接池（第二阶段，curl_multi 改造之后）

### 问题背景
当前 `ChatRepository`（`chat_repository.cc`）使用单个 `MYSQL*` 连接 + `std::mutex` 串行化所有查询。存在两个问题：

1. **阻塞工作线程**：`mysql_query()`、`mysql_store_result()`、`mysql_real_connect()`、`mysql_ping()` 底层都走 TCP socket IO。虽然 sylar 的 hook 机制已拦截 `read/write/connect`，理论上能让 MySQL IO 自动 fiber 化，但 `std::mutex` 不是 fiber 感知的——一个 fiber 拿到锁后若在 `mysql_query` 内 yield，锁不会释放，其他 fiber 在 `lock_guard` 处会阻塞整个线程。
2. **并发瓶颈**：所有读写请求串行排队经过同一个连接，即使 MySQL 本身能处理并发，应用层也只能逐个执行。

严重程度比 curl 低（MySQL 查询通常毫秒级，LLM 请求秒级到十秒级），但在 MySQL 慢查询或网络抖动时同样会卡住工作线程。

### 选定方案：连接池

不改 MySQL 调用方式（不用非阻塞 API），而是维护一个连接池，每个 fiber 从池中取独立连接，用完归还。

### 核心设计

#### 1. 新增 MysqlConnectionPool
新增 `src/ai/storage/mysql_connection_pool.h/.cc`：

```cpp
class MysqlConnectionPool {
public:
    using ptr = std::shared_ptr<MysqlConnectionPool>;

    // 初始化连接池，创建 min_size 个初始连接
    bool Init(const config::MysqlSettings& settings, size_t min_size, size_t max_size, std::string& error);

    // 从池中获取一个可用连接（池空时创建新连接，达上限时 fiber yield 等待归还）
    MYSQL* Acquire(uint64_t timeout_ms = 3000);

    // 归还连接到池中
    void Release(MYSQL* conn);

    // 关闭所有连接
    void Shutdown();
};
```

#### 2. RAII 连接守卫
```cpp
class ScopedMysqlConn {
    MysqlConnectionPool* m_pool;
    MYSQL* m_conn;
public:
    ScopedMysqlConn(MysqlConnectionPool* pool) : m_pool(pool), m_conn(pool->Acquire()) {}
    ~ScopedMysqlConn() { if (m_conn) m_pool->Release(m_conn); }
    MYSQL* get() { return m_conn; }
    operator bool() { return m_conn != nullptr; }
};
```

#### 3. 改造 ChatRepository
- 构造函数注入 `MysqlConnectionPool::ptr` 替代 `MysqlSettings`。
- 移除 `m_conn` 成员和 `m_mutex`。
- 每个查询方法内部通过 `ScopedMysqlConn` 获取/归还连接：

```cpp
bool ChatRepository::LoadHistory(...) {
    ScopedMysqlConn conn(m_pool.get());
    if (!conn) {
        error = "acquire mysql connection failed";
        return false;
    }
    // 用 conn.get() 替代原来的 m_conn，其余查询逻辑不变
    mysql_query(conn.get(), sql.str().c_str());
    ...
}
```

#### 4. 同步改造 AsyncMySqlWriter
`AsyncMySqlWriter` 当前也持有独立 `MYSQL*` 连接做批量写入。改造方式：
- 注入同一个连接池，`FlushBatch()` 时 Acquire 一个连接，事务完成后 Release。
- 或者保持独立连接不变（写路径在后台线程，不占 IOManager 工作线程，优先级低）。

### 池内等待的 fiber 化
当池中无可用连接且已达 `max_size` 上限时，`Acquire()` 需要等待其他 fiber 归还。实现方式：
- 维护一个等待队列（存放 `Fiber::ptr`）。
- `Acquire()` 发现无可用连接时，将当前 fiber 入队并 `Fiber::YieldToHold()`。
- `Release()` 归还连接后，若等待队列非空，取出队首 fiber 并 `IOManager::schedule()` 唤醒。
- 这样等待过程不阻塞线程，其他 fiber 可以继续执行。

### 连接健康检查
- `Release()` 归还时记录归还时间。
- `Acquire()` 取出连接时，若距上��使用超过阈值（如 30s），先 `mysql_ping()` 探活，失败则丢弃并创建新连接。
- 避免拿到已被 MySQL server 超时断开的死连接。

### 配置扩展
在 `MysqlSettings` 中新增：
- `pool_min_size`：初始连接数（默认 2）
- `pool_max_size`：最大连接数（默认 8）
- `pool_acquire_timeout_ms`：获取连接超时（默认 3000）

### 曾考虑但未选择的方案
- **非阻塞 MySQL API（`mysql_real_query_start/cont`）**：需要 MariaDB Connector/C 才有完整支持，标准 libmysqlclient 支持有限。改造量大，且连接池方案已经足够解决并发问题。
- **仅替换 std::mutex 为 fiber 协程锁**：能让 hook 生效不卡线程，但仍然是单连接串行，并发瓶颈未解。

### 预期收益
- 多个 fiber 可并发执行 DB 查询，不再串行排队。
- 配合 hook 机制，每个连接的 socket IO 自动 fiber 化，不阻塞工作线程。
- 连接复用减少频繁建连开销。
- 对上层 `ChatService` 完全透明（`ChatStore` 接口不变）。
