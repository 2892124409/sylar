# Sylar

一个用于实习 / 求职展示的 C++ 服务端项目：底层协程与网络能力基于 GitHub 开源 `sylar` 项目做二次开发，在此基础上继续扩展 HTTP 框架和 AI 聊天应用层，而不是只做第三方 SDK 的简单封装。项目围绕一条完整请求链路展开，覆盖鉴权、流式回复、异步持久化、消息队列解耦、RAG 检索增强等能力。

## 项目定位

这个项目更接近“服务端系统能力展示”，重点不在网页界面，而在下面这些能力是否真的从源码里落到了可运行的工程里：

- 基于开源 `sylar` 的协程调度器、IOManager、Hook、网络层做理解、改造与集成
- 在底层运行时之上扩展 HTTP 解析、路由、中间件、Session、SSE 流式输出
- 面向 AI 对话场景的业务层：鉴权、多 Provider 路由、上下文管理、历史记录、异步持久化
- RabbitMQ 解耦写入链路，Ollama + Qdrant 组成的可选 RAG 能力

如果把它作为实习项目来介绍，核心卖点不是“我接了一个大模型 API”，而是“我把一个 C++ 聊天服务端从运行时到业务层完整串起来了”。

## 项目来源

- 底层协程、IO 与部分网络基础设施来自 GitHub 开源 `sylar` 项目
- 当前仓库不是对原项目的原样搬运，而是在其基础上继续做了结构梳理、模块裁剪、运行时层改造和业务扩展
- 你在面试里可以直接说明：这个项目的底层基座来自开源项目，自己的工作重点在源码理解、二次开发，以及 HTTP/AI 应用层能力落地

## 相对原版 Sylar 的改造

这部分是这个项目最值得讲清楚的地方。底层不是“从零自研”，但也不是“直接把原版 sylar 拉下来跑业务”。

- 调度器从“全局任务链表 + 全局锁”改成了“per-worker 本地队列 + 跨线程 mailbox”
  - 每个 worker 维护自己的 `localQueue`
  - 跨 worker 投递优先走无锁 ring，满了再退化到 fallback 链表
  - 调度时会根据 worker 当前负载做轻量选择，而不是所有线程都去竞争一个全局任务池
- 唤醒策略改成了“按 worker 定向唤醒”
  - 目标 worker 进入休眠态才唤醒，减少无效唤醒和高频通知开销
- `IOManager` 从“单 epoll + 单唤醒管道”改成了“per-worker epoll + eventfd”
  - 每个 worker 都有自己的 `epfd` 和 `wakeFd`
  - `fd` 事件会绑定 `ownerWorker`，后续增删改查都尽量在所属 worker 上完成
- 定时器从“全局 set”改成了“per-worker 最小堆桶”
  - timer 与 worker 绑定，唤醒和超时处理更贴近执行线程
- `Fiber` 协议增加了等待结果语义
  - 不只是简单 `resume/yield`
  - 还支持 `WAIT_READY / WAIT_TIMEOUT / WAIT_CANCELLED`
  - 便于 Hook / IO 等待层把超时和取消结果传回协程
- 上下文切换后端做了抽象
  - 支持 `libco_asm` 和 `ucontext`
  - 默认走 `libco_asm`，兼顾性能和兼容性

因此，这个项目更准确的描述不是“基于 sylar 写了个 AI 服务”，而是“基于 sylar 的协程 / IO 思路，重构了调度与事件循环的一部分，再往上实现 HTTP 和 AI 应用层”。

## 技术栈

- 语言与构建：`C++11`、`CMake`
- 并发与网络：协程、调度器、`epoll`、Hook、TCP Server
- 协议与服务：HTTP 解析、路由、中间件、Session、SSE
- AI 集成：OpenAI-compatible Provider、Anthropic Provider
- 存储与中间件：MySQL / MariaDB、RabbitMQ
- 检索增强：Ollama Embedding、Qdrant Vector Store
- 配置与安全：`yaml-cpp`、`OpenSSL`

## 核心特性

- 基于开源 `sylar` 改造的协程运行时与网络层
  - `src/sylar` 里包含 Fiber、Scheduler、IOManager、Timer、Hook、FD 管理、Socket/TcpServer 等基础设施，并作为整个项目的底层基座。
  - 相比原版 `sylar`，这里把调度器、IOManager、TimerManager 和 Fiber 等待语义都做了更适合多 worker 场景的改造。
  - 默认使用 `libco_asm` 上下文切换后端，追求更低切换成本。
- 在底层运行时之上扩展的 HTTP 框架
  - `src/http` 里实现了请求解析、响应组装、路由分发、中间件链、Session、SSL、SSE。
  - 路由优先级明确：精确匹配优先，其次参数路由，再到通配和默认处理。
  - 支持 `Content-Length` 与 chunked 请求体解析，服务端连接支持 keep-alive。
- AI 对话服务
  - `src/ai` 里实现注册、登录、登出、查询当前身份、聊天补全、流式聊天、历史记录查询。
  - 路由策略支持“请求显式指定 provider”以及“model -> provider 映射 + 默认 provider”。
  - 已实现 OpenAI-compatible Client 与 Anthropic Client。
- 异步持久化
  - 聊天写入链路不会在主请求线程内直接落库，而是先进入消息 sink。
  - 可选择本地异步 MySQL Writer，或者走 RabbitMQ 再由独立消费者写库。
- 可选 RAG
  - 检索链路：查询 -> Embedding -> Qdrant 向量检索 -> 拼接召回片段 -> 参与对话。
  - 索引链路：消息异步进入 RAG Indexer -> Embedding -> Upsert 到 Qdrant。

## 系统架构

项目可以理解成三层：

- `src/sylar`
  - 基于开源 `sylar` 改造的底层运行时，负责协程调度、IO 多路复用、Hook、定时器、Socket 与 TCP Server。
- `src/http`
  - HTTP 框架层，负责协议解析、路由、中间件、Session、SSE、SSL。
- `src/ai`
  - 应用层，负责鉴权、聊天服务、Provider 路由、持久化、MQ、RAG。

一条典型链路如下：

```text
Client
  -> HttpServer(TcpServer)
  -> Router / Middleware
  -> AiHttpHandlers
  -> ChatService
     -> LlmRouter
        -> OpenAICompatibleClient / AnthropicClient
     -> MessageSink
        -> AsyncMySqlWriter
        -> RabbitMqMessageSink -> ai_mq_consumer -> MySQL
     -> RagRetriever (optional)
     -> RagIndexer  (optional)
        -> Ollama Embedding
        -> Qdrant
```

## 请求链路说明

以一次聊天请求为例，源码里的真实处理过程大致是：

1. `HttpServer` 接收连接，请求被 Router 分发到 `/api/v1/chat/completions` 或 `/api/v1/chat/stream`。
2. `RequestIdMiddleware` 注入请求 ID，`AuthMiddleware` 解析 Bearer Token，并把主体信息写入请求头。
3. `AiHttpHandlers` 解析 JSON，请求体至少需要 `message` 字段，可选 `conversation_id`、`model`、`provider`、`temperature`、`max_tokens`。
4. `ChatService` 负责加载上下文、控制历史窗口、必要时触发摘要刷新，并按需接入 RAG 召回。
5. `LlmRouter` 按 `显式 provider > model_map > default_provider` 选择实际上游。
6. 上游请求通过 `FiberCurlSession` 发出；普通模式返回整段结果，流式模式通过 SSE 持续向客户端推送。
7. 对话消息异步进入持久化链路，同时可进入 RAG 索引链路。

## 目录结构

```text
.
├── CMakeLists.txt
├── src
│   ├── base                # 日志、配置、基础组件
│   ├── sylar               # 协程运行时 / IOManager / 网络层
│   ├── http                # HTTP 框架
│   └── ai                  # AI 应用层（auth/chat/llm/mq/rag/storage）
├── tests                   # 单元测试与模块测试
├── docs                    # 设计文档与阶段性记录
├── build                   # 构建产物
└── bin                     # 额外脚本或可执行文件
```

## 运行环境与依赖

### 1. 平台要求

- 默认配置要求 `Linux x86_64`
- 原因是 `CMakeLists.txt` 默认使用 `SYLAR_FIBER_CONTEXT_BACKEND=libco_asm`
- 如果你不在 `Linux x86_64` 环境，可以尝试切换为：

```bash
cmake -S . -B build -DSYLAR_FIBER_CONTEXT_BACKEND=ucontext
```

这个通用后端更适合兼容性场景，但当前默认说明仍以 `Linux x86_64` 为主。

### 2. 本地库依赖

根据 `CMakeLists.txt`，构建时至少需要：

- `yaml-cpp`
- `libcurl`
- `OpenSSL`
- `mysqlclient` 或 `mariadb` client library
- `librabbitmq`
- `pthread`
- `dl`
- `nlohmann/json.hpp` 头文件

### 3. 外部服务依赖

- MySQL / MariaDB：必需
- RabbitMQ：当 `ai.mq.enabled=true` 时必需
- Ollama + Qdrant：当 `ai.rag.enabled=true` 时必需

如果只是想先把主链路跑通，最简单的方式是：

- 保留 MySQL
- 把 `ai.mq.enabled` 改成 `false`
- 把 `ai.rag.enabled` 改成 `false`

## 编译

```bash
cmake -S . -B build
cmake --build build -j
```

构建成功后，主要可执行文件在 `build/bin/`：

- `ai_chat_server`
- `ai_auth_chat_client`
- `ai_mq_consumer`

## 配置说明

示例配置文件在：

```text
src/ai/config/ai_server.example.yml
```

启动前至少需要确认这些配置：

- `ai.server.host` / `ai.server.port`
- `ai.llm.providers[*].base_url` / `api_key` / `default_model`
- `ai.mysql.*`
- `ai.mq.enabled`
- `ai.rag.enabled`

示例配置里已经给出了：

- OpenAI-compatible Provider 示例
- Anthropic Provider 示例
- MySQL 连接池配置
- RabbitMQ 生产 / 消费配置
- Ollama Embedding 与 Qdrant 配置

## 运行方式

### 1. 启动服务端

```bash
./build/bin/ai_chat_server -c src/ai/config/ai_server.example.yml
```

### 2. 如果开启 MQ，启动持久化消费者

```bash
./build/bin/ai_mq_consumer -c src/ai/config/ai_server.example.yml
```

### 3. 启动命令行客户端

```bash
./build/bin/ai_auth_chat_client --base-url http://127.0.0.1:8080
```

也可以直接自动注册并登录：

```bash
./build/bin/ai_auth_chat_client \
  --base-url http://127.0.0.1:8080 \
  --username demo \
  --password 123456 \
  --register
```

客户端支持的交互命令包括：

- `/register <u> <p>`
- `/login <u> <p>`
- `/logout`
- `/me`
- `/new`
- `/history [limit]`
- `/stream on|off`
- `/provider <id>`
- `/token <value>`
- `/exit`

## 主要接口

| 方法 | 路径 | 说明 | 是否需要鉴权 |
| --- | --- | --- | --- |
| `GET` | `/api/v1/healthz` | 健康检查 | 否 |
| `POST` | `/api/v1/auth/register` | 注册账号 | 否 |
| `POST` | `/api/v1/auth/login` | 登录并获取 Bearer Token | 否 |
| `POST` | `/api/v1/auth/logout` | 注销当前 Token | 是 |
| `GET` | `/api/v1/auth/me` | 获取当前登录身份 | 是 |
| `POST` | `/api/v1/chat/completions` | 非流式聊天 | 是 |
| `POST` | `/api/v1/chat/stream` | SSE 流式聊天 | 是 |
| `GET` | `/api/v1/chat/history/:conversation_id` | 查询会话历史 | 是 |

Bearer Token 通过 `Authorization: Bearer <token>` 传递。鉴权层会把身份信息转换为请求内的主体标识，后续聊天历史和会话隔离都依赖这个主体 SID。

## 接口示例

### 注册

```bash
curl -X POST http://127.0.0.1:8080/api/v1/auth/register \
  -H 'Content-Type: application/json' \
  -d '{"username":"demo","password":"123456"}'
```

### 登录

```bash
curl -X POST http://127.0.0.1:8080/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"demo","password":"123456"}'
```

### 非流式聊天

```bash
curl -X POST http://127.0.0.1:8080/api/v1/chat/completions \
  -H 'Content-Type: application/json' \
  -H "Authorization: Bearer $TOKEN" \
  -d '{
    "message": "你好，介绍一下你自己",
    "temperature": 0.7,
    "max_tokens": 256
  }'
```

### 指定 provider 的聊天请求

```bash
curl -X POST http://127.0.0.1:8080/api/v1/chat/completions \
  -H 'Content-Type: application/json' \
  -H "Authorization: Bearer $TOKEN" \
  -d '{
    "message": "帮我总结这段对话",
    "provider": "deepseek_main",
    "model": "deepseek-chat"
  }'
```

## 安全与实现细节

- 密码不是明文存储，认证层使用 `PBKDF2-HMAC-SHA256`
- Token 不直接以明文形式持久化，而是存储其 `SHA256` 哈希
- 登录成功后会返回 `principal_sid`，聊天历史与会话隔离依赖这个主体标识
- 流式接口基于 SSE，便于命令行客户端或前端页面直接消费

## 测试

仓库中包含运行时、HTTP、聊天服务、RAG 等多个测试目标。当前源码树下已经可以直接构建出这些可执行测试文件，例如：

- `test_fiber`
- `test_scheduler`
- `test_iomanager`
- `test_hook`
- `test_http_parser`
- `test_http_server`
- `test_ai_chat_service`
- `test_rag_indexer`

建议先跑下面这几项核心测试：

```bash
./build/bin/test_http_parser
./build/bin/test_ai_chat_service
./build/bin/test_rag_indexer
./build/bin/test_hook
```

这些测试分别覆盖了：

- HTTP 报文解析
- ChatService 主流程
- RAG 索引链路
- Hook 与协程化阻塞调用

## 这个项目适合怎么讲

如果你要把它写进简历或面试介绍，建议不要只说“我做了一个 AI 聊天项目”，而是更具体一些：

- 我基于开源 `sylar` 项目做了底层协程 / 网络层的理解、改造和整合
- 我把原版偏“全局任务队列 + 单 epoll”的运行时模型，改成了更偏 per-worker 的调度与事件循环结构
- 我在这个运行时基础上扩展了 HTTP 解析、路由、中间件和 SSE 流式输出
- 我把 AI 对话服务做成了完整链路，包含鉴权、路由、上下文、异步持久化、MQ、RAG
- 我不是只会调 SDK，而是能在开源基座上做二次开发，把一个服务端系统分层设计并真正跑起来

## 当前边界

这个项目已经覆盖了比较完整的服务端主链路，但它的定位仍然是“学习与展示工程能力”，不是以生产落地为目标做完全部硬化：

- 重点在架构与关键路径，不在高可用部署体系
- 依赖外部组件较多，运行前需要先准备 MySQL / RabbitMQ / Ollama / Qdrant 等环境
- 监控、告警、压测、灰度、容灾等生产能力还可以继续补强
- 测试已经覆盖多个关键模块，但还不是一套完整的端到端生产级验证体系

## 后续可扩展方向

- 增加更完整的可观测性：metrics、trace、structured logging
- 增加限流、熔断、重试预算、配额控制
- 增加更系统的 benchmark 与压测报告
- 增加 Web 前端或管理后台，展示对话、路由命中、MQ 堆积和 RAG 命中情况
- 增加更多 Provider 适配器和更细粒度的路由策略
