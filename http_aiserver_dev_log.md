# HTTP + AI Server 开发笔记

## 项目目标 (2026-03-07)
- 基于当前 sylar 项目实现 Servlet/路由风格的 HTTP 框架。
- 支持 Session 管理。
- 支持 SSE 长连接输出。
- 当前阶段只完成 HTTP 框架层，不实现 AI 应用逻辑。

---

## 第一阶段：HTTP 核心框架骨架

### 0. `http.h / http.cc`
#### 类/文件作用
这一组文件不是用来处理 socket，也不是用来解析 HTTP 报文的。

它的作用是给整个 HTTP 模块提供最基础的公共定义，主要包括：

- `HttpMethod`：HTTP 请求方法枚举
- `HttpStatus`：HTTP 响应状态码枚举
- 枚举和字符串之间的转换函数

可以把它理解成：

```text
HTTP 模块最底层的公共语义定义文件
```

#### 为什么要单独设计成一个文件
如果不把这些内容单独抽出来，后面的：

- `HttpRequest`
- `HttpResponse`
- `HttpParser`

都会自己定义一套 method/status，或者互相 include，最后很容易造成：

- 重复定义
- 头文件依赖混乱
- 模块边界不清晰

所以我把“HTTP 最基础的公共枚举和转换函数”集中放在 `http.h / http.cc` 里。

这种设计的好处是：

1. **依赖方向清晰**
   - 上层模块都依赖它
   - 它自己不依赖上层模块

2. **便于复用**
   - `HttpRequest` 和 `HttpResponse` 都可以共用同一套 method/status 定义

3. **便于扩展**
   - 后面如果要增加更多方法或状态码，只需要改这里

#### 具体设计了什么

**1. `HttpMethod`**

当前保留了：

- `GET`
- `POST`
- `PUT`
- `DELETE_`
- `HEAD`
- `OPTIONS`
- `PATCH`
- `INVALID_METHOD`

这里保留 `INVALID_METHOD` 的原因是：

- 解析器在把字符串方法转成枚举时，可能遇到当前不支持的方法；
- 这时候不能直接崩溃，需要有一个“非法值/未知值”承接错误状态。

**2. `HttpStatus`**

当前只保留了当前框架阶段最常用的一小部分状态码：

- `200 OK`
- `400 BAD_REQUEST`
- `404 NOT_FOUND`
- `408 REQUEST_TIMEOUT`
- `500 INTERNAL_SERVER_ERROR`
- `501 NOT_IMPLEMENTED`
- `503 SERVICE_UNAVAILABLE`

这里没有一开始就把所有状态码全写上，是因为当前是学习和框架搭建阶段，先保留最常用集合更清晰。

**3. 转换函数**

- `HttpMethodToString()`
- `StringToHttpMethod()`
- `HttpStatusToString()`

这几个函数的设计目的，是把：

```text
程序内部的枚举语义
<->
网络报文里的文本语义
```

连接起来。

例如：

- parser 解析到 `"GET"` 时，要转成 `HttpMethod::GET`
- response 序列化时，要把 `HttpStatus::OK` 转成 `"OK"`

#### 为什么程序内部用枚举，不直接全程用字符串
因为枚举有几个明显好处：

1. **更安全**
   - 方法集合是受控的，不容易拼错字符串

2. **更适合判断逻辑**
   - 以后如果要写：
     - 只允许 `GET`
     - `POST/PUT` 才允许 body
   - 用枚举判断更直接

3. **更利于后续扩展**
   - 比如后面做权限、路由限制、方法过滤时，枚举会更清楚

#### 当前阶段需要注意的点

1. `HttpMethod` 目前不是完整 HTTP 方法全集，只是当前够用的子集
2. `HttpStatus` 目前也是精简版，不是 RFC 全量状态码
3. `StringToHttpMethod()` 当前是大小写敏感的，意味着 parser 期望标准 HTTP 大写方法
4. `DELETE` 因为和 C++ 关键字/命名冲突风险，枚举里写成了 `DELETE_`

#### 学习这个文件时最应该抓住什么
你现在看 `http.h / http.cc`，最需要抓住的是：

```text
这个文件不是在做“网络处理”，而是在定义“HTTP 模块的公共语言”。
```

也就是说，后面的所有 HTTP 类，都会建立在这里定义的方法和状态码语义之上。

---

### 1. 本阶段目标
#### 模块作用
先把 HTTP 框架的最小闭环搭起来，让项目具备：
- HTTP 请求/响应对象
- HTTP 请求解析
- 基于 `TcpServer` 的 `HttpServer`
- Servlet/路由分发
- 内存 Session 管理骨架
- SSE 输出工具骨架

#### 设计理念
当前阶段优先打通框架主链路，而不是一次把所有高级特性做满。

因此本阶段策略是：
- 先支持 HTTP/1.1
- 先支持 `Content-Length`
- 先支持 keep-alive
- 先支持 Cookie + 内存 Session
- 先支持基础 SSE 输出
- 暂不支持 chunked request body、multipart、WebSocket

---

### 2. 当前实现拆分
#### 设计要点
本阶段新增模块：

- `HttpRequest/HttpResponse`
- `HttpRequestParser`
- `HttpSession`
- `Servlet/FunctionServlet/ServletDispatch`
- `HttpServer`
- `Session/SessionManager`
- `SSEWriter`

整体分层：

```text
TcpServer -> HttpServer -> HttpSession -> HttpParser
                               |
                               -> ServletDispatch -> Servlet
                               -> SessionManager
                               -> SSEWriter
```

---

### 2.1 每个类分别是干什么的（学习版）
#### 设计要点

这一节专门用来回答“我现在看到很多类，但不知道它们分别干什么”的问题。

**1. `HttpRequest`**
- 作用：表示“一条已经被解析好的 HTTP 请求”
- 里面装的内容：method、path、query、headers、body、cookies、keep-alive
- 你可以把它理解成：业务层看到的“请求对象”

**2. `HttpResponse`**
- 作用：表示“一条准备返回给客户端的 HTTP 响应”
- 里面装的内容：status、headers、body、Set-Cookie、keep-alive
- Servlet 的主要工作就是填这个对象

**3. `HttpRequestParser`**
- 作用：把 TCP 字节流解析成 `HttpRequest`
- 它是真正解决 HTTP 半包/粘包的地方
- 关键边界规则：先找 `\r\n\r\n`，再按 `Content-Length` 取 body

**4. `HttpSession`**
- 作用：把 `SocketStream` 包装成“HTTP 语义会话”
- 负责在连接级维护接收缓冲区
- 对上提供 `recvRequest()` / `sendResponse()`

**5. `Servlet`**
- 作用：HTTP 业务处理器抽象接口
- 一个路由最终一定会落到某个 Servlet 上执行

**6. `FunctionServlet`**
- 作用：把 lambda / 回调函数包装成 Servlet
- 这样注册测试接口时不用每次都写一个继承类

**7. `ServletDispatch`**
- 作用：HTTP 路由器
- 负责根据 URI 找到要执行的 Servlet
- 当前支持精确匹配和通配匹配

**8. `HttpServer`**
- 作用：真正的 HTTP 服务器入口
- 它继承自 `TcpServer`
- 也就是说：底层还是 TCP Server，只是在 `handleClient()` 里加上 HTTP 解析和路由分发

**9. `Session`**
- 作用：单个服务端会话对象
- 存放某个用户/客户端的会话数据
- 当前阶段先用 `string -> string`

**10. `SessionManager`**
- 作用：管理所有 `Session`
- 负责创建、查找、续期、删除、过期清理

**11. `SSEWriter`**
- 作用：把普通文本按 SSE 协议格式写出去
- 是后续 AI 流式 token 输出的基础工具

一句话关系图：

```text
TcpServer
  -> HttpServer
      -> HttpSession
          -> HttpRequestParser
      -> ServletDispatch
          -> Servlet
      -> SessionManager
      -> SSEWriter
```

---

### 3. HTTP 如何解决 TCP 黏包/半包
#### 设计理念
HTTP 不会复用之前项目里的自定义二进制包协议，而是在 HTTP 协议层按 HTTP 自己的边界规则做解析。

#### 设计要点
当前解析策略：

1. 先在接收缓冲区中查找 `\r\n\r\n`，确定 header 结束位置；
2. 解析请求行与 headers；
3. 再根据 `Content-Length` 判断 body 是否完整；
4. 完整则产出一个 `HttpRequest`；
5. 消费已解析字节，剩余数据保留给下一个请求。

这套机制天然支持：
- 半包
- 粘包
- keep-alive 下一个连接多个请求

---

### 4. Session 设计（第一版）
#### 设计理念
Session 先做最小可用版本：
- 服务端内存存储
- 客户端通过 Cookie `SID` 持有会话标识
- Session 值先用 `string -> string`

#### 设计要点
- `Session`：保存 `id`、创建时间、最后访问时间、超时时间和键值对
- `SessionManager`：负责创建、获取、续期、删除、过期清理
- 当前版本已经具备后续接入业务层会话状态的基础

---

### 5. SSE 设计（第一版）
#### 设计理念
SSE 当前只实现输出能力，不实现消息广播中心。

#### 设计要点
- `SSEWriter` 支持发送：
  - `event`
  - `id`
  - `retry`
  - `data`
- 支持发送注释型心跳帧
- 为后续 AI token 流式输出预留能力

---

### 6. 测试验证
#### 设计要点
本阶段新增的测试：

- `test_http_parser`
  - 普通请求解析
  - 半包 body
  - 粘包双请求
- `test_http_server`
  - 启动 `HttpServer`
  - 注册 `/ping` servlet
  - 本地发起请求并验证响应

---

### 7. 遇到的问题
#### Bug 1：`HttpServer` 集成测试最初出现退出卡住

**现象**：

- 第一版 `test_http_server` 里，主线程直接 `sleep_for` 等待服务启动，然后在主线程发请求、再调用 `server->stop()`；
- 测试看起来功能正确，但进程退出时会长时间卡住。

**定位过程**：

1. 先确认 HTTP 路由和响应本身是正常的；
2. 再观察发现请求已经成功返回，卡住主要发生在 server stop / IOManager 退出阶段；
3. 结合现有 `TcpServer` 的 stop 逻辑，确认问题不在 HTTP 解析，而在测试驱动方式和调度器生命周期配合上。

**根因**：

- 测试中的客户端请求和停止时机放在调度器外部驱动；
- `TcpServer::stop()` 本身是异步清理模式；
- 主线程外部等待 + 调度器内部异步停止，容易把测试变成“不知道谁负责收尾”的状态。

**修复**：

- 改成在 `IOManager` 内部调度两个任务：
  1. 延迟发起客户端请求；
  2. 延迟停止服务器；
- 让请求、停止、收尾都在同一个调度模型里完成。

**收获**：

- 网络服务测试不能只验证“请求通了”，还要验证“生命周期是否正确收尾”；
- 特别是 `TcpServer/HttpServer + IOManager` 这种异步架构，测试驱动方式本身就可能制造假问题。

---

### 8. 阶段总结
#### 一句话总结
本阶段的目标不是做 AI 应用逻辑，而是先把“HTTP + Servlet + Session + SSE”这条底层基础设施链路搭起来。

#### 当前完成情况
- 已完成 `HttpRequest/HttpResponse`
- 已完成 `HttpRequestParser`
- 已完成 `HttpSession`
- 已完成 `Servlet/FunctionServlet/ServletDispatch`
- 已完成 `HttpServer`
- 已完成 `Session/SessionManager` 第一版
- 已完成 `SSEWriter` 第一版
- 已补充 `test_http_parser` 和 `test_http_server`

#### 当前验证结果
- `cmake --build build --target test_http_parser test_http_server` 通过
- `./bin/test_http_parser` 通过
- `./bin/test_http_server` 通过

#### 下一阶段
- 完善 Session 生命周期管理（定时清理、更多测试）
- 给 SSE 增加真正的路由接入与心跳示例
- 继续补充开发过程中遇到的 bug 和调优记录

---
