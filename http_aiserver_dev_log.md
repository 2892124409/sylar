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

### 0.1 `http_request.h / http_request.cc`
#### 类/文件作用
`HttpRequest` 的作用是承载“已经被解析完成的一条 HTTP 请求”。

它自己不负责：
- 从 socket 读数据
- 解析字节流

它只负责保存解析后的请求语义，供路由和业务处理使用。

#### 具体设计了什么
当前版本主要包含这些字段：

- `method`：请求方法（GET/POST 等）
- `versionMajor/versionMinor`：HTTP 版本
- `path/query/fragment`：请求目标拆分后的三个部分
- `headers`：原始请求头
- `params`：query string 解析出来的参数
- `cookies`：Cookie 解析出来的键值
- `body`：请求体
- `keepalive`：是否保持连接

并提供了：
- header/param/cookie 的 set/get 方法
- `getVersionString()`
- `getPathWithQuery()`

#### 为什么这么设计
把请求对象单独建模有三个目的：

1. **解耦协议解析和业务处理**
   - `HttpParser` 负责“把字节解析成请求对象”
   - `Servlet` 负责“根据请求对象处理业务”

2. **统一上层接口**
   - 无论底层 socket 如何收包，上层都只面对 `HttpRequest`

3. **便于后续扩展**
   - 后续要加表单、JSON、鉴权、trace id 等，都可以在 `HttpRequest` 上扩展

#### 当前阶段需要注意的点
1. `params` 目前只做了最基础的 `a=b&c=d` 拆分，没有 URL decode
2. `headers` 的 key 当前按原样存储，大小写归一化策略后续可再优化
3. `keepalive` 是解析后的语义结果，不是原始报文文本

#### 学习这个类时最该抓住的点

```text
HttpRequest 是“解析结果对象”，不是“解析器对象”。
```

也就是说：
- 它回答的是“请求长什么样”
- 不是“请求怎么从 TCP 字节流里被拆出来”

#### 学习问答记录
**Q：什么是 Cookie？**

**A：**
Cookie 是服务器让浏览器保存的一小段键值数据。服务器在响应里通过 `Set-Cookie` 下发，浏览器后续请求同一站点时会自动通过 `Cookie` 请求头带回。

在当前 HTTP 框架里，Cookie 主要用于会话识别：

1. 服务器首次响应下发 `Set-Cookie: SID=...`；
2. 浏览器保存 SID；
3. 后续请求自动带 `Cookie: SID=...`；
4. 服务端通过 `SessionManager` 按 SID 找到对应 Session。

关键点：
- Cookie 里通常只放“标识”（如 SID），不是完整会话数据；
- 真正会话状态应保存在服务端 Session 中；
- 常见安全属性包括 `HttpOnly`、`Secure`、`SameSite`。

---

### 0.2 `http_response.h / http_response.cc`
#### 类/文件作用
`HttpResponse` 的作用是承载“准备返回给客户端的一条 HTTP 响应”。

它自己不负责：
- 网络发送
- socket 管理

它只负责把响应语义组织好，最终由 `HttpSession::sendResponse()` 发出去。

#### 具体设计了什么
当前版本主要包含：

- `status`：HTTP 状态码（200/404/500...）
- `versionMajor/versionMinor`：HTTP 版本
- `keepalive`：是否保持连接
- `reason`：状态描述（可自定义）
- `headers`：普通响应头
- `setCookies`：`Set-Cookie` 头列表
- `body`：响应体

并提供：
- `setHeader/getHeader`
- `addSetCookie`
- `toString()`（序列化为完整 HTTP 响应报文）

#### 为什么这么设计
1. **解耦业务和网络层**
   - Servlet 只负责填 `HttpResponse`
   - 网络发送由 `HttpSession` 统一负责

2. **保证响应格式一致**
   - 所有响应最终都走 `toString()` 统一序列化

3. **支持多 Set-Cookie**
   - `Set-Cookie` 允许同一响应出现多行
   - 不能简单放在普通 header map 里覆盖

#### 当前阶段需要注意的点
1. `toString()` 会自动补 `Connection` 和 `Content-Length`（若未手动设置）
2. `reason` 不设置时会使用 `HttpStatusToString()` 默认文本
3. `keepalive` 影响 `Connection: keep-alive/close` 输出语义

#### 学习这个类时最该抓住的点

```text
HttpResponse 是“业务填充的响应对象”，不是“直接写 socket 的工具类”。
```

也就是说：
- 它回答的是“我要回什么”
- 不是“怎么把字节发出去”

---

### 0.3 `http_parser.h / http_parser.cc`
#### 类/文件作用
`HttpRequestParser` 的作用是把“连接缓冲区里的原始字节流”解析成 `HttpRequest` 对象。

它不负责 socket 读写，也不负责路由分发，只负责协议解析。

#### 为什么这个类是 HTTP 框架的核心
因为 TCP 是字节流，没有消息边界。HTTP 的半包/粘包问题，最终都要在这里解决。

当前解析器的核心边界规则是：

1. 先找 `\r\n\r\n`，确定 header 结束；
2. 解析请求行和 headers；
3. 再根据 `Content-Length` 判断 body 是否完整；
4. 完整则返回 `HttpRequest`，并告知已消费字节数。

#### 具体设计了什么
- `parse(const std::string& buffer, size_t& consumed)`
  - 输入连接级缓冲区
  - 成功时返回请求对象并给出消费字节数
  - 不完整时返回空并继续等待更多数据
  - 格式错误时设置错误状态

- `hasError()/getError()/reset()`
  - 用于把解析错误状态传给 `HttpSession/HttpServer`

#### 为什么 parse 要返回 consumed
因为 keep-alive 下同一连接会连续出现多个请求。

解析出第一条请求后，必须知道“这次用了多少字节”，这样才能：

- 从缓冲区删掉已消费部分
- 把剩余字节留给下一条请求继续解析

这就是支持粘包场景的关键。

#### 当前阶段需要注意的点
1. 当前版本只支持 `Content-Length`，不支持 chunked request body
2. 当前 query/cookie 解析是最小可用版，未做 URL decode
3. 当前 header key 按原样保存，未做大小写归一化

#### 学习这个类时最该抓住的点

```text
HttpParser 负责“把字节流切成请求对象”，是 HTTP 协议层的边界处理核心。
```

---

### 0.4 `http_session.h / http_session.cc`
#### 类/文件作用
`HttpSession` 的作用是把“TCP 字节流连接”提升为“HTTP 会话连接”。

它位于：

- 下层：`SocketStream`（只管字节读写）
- 上层：`HttpServer/Servlet`（关心请求和响应语义）

之间，起桥接作用。

#### 具体设计了什么
当前版本核心能力是两个方法：

- `recvRequest()`：从连接中读取并解析出一条 `HttpRequest`
- `sendResponse()`：把 `HttpResponse` 序列化后发回客户端

核心成员：

- `HttpRequestParser m_parser`
- `std::string m_buffer`

其中 `m_buffer` 是连接级缓冲区，负责跨多次 `read()` 累积数据。

#### 为什么这么设计
`HttpRequestParser` 本身只会“解析给定缓冲区”，不会自己读 socket。

所以必须有一个会话层来负责：

1. 持续从 socket 读数据；
2. 追加到连接缓冲区；
3. 调 parser 尝试解析；
4. 成功后根据 consumed 删除已消费字节；
5. 保留剩余字节给下一条请求。

这就是 `HttpSession` 存在的根本原因。

#### 当前阶段需要注意的点
1. `HttpSession` 才是“半包/粘包闭环处理”的连接级入口
2. `HttpParser` 负责协议规则，`HttpSession` 负责读循环和缓冲生命周期
3. keep-alive 场景下，同一连接会连续调用多次 `recvRequest()`

#### 学习这个类时最该抓住的点

```text
HttpSession = 连接级缓冲管理 + Parser 调度 + Response 发送。
```

它是 HTTP 协议层和网络层的关键桥梁。

#### 学习问答记录

**Q1：什么是"连接级缓冲区"（m_buffer）？**

**A：**

"连接级缓冲区"是指生命周期与 TCP 连接绑定的缓冲区。

**为什么需要它？**

因为 TCP 是字节流协议，每次 `read()` 可能读到：
- 半个 HTTP 请求（半包）
- 一个完整请求
- 一个完整请求 + 下一个请求的一部分（粘包）
- 多个完整请求（粘包）

例如：
```
第一次 read(): "GET /api HTTP/1.1\r\nHost: loc"  ← 只读到一半
第二次 read(): "alhost\r\n\r\n"                  ← 补全了请求头
```

或者：
```
第一次 read(): "GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n"  ← 两个完整请求粘在一起
```

**什么是"连接级"？**

"连接级"的意思是：这个缓冲区的生命周期跟 TCP 连接绑定。

- 一个 `HttpSession` 对应一个 TCP 连接
- 这个连接可能会收到多个 HTTP 请求（keep-alive）
- `m_buffer` 会一直存在，跨越多次 `recvRequest()` 调用

**工作流程：**

```
1. 第一次调用 recvRequest():
   - read() 读到 "GET /a HTTP..."，追加到 m_buffer
   - parser 解析出第一个请求
   - 从 m_buffer 删除已消费的字节
   - 剩余字节留在 m_buffer 里

2. 第二次调用 recvRequest():
   - m_buffer 里可能还有上次剩下的数据
   - 继续 read() 追加新数据
   - parser 继续解析...
```

**对比：如果没有连接级缓冲区会怎样？**

如果每次 `recvRequest()` 都用临时缓冲区，那么：
- 半包时，读到的不完整数据会丢失
- 粘包时，第二个请求的数据会丢失

所以必须有一个"活得跟连接一样长"的缓冲区来保存这些跨调用的数据。

---

**Q2：接收一个 HTTP 请求的完整过程是怎样的？涉及哪些缓冲区？**

**A：**

整个过程涉及**三层缓冲区**：

```
客户端发送
    ↓
[1] 操作系统内核 socket 接收缓冲区（TCP 层）
    ↓
[2] read() 临时栈缓冲区（char buffer[4096]）
    ↓
[3] HttpSession 连接级缓冲区（m_buffer）
    ↓
解析器读取
```

**详细流程：**

**步骤 0：客户端发送数据**
```c
send(sockfd, "GET /api HTTP/1.1\r\nHost: localhost\r\n\r\n", ...);
```

**步骤 1：数据到达操作系统内核**

服务器的网络协议栈接收到 TCP 数据包后，把数据放入**内核 socket 接收缓冲区**。

此时：
```
内核 socket 缓冲区: "GET /api HTTP/1.1\r\nHost: localhost\r\n\r\n"
应用程序还不知道有数据到达
```

**步骤 2：HttpSession::recvRequest() 被调用**

第一次循环：
```cpp
// 此时 m_buffer 是空的（新连接）
m_buffer = ""

// 尝试解析
HttpRequest::ptr request = m_parser.parse(m_buffer, consumed);
// 返回 nullptr（因为 m_buffer 是空的）

// 需要读取数据
char buffer[4096];  // ← 临时栈缓冲区
int rt = read(buffer, sizeof(buffer));
```

**步骤 3：read() 系统调用（SocketStream 的作用）**

这里的 `read()` 实际上是 `SocketStream::read()`，因为 `HttpSession` 继承自 `SocketStream`。

**完整的调用链路：**
```
HttpSession::recvRequest()
    ↓ 调用 read(buffer, 4096)
SocketStream::read(buffer, 4096)
    ↓ 调用 m_socket->recv()
Socket::recv()
    ↓ 调用系统调用 ::recv(fd, buffer, 4096, 0)
内核 socket 缓冲区
```

**SocketStream 的价值：**
- 封装了 socket 的基础读写操作（read/write/readFixSize/writeFixSize）
- 提供了统一的 Stream 接口（后续可扩展 SSLSocketStream）
- 管理 socket 生命周期（通过 m_owner 控制是否负责关闭）
- 提供便利方法（writeFixSize 保证循环写直到全部发送完毕）

`read(buffer, 4096)` 做了什么：
1. 从内核 socket 缓冲区拷贝数据到应用程序的 buffer
2. 最多拷贝 4096 字节
3. 返回实际读取的字节数

**关键点：read() 不保证读到完整的 HTTP 请求！**

可能的情况：

- **情况 A：读到完整请求**
  ```
  read() 返回: rt = 41
  buffer 内容: "GET /api HTTP/1.1\r\nHost: localhost\r\n\r\n"
  ```

- **情况 B：只读到一半（半包）**
  ```
  read() 返回: rt = 23
  buffer 内容: "GET /api HTTP/1.1\r\nHo"
  ```

- **情况 C：读到多个请求（粘包）**
  ```
  read() 返回: rt = 42
  buffer 内容: "GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n"
  ```

**步骤 4：追加到连接级缓冲区**

```cpp
m_buffer.append(buffer, rt);
```

**为什么需要这一步？**

因为 `buffer[4096]` 是栈上的临时变量，函数返回后就销毁了。

必须把数据保存到**连接级缓冲区 m_buffer**，这样：
- 半包时，下次 read() 的数据可以接上
- 粘包时，第一个请求处理完后，剩余数据还在

**步骤 5：回到循环开头，尝试解析**

```cpp
size_t consumed = 0;
HttpRequest::ptr request = m_parser.parse(m_buffer, consumed);
```

解析器做什么：
1. 在 `m_buffer` 中查找 `\r\n\r\n`（请求头结束标志）
2. 解析请求行：`GET /api HTTP/1.1`
3. 解析请求头：`Host: localhost`
4. 检查 `Content-Length`（如果有 body）
5. 判断是否完整

如果完整：
```cpp
request != nullptr
consumed = 41  // 消费了 41 字节
```

如果不完整（半包）：
```cpp
request == nullptr
consumed = 0
m_parser.hasError() == false  // 不是错误，只是数据不够
```

此时继续循环，再次 `read()` 读取更多数据。

**步骤 6：删除已消费的字节**

```cpp
if (request)
{
    m_buffer.erase(0, consumed);  // 删除前 41 字节
    return request;
}
```

**为什么要删除？**

假设是粘包场景：
```
m_buffer = "GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n"
                                 ↑
                            consumed = 20
```

删除后：
```
m_buffer = "GET /b HTTP/1.1\r\n\r\n"  // 第二个请求留在缓冲区
```

下次调用 `recvRequest()` 时，会直接从 `m_buffer` 解析出第二个请求，不需要再 `read()`。

**三个缓冲区的作用总结：**

| 缓冲区 | 位置 | 生命周期 | 作用 |
|--------|------|----------|------|
| **内核 socket 缓冲区** | 操作系统内核 | 连接存在期间 | TCP 层接收网络数据 |
| **临时栈缓冲区 buffer[4096]** | 函数栈 | 单次 read() 调用 | 从内核拷贝数据到应用程序 |
| **连接级缓冲区 m_buffer** | HttpSession 对象 | 连接存在期间 | 跨多次 read() 累积数据，支持半包/粘包 |

**关键理解：**

1. **read() 是不可控的**：你不知道一次能读多少字节
2. **m_buffer 是缓冲层**：把不可控的 read() 变成可控的完整请求
3. **循环是必须的**：因为可能需要多次 read() 才能凑齐一个完整请求
4. **erase 是必须的**：因为粘包时要把剩余数据留给下一个请求
5. **SocketStream 是抽象层**：让 HttpSession 专注于 HTTP 协议逻辑，不用关心底层 socket 读写细节

---

### 0.5 `servlet.h / servlet.cc`
#### 类/文件作用
这一组文件负责 HTTP 框架里的“业务处理抽象 + 路由分发”。

它们不做协议解析，不直接操作 socket，而是定义：

- `Servlet`：统一业务处理接口（`handle(request, response, session)`）
- `FunctionServlet`：把 lambda / 回调包装成 Servlet
- `ServletDispatch`：根据 URI 选择最终要执行的 Servlet

可以把这层理解为：

```text
HTTP 请求进入后，业务逻辑真正落地执行的入口层
```

#### 为什么这么设计
1. **协议层与业务层解耦**
   - `HttpSession/HttpParser` 只负责把字节流变成请求对象
   - `Servlet` 只负责“拿到请求后怎么处理”

2. **统一处理接口**
   - 无论是 `/ping` 还是后续 `/chat/*`，最终都走同一套 `handle()` 入口

3. **路由能力独立封装**
   - `ServletDispatch` 集中管理精确匹配 + 通配匹配 + 默认兜底
   - 业务代码不需要关心路由查找细节

4. **降低开发门槛**
   - `FunctionServlet` 支持直接注册 lambda，避免每个接口都写继承类

#### 当前阶段需要注意的点
1. 路由匹配顺序是：**精确匹配 -> 通配匹配 -> 默认 Servlet(404)**
2. 通配匹配当前是简化版前缀规则（如 `/api/*`）
3. `ServletDispatch` 本身也是 `Servlet`，这让“分发器可被当作普通 Servlet 使用”
4. 目前路由容器使用 `vector` 线性查找，优先可读性，性能优化可在后续阶段再做

#### 学习这个类时最该抓住的点

```text
Servlet 是业务执行抽象，ServletDispatch 是路由入口，二者共同把 HTTP 协议层和业务处理层解耦。
```

#### 学习问答记录
**Q：精确路由和通配路由有什么区别？**

**A：**

区别在于匹配规则：

- **精确路由**：必须 URI 完全相等才命中。
  - 例如注册 `/ping`，只匹配 `/ping`，不匹配 `/ping/1`。
- **通配路由**：按模式命中，一条规则可以覆盖一批 URI。
  - 当前实现是简化版前缀通配（如 `/api/*`），可匹配 `/api/chat`、`/api/user/info`。

当前匹配顺序是：

1. 先查精确路由
2. 再查通配路由
3. 最后走默认 Servlet（通常是 404）

所以如果同时存在：
- 精确路由 `/api/chat`
- 通配路由 `/api/*`

那么请求 `/api/chat` 会优先命中精确路由。

---

### 0.6 `http_server.h / http_server.cc`
#### 类/文件作用
`HttpServer` 是 HTTP 框架的服务器入口，负责把 `TcpServer` 接收到的连接按 HTTP 语义进行处理。

它的核心职责不是“自己去 accept 连接”，而是复用 `TcpServer` 的网络能力，并在 `handleClient()` 中补上 HTTP 处理主链路。

#### 为什么这么设计
1. **复用已有 TCP 基础设施**
   - `TcpServer` 已经具备连接接入、调度和生命周期管理能力
   - `HttpServer` 只需要专注 HTTP 协议层流程

2. **主链路集中在一个入口函数**
   - `handleClient()` 统一组织：收请求 -> 路由分发 -> 发响应 -> keep-alive 决策
   - 便于后续在同一位置加入鉴权、日志、限流、观测等中间逻辑

3. **路由与会话能力内聚**
   - `m_dispatch` 承担路由分发
   - `m_sessionManager` 承担 SID 会话管理
   - 让业务代码通过 `getServletDispatch()` 注册路由即可

#### 当前阶段需要注意的点
1. 解析失败时会返回 `400 Bad Request`，并关闭连接
2. 每个请求都会创建 `HttpResponse`，并继承请求版本与 keep-alive 语义
3. 连接是否继续由双条件决定：`request->isKeepAlive()` 与 `response->isKeepAlive()`
4. `handleClient()` 循环结束后会主动 `session->close()`

#### 学习这个类时最该抓住的点

```text
HttpServer = TcpServer 的 HTTP 语义适配层，核心价值在 handleClient() 的请求生命周期编排。
```

#### `handleClient` 时序图（成功 / 400 / 断连）

```text
参与者：Client, HttpServer::handleClient, HttpSession, HttpRequestParser, ServletDispatch, SessionManager

主循环开始（while !isStop）
Client -> HttpSession: 发送字节流
HttpSession -> HttpRequestParser: parse(m_buffer)

分支 A：请求成功解析
HttpRequestParser --> HttpSession: request + consumed
HttpSession --> HttpServer::handleClient: HttpRequest
HttpServer::handleClient -> SessionManager: getOrCreate(request, response)
SessionManager --> HttpServer::handleClient: Session（必要时在 response 写 Set-Cookie）
HttpServer::handleClient -> ServletDispatch: handle(request, response, session)
ServletDispatch --> HttpServer::handleClient: 业务已填充 response
HttpServer::handleClient -> HttpSession: sendResponse(response)
HttpSession --> Client: HTTP 响应

  keep-alive 判断：
  - request.keepalive=true 且 response.keepalive=true -> 继续下一轮循环
  - 任一为 false -> 跳出循环并关闭连接

分支 B：解析错误（400）
HttpRequestParser --> HttpSession: nullptr + parserError
HttpSession --> HttpServer::handleClient: request=null
HttpServer::handleClient -> HttpSession: hasParserError() == true
HttpServer::handleClient: 构造 400 Bad Request 响应（body 附 parserError）
HttpServer::handleClient -> HttpSession: sendResponse(400)
HttpSession --> Client: 400 Bad Request
HttpServer::handleClient: break -> close

分支 C：连接断开/读失败
HttpSession: recvRequest() 读取失败(rt<=0) 或对端关闭
HttpSession --> HttpServer::handleClient: request=null（且无 parserError）
HttpServer::handleClient: break -> close

循环结束后统一收尾：
HttpServer::handleClient -> HttpSession: close()
HttpServer::handleClient: log "HttpServer client closed"
```

#### 学习问答记录
**Q：到底什么是 keep-alive？**

**A：**

`keep-alive` 的含义是：一个 TCP 连接不只服务一个 HTTP 请求，而是可以复用来处理后续多个请求。

对比：

- **不 keep-alive（短连接）**：1 个请求 -> 建连 -> 响应 -> 关闭连接
- **keep-alive（长连接）**：1 次建连 -> 多次请求/响应 -> 最后再关闭

它的价值：

1. 减少频繁建连/断连开销
2. 降低请求延迟
3. 提高吞吐能力

在当前实现中的体现：

1. `HttpSession` 持有连接级 `m_buffer`，可连续解析同一连接上的多条请求
2. `HttpServer::handleClient()` 用循环持续处理请求
3. 只有在以下情况才结束连接：
   - `!request->isKeepAlive()`
   - `!response->isKeepAlive()`
   - 发送失败 / 读取失败 / 解析错误

补充：HTTP/1.1 通常默认 keep-alive（除非显式 `Connection: close`）。

**Q：AI 对话服务里，普通请求和流式请求是不是可以用不同连接策略？**

**A：**

可以。业务层可以按接口类型决定连接策略，但要区分“业务语义”和“传输优化”：

1. **普通对话请求（非流式）**
   - 可以选择 `Connection: close`（短连接）
   - 也可以保留 keep-alive（通常性能更好）
   - 无论是否 keep-alive，业务都应按“请求级”处理，不把上下文绑定到连接

2. **流式输出（SSE）**
   - 单次响应期间连接会持续保持，直到流结束
   - 流结束后是否复用连接，由 `response->setKeepAlive(true/false)` 决定

结论：不是“普通必须关、流式必须开”，而是可以按接口目标选择。

**Q：WebSocket 和 keep-alive 有什么区别？**

**A：**

核心区别：

1. **keep-alive**
   - 仍然是 HTTP 协议
   - 仍然是请求-响应模型
   - 只是复用同一 TCP 连接处理多次 HTTP 请求

2. **WebSocket**
   - 先通过 HTTP Upgrade 握手
   - 升级后切换为 WebSocket 帧协议
   - 不再是 HTTP 请求-响应，而是全双工消息通道

场景对比：

- keep-alive：普通 API 请求复用连接
- WebSocket：需要实时双向通信（服务端主动推送）

一句话：**keep-alive 是 HTTP 连接复用；WebSocket 是升级后的长期双向消息协议。**

#### 第二阶段补充（简述）

第二阶段在本模块上的改动已完成：

1. `HttpRequestParser` 增加请求头/请求体大小限制
2. `HttpServer` 增加 `400` 与 `413` 错误区分
3. 路由层补充了扩展方向说明

详细设计与测试结果统一记录在文末 `## 第二阶段` 章节。

---

### 0.7 `session.h / session.cc / session_manager.h / session_manager.cc`
#### 类/文件作用
这一组文件负责 HTTP 框架里的服务端会话能力（Session）。

- `Session`：单个会话对象，保存 SID、时间信息和会话键值数据
- `SessionManager`：会话仓库与生命周期管理器（创建、查找、续期、过期清理）

它与 `HttpSession` 的区别是：

- `HttpSession` 是**连接级**（TCP 连接生命周期）
- `Session` 是**用户会话级**（通过 Cookie SID 跨连接保持）

#### 为什么这么设计
1. **把连接和业务会话解耦**
   - TCP 连接会断开重连
   - 业务会话需要跨请求、跨连接延续

2. **统一 SID 管理入口**
   - `getOrCreate()` 统一处理 Cookie SID 的读取、校验、创建和回写

3. **先做内存最小闭环**
   - 当前先用 `map<string, Session::ptr>` 跑通机制
   - 后续可平滑替换为 Redis/数据库存储

#### 当前阶段需要注意的点
1. `Session` 数据模型当前是 `string -> string`
2. `getOrCreate()` 可能通过 `Set-Cookie` 下发新 SID
3. `get()` 命中过期会话时会直接删除并返回空
4. `sweepExpired()` 是批量清理入口，适合定时任务调用

#### 学习这个类时最该抓住的点

```text
SessionManager 负责“SID 到服务端状态”的映射与生命周期，是通用有状态 HTTP 能力的基础。
```

#### 第二阶段补充（简述）

第二阶段在本模块上的改动已完成：

1. `SessionManager` 增加周期性过期清理定时器能力
2. 形成“访问时清理 + 后台定时清理”的组合机制
3. 已补齐 `test_session_manager` 生命周期测试

详细设计与测试结果统一记录在文末 `## 第二阶段` 章节。

#### 学习问答记录
**Q：这个 Session 是干什么的？和 HttpSession 的区别是什么？**

**A：**

`Session` 是服务端业务会话对象，用来保存“用户状态/业务状态”。

例如当前可以存：
- `user_id`
- `conversation_id`
- 鉴权标记或其他业务键值

它的生命周期不是跟 TCP 连接走，而是跟“会话有效期”走。

`HttpSession` 则是连接级对象，职责是 HTTP 收发与解析协作。

区别可概括为：

- `HttpSession`：连接级，负责“这条连接如何收发请求/响应”
- `Session`：业务会话级，负责“这个用户/会话当前状态是什么”

一句话：`HttpSession` 管连接，`Session` 管状态。

---

### 0.8 `sse.h / sse.cc`
#### 类/文件作用
`SSEWriter` 是一个轻量级 SSE 输出工具，负责把事件数据编码为 SSE 协议格式，并写入当前 HTTP 连接。

它不负责：
- 路由接入
- 客户端连接池管理
- 多客户端广播

它只负责：
- 给定一条事件数据，按 SSE 规则格式化并发送

#### 为什么这么设计
1. **职责单一**
   - `HttpServer/Servlet` 负责请求路由和业务流程
   - `SSEWriter` 只负责协议格式化与写出

2. **复用已有连接抽象**
   - 直接复用 `HttpSession` 的写能力（`writeFixSize`）
   - 不重复实现网络发送逻辑

3. **为 AI 流式输出预留能力**
   - `sendEvent()` 适合 token 增量输出
   - `sendComment()` 可用于心跳保活

#### 当前阶段需要注意的点
1. `sendEvent()` 会把多行 data 拆成多行 `data: ...`
2. 空 data 会输出 `data: \n`，保证事件格式合法
3. 每条事件以空行结束（`\n\n`），这是 SSE 帧边界
4. 当前只实现单连接写出，不包含广播/订阅模型

#### 学习这个类时最该抓住的点

```text
SSEWriter 是“协议编码器 + 连接写出器”，用于把普通文本变成可被 EventSource 消费的 SSE 事件流。
```

#### 学习问答记录
**Q1：什么是事件数据？**

**A：**

在 SSE（Server-Sent Events）里，事件数据就是一条事件里真正要传给前端业务层的内容，对应 `data:` 字段。

可以把一条 SSE 事件拆成两部分：

1. **元信息（可选）**
   - `event:` 事件名
   - `id:` 事件 ID
   - `retry:` 建议重连间隔
2. **正文内容（核心）**
   - `data:` 事件数据

示例：

```text
event: token
id: 1001
data: 你好，这是一段增量输出

```

这里前端真正展示给用户的核心内容，就是 `data` 里的文本。

如果有多行 `data:`：

```text
data: 第一行
data: 第二行

```

这两行会被客户端当成同一条事件的数据（中间按换行拼接）。

**Q2：SSE 协议格式是什么？**

**A：**

SSE 是一个基于 HTTP 的文本流协议，核心规则是：
**按行写字段，按空行分隔事件帧。**

一条完整事件帧示例：

```text
event: message
id: 42
retry: 3000
data: hello
data: world

```

格式要点：

- 每个字段一行：`字段名: 值`
- `data:` 可以出现多次（多行数据）
- 事件最后必须有一个空行（`\n\n`）作为帧结束
- 以 `:` 开头的是注释帧（常用于心跳），例如 `: ping\n\n`

常见响应头：

- `Content-Type: text/event-stream`
- `Cache-Control: no-cache`
- `Connection: keep-alive`

**Q3：SSE 协议的作用是什么？**

**A：**

SSE 的作用是：
**让服务端可以主动、持续地向客户端推送消息**，而不需要客户端不断轮询。

在 AI 对话服务器场景中，最直接的价值是：

- 模型每生成一段 token 就推给前端
- 前端边收边显示
- 用户不必等整段答案完成才看到内容

典型收益：

1. **更实时**：消息到就推，不等下一次轮询
2. **更省资源**：减少空轮询请求
3. **实现更轻**：基于 HTTP，不必一开始上 WebSocket
4. **天然重连能力**：可配合 `id/retry` 做断线重连

**Q4：SSE 协议的作用机制？**

**A：**

SSE 不是“发很多个 HTTP 响应”，而是：

1. 客户端发起一次 HTTP 请求（通常 `GET /events`）
2. 服务端返回 SSE 相关响应头，并保持连接不立即关闭
3. 服务端在同一个响应体里持续写事件帧（`data: ...\n\n`）
4. 客户端 `EventSource` 持续读取流，遇到空行就触发一次事件回调
5. 若连接中断，客户端可按 `retry` 自动重连（可结合 `Last-Event-ID`）

本质是：
**单请求 + 长响应流 + 多事件帧。**

**Q5：持续追加响应体时，客户端怎么能看到？对应代码中的哪一行？**

**A：**

客户端能实时看到，前提是服务端每次都把数据真实写入 socket/TCP 流，而不是只在内存中拼接字符串。

在当前实现里，关键代码是：

```cpp
return m_session->writeFixSize(payload.c_str(), payload.size());
```

位置：

- `http/stream/sse.cc` 的 `SSEWriter::sendEvent()`
- `http/stream/sse.cc` 的 `SSEWriter::sendComment()`

机制对应关系：

1. `payload` 先按 SSE 格式组装（如 `data: xxx\n\n`）
2. `writeFixSize(...)` 把字节写到连接（socket）
3. 客户端收到字节后，`EventSource` 解析到 `\n\n`
4. 触发前端回调，页面更新

实践注意：若链路中有代理缓冲，可能出现“服务端已写出但前端延迟看到”的现象。

**Q6：什么是 SSE 注释帧，为什么要有这个东西？**

**A：**

SSE 注释帧是以 `:` 开头的特殊行，不承载业务数据，常见形式是：

```text
: ping

```

在当前代码里由 `SSEWriter::sendComment()` 发送。

它的核心用途是“心跳保活”：

1. 告知链路两端连接仍然存活
2. 降低代理/NAT/负载均衡因长时间无数据而断开连接的概率
3. 不污染业务事件流（注释帧不是普通 `data` 事件）

所以注释帧不是给业务层传 token 的，而是给连接层“续命”的。

#### 第二阶段补充（简述）

第二阶段在本模块上的改动已完成：

1. 引入流式响应语义（`setStream`/`toHeaderString`）
2. 完成 `/events` SSE demo 接入
3. 补齐 `test_sse_writer` 与 `test_sse_server`

详细设计与测试结果统一记录在文末 `## 第二阶段` 章节。

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

## 第二阶段：生命周期与流式能力增强

### 0. 第二阶段改动类速览
#### 0.1 `session_manager.h / session_manager.cc`
- 新增周期清理接口：`startSweepTimer()` / `stopSweepTimer()` / `hasSweepTimer()`
- 接入 `TimerManager::addConditionTimer(...)`，形成后台自动过期清理能力

#### 0.2 `http_response.h / http_response.cc`
- 新增流式响应语义：`setStream()` / `isStream()`
- 新增 `toHeaderString()`，支持“仅发送响应头”的流式写出模型

#### 0.3 `http_server.h / http_server.cc`
- `handleClient()` 新增流式分支（`response->isStream()`）
- 解析错误按类型区分返回 `400` 或 `413`

#### 0.4 `http_parser.h / http_parser.cc` + `http_session.h`
- 请求头/请求体大小限制接入（默认 8KB / 10MB）
- 新增错误类型区分：普通格式错误 vs 请求过大
- `HttpSession` 暴露 `isRequestTooLarge()` 供 `HttpServer` 决策

#### 0.5 `sse.h / sse.cc`
- 完成 SSE 工具类链路验证（注释帧 + 事件帧）
- 与 `HttpServer` 流式分支配合，形成可用 SSE demo

#### 0.6 测试改动
- 新增：`test_session_manager`、`test_sse_writer`、`test_sse_server`
- 增强：`test_http_parser`、`test_http_server`

### 1. 本阶段目标
#### 模块作用
在第一阶段“HTTP 框架最小闭环”已经跑通的基础上，第二阶段的目标是把框架推进到：

- 生命周期更完整
- 流式能力真正可接入
- 面对异常请求时更健壮
- 有 demo、可测试、可验证

#### 设计理念
这一阶段仍然坚持“先做通用 HTTP 框架，不做 AI 业务协议”的原则。

因此重点不是 `/chat` 这类应用层接口，而是补齐通用能力：

- `SessionManager` 生命周期增强
- SSE 通用接入示例
- `HttpServer` 健壮性增强
- 路由层扩展点梳理

---

### 2. 本阶段完成项
#### 设计要点

#### 1. Session 生命周期增强
- 为 `SessionManager` 增加了周期性过期清理定时器：
  - `startSweepTimer()`
  - `stopSweepTimer()`
  - `hasSweepTimer()`
- 形成“请求访问时顺带清理 + 后台定时清理”的组合方案

实现细节：
- `SessionManager` 继承 `enable_shared_from_this`
- 定时清理通过 `TimerManager::addConditionTimer(...)` 接入
- 回调使用 `weak_ptr` 保护对象生命周期，避免悬空访问

**为什么必须用 `weak_ptr` 而不是 `shared_ptr`？**

如果定时器回调直接捕获 `shared_ptr<SessionManager>`：

```cpp
// 错误写法
SessionManager::ptr self = shared_from_this();
m_sweepTimer = manager->addTimer(
    sweep_interval_ms,
    [self]() {  // 捕获了 shared_ptr
        self->sweepExpired();
    },
    true
);
```

问题：
1. 定时器回调持有 `shared_ptr`，增加了 `SessionManager` 的引用计数
2. 即使外部代码已经不再需要这个 `SessionManager`，引用计数也不会归零
3. 只要定时器没取消，`SessionManager` 就无法被销毁
4. 结果：内存泄漏 + 对象僵尸化

正确做法（用 `weak_ptr`）：

```cpp
std::weak_ptr<SessionManager> weak_self = shared_from_this();
m_sweepTimer = manager->addConditionTimer(
    sweep_interval_ms,
    [weak_self]() {  // 捕获 weak_ptr
        SessionManager::ptr self = weak_self.lock();  // 尝试提升
        if (!self) {
            return;  // 对象已销毁，安全退出
        }
        self->sweepExpired();
    },
    weak_self,
    true
);
```

优势：
1. `weak_ptr` 不增加引用计数，只是"观察权"而非"所有权"
2. 外部释放引用后，`SessionManager` 可以正常销毁
3. 回调触发时先 `lock()` 检查对象是否还活着：
   - 活着 → 执行清理
   - 已死 → 安全退出，不访问悬空内存

生命周期对比：

```text
【用 shared_ptr 捕获（错误）】
外部代码持有 SessionManager
    ↓
外部代码释放引用
    ↓
定时器回调仍持有 shared_ptr  ← 引用计数不为 0
    ↓
SessionManager 无法销毁  ← 内存泄漏

【用 weak_ptr 捕获（正确）】
外部代码持有 SessionManager
    ↓
外部代码释放引用
    ↓
定时器回调持有 weak_ptr  ← 不影响引用计数
    ↓
SessionManager 正常销毁
    ↓
下次定时器触发时 weak_ptr.lock() 返回空  ← 安全退出
```

一句话总结：**`weak_ptr` 让定时器回调变成"观察者"而不是"所有者"，避免定时器把 `SessionManager` 的生命周期强行延长。**

当前取舍：
- 先做内存态生命周期闭环，不引入 Redis/DB
- 先保证线程安全和可验证性，后续再做容量治理与淘汰策略

#### 2. SSE 通用接入落地
- 给 `HttpResponse` 增加流式响应语义：
  - `setStream(true)`
  - `toHeaderString()`
- `HttpServer::handleClient()` 识别流式响应，允许业务代码手动写 header/body
- 新增 `/events` 风格的 SSE demo，并验证注释帧与事件帧都能被客户端收到

为什么改：
- 第一阶段的 `SSEWriter` 只是“协议写出工具”，还没有接入 `HttpServer` 主链路
- `HttpServer` 默认会执行 `sendResponse(response)`，这适合一次性响应，不适合 SSE 这种“先发头、再持续写事件帧”的模型
- 因此需要引入流式响应语义，让服务端区分“普通响应”和“流式响应”

改了什么：
1. `HttpResponse` 增加流式语义
   - `setStream(true)` / `isStream()`
   - `toHeaderString()`：仅序列化响应头，不补 `Content-Length`
2. `HttpServer::handleClient()` 增加流式分支
   - `response->isStream() == true` 时跳过默认 `sendResponse()`
   - 由业务代码自行发送响应头和后续数据
3. 新增 `/events` 示例链路（在测试中验证）
   - 手动写响应头
   - 用 `SSEWriter::sendComment()` 发送心跳注释帧
   - 用 `SSEWriter::sendEvent()` 发送标准事件帧

请求怎么流转（SSE）：
1. 客户端发起 `GET /events`
2. `HttpServer` 收到请求并路由到对应 Servlet
3. Servlet 设置 `response->setStream(true)` 并准备 SSE 头
4. Servlet 手动写出 `response->toHeaderString()`
5. Servlet 通过 `SSEWriter` 持续写事件帧（`data: ...\n\n`）
6. `HttpServer` 检测到 stream 标记，跳过默认 `sendResponse()`
7. 根据连接策略决定后续关闭或继续

SSE 写出到底层发送函数的完整链路：

```text
Servlet/lambda
  -> SSEWriter::sendEvent() / sendComment()
      -> HttpSession::writeFixSize(...)
          -> SocketStream::writeFixSize(...)
              -> Stream::writeFixSize(const void* buffer, size_t length)
                  -> 循环调用 write(...) 直到本次 payload 写满
                      -> Socket::send(...)
                          -> 系统调用 send/recvmsg
                              -> 内核 TCP 发送缓冲区
                                  -> 客户端 EventSource 读取并按 \n\n 分帧
```

关键点：
1. `sendEvent()` 只是“组装 SSE 协议文本”
2. 真正保证“完整写出一帧”的是 `writeFixSize(...)`
3. `Stream::writeFixSize(...)` 的价值是避免短写导致事件帧残缺

AI 对话场景中使用 SSE 的经典做法：

1. 增量输出策略
   - 模型每产出一个增量片段（token/短句）就调用一次 `sendEvent()`
   - 不建议每个字符都发一次，可做 20~100ms 或固定长度聚合

2. 心跳保活策略
   - 周期性调用 `sendComment("ping")`（如每 10~20 秒）
   - 防止代理/NAT/负载均衡在空闲时断开连接

3. 事件类型约定
   - `event: token`：正文增量
   - `event: meta`：模型信息/统计信息
   - `event: error`：中途失败
   - `event: done`：流结束

4. 收尾策略
   - 正常结束时发送 `done` 事件
   - 异常中断时发送 `error`（若连接仍可写）并尽快关闭
   - 是否复用连接由 `Connection/keep-alive` 策略决定

实现细节：
- 普通响应继续走 `sendResponse()`
- 流式响应由业务代码手动发送“响应头 + 事件帧”，服务端默认发送路径跳过
- SSE demo 验证了 `sendComment` 与 `sendEvent` 的完整链路

当前取舍：
- 先不引入 chunked response，保持最小改动可跑通
- 先用单连接示例验证协议链路，再考虑抽象化封装

#### 3. HttpServer 健壮性增强
- 给 `HttpRequestParser` 增加请求头大小限制（默认 8KB）
- 给 `HttpRequestParser` 增加请求体大小限制（默认 10MB）
- 增加错误分类，支持把“请求过大”返回为 `413 Payload Too Large`

实现细节：
- 请求头未收齐但缓冲区已超过上限时，直接进入“请求过大”错误
- `Content-Length` 超过体积上限时，直接拒绝解析
- `HttpServer::handleClient()` 根据错误类型区分返回 `400` 或 `413`

当前取舍：
- 限制项目前是解析器静态全局参数，优先保证行为一致和测试稳定
- 后续可改为服务实例级配置或配置文件驱动

#### 4. 路由层扩展点梳理
- 明确当前路由层继续保持“精确 -> 通配 -> 默认”的简单模型
- 梳理了后续可扩展方向：更高效路由结构、路径参数、钩子、中间件链

当前取舍：
- 第二阶段不强行重构路由层
- 把重心放在生命周期、流式能力、边界防护这三条主链路

---

### 3. 测试与验证
#### 当前验证结果
本阶段新增/增强测试：

- `test_session_manager`
- `test_sse_writer`
- `test_sse_server`
- `test_http_parser`（新增请求过大场景）
- `test_http_server`（新增 413 集成验证）

已验证通过：

- `./bin/test_session_manager`
- `./bin/test_sse_writer`
- `./bin/test_sse_server`
- `./bin/test_http_parser`
- `./bin/test_http_server`

---

### 4. 当前实现与后续优化
#### 设计取舍

**当前实现：**
- Session 仍然是纯内存版本
- SSE 采用“业务代码手动写 header/body”的最小流式方案
- 请求大小限制是解析器级静态全局限制
- 路由层继续保留简单线性匹配模型

**后续可优化：**
- Session 配置化清理周期、容量上限、LRU/惰性淘汰、外部存储
- SSE 支持更自然的 chunked/长流响应模型
- `HttpServer` 补充超时、限流、统一错误响应格式
- 路由层演进为参数路由 / 中间件链 / 更高效匹配结构

---

### 5. 阶段总结
#### 一句话总结
第二阶段的核心不是新增更多类，而是把第一阶段的骨架补成“更像一个真正可复用 HTTP 框架”的状态：

- Session 会自己清理
- SSE 可以真正接入服务端链路
- 请求过大时服务器有明确保护行为
- 整体行为有 demo 和测试托底

#### 当前结论
到这里，通用 HTTP 框架的第二阶段主目标已经完成。

### 6. 下一阶段
#### 第三阶段会怎么做
第三阶段将继续停留在“通用 HTTP 框架层”，不进入 AI 应用协议，实现重点是：

1. 参数路由（提升路由表达能力）
2. 请求处理拦截器（统一前后置处理）
3. 统一错误响应模型（400/404/413/500 收口）
4. 关键参数配置化（统一配置入口）
5. 测试体系升级（新增测试 + 回归）

---

## 第三阶段：框架化能力收敛

### 0. 第三阶段改动类速览
#### 0.1 `http_request.h / http_request.cc`
- 新增 route params 容器与接口：
  - `setRouteParam()` / `getRouteParam()` / `hasRouteParam()` / `clearRouteParams()`

#### 0.2 `servlet.h / servlet.cc`
- 新增参数路由：`addParamServlet()`
- 新增请求拦截器：`addPreInterceptor()` / `addPostInterceptor()`
- 路由顺序升级为：精确 -> 参数 -> 通配 -> 默认

#### 0.3 `http_error.h / http_error.cc`
- 新增统一错误响应入口：`ApplyErrorResponse()`
- 收口 400/404/413/500 的输出格式（支持 JSON / text）

#### 0.4 `http_framework_config.h / http_framework_config.cc`
- HTTP 配置项统一收敛到配置门面
- 已切换到 `ConfigVar` 体系，并标注每项重载/生效形式

#### 0.5 `http_server.h / http_server.cc`
- 统一错误响应接入
- 增加异常捕获保护
- `stop()` 接管 Session 清理定时器关闭，避免后台任务拖住退出

#### 0.6 `fiber_framework_config.h / fiber_framework_config.cc`（配置体系统一补充）
- 网络层配置项统一收敛为配置门面
- 与 HTTP 层配置风格保持一致，便于统一维护

#### 0.7 测试改动
- 新增：`test_http_dispatch`、`test_http_framework_config`
- 增强：`test_http_server`
- 回归覆盖：`test_http_parser`、`test_session_manager`、`test_sse_writer`、`test_sse_server`

### 1. 本阶段目标
#### 模块作用
第三阶段的目标，是把当前项目从“可用的 HTTP 框架”继续推进到“更像框架”的状态。

这一阶段不进入 AI 业务层，而是继续补通用 HTTP 基础设施：

- 参数路由
- 请求处理拦截器（Interceptor）
- 统一错误响应模型
- 关键参数配置化
- 测试体系升级

---

### 2. 本阶段完成项
#### 设计要点

#### 1. 参数路由
- `ServletDispatch` 新增参数路由注册：`addParamServlet()`
- 路由匹配顺序升级为：**精确 -> 参数 -> 通配 -> 默认**
- `HttpRequest` 新增 `route params` 容器与访问接口：
  - `setRouteParam()`
  - `getRouteParam()`
  - `hasRouteParam()`
  - `clearRouteParams()`

当前取舍：
- 先只支持整段参数（如 `/user/:id`）
- 不做正则匹配和多段捕获

##### 学习问答记录（路由匹配类型）

**Q：什么是精确匹配、参数路由匹配、通配匹配，以及它们之间有什么区别？**

**A：**

1. 精确匹配（Exact Match）
   - 路径必须完全相等才命中
   - 例：注册 `/user/me`，只匹配 `/user/me`，不匹配 `/user/42`
   - 适合固定接口地址

2. 参数路由匹配（Param Route Match）
   - 路由里用 `:name` 表示变量段
   - 例：注册 `/user/:id`，可匹配 `/user/42`，并提取 `id=42`
   - 适合"结构固定、某些段是动态值"的接口

3. 通配匹配（Glob Match）
   - 用 `*` 做宽泛匹配（当前版本是简化实现，主要是 `*` 或前缀 `prefix*`）
   - 例：`/user/*` 可以兜住一批 `/user/...` 路径
   - 适合兜底、通用处理、静态前缀类接口

区别（核心）：

1. 匹配严格程度
   - 精确匹配最严格
   - 参数路由次之（允许变量段）
   - 通配匹配最宽泛

2. 是否产生结构化参数
   - 精确：不产生参数
   - 参数路由：会产生 `route params`（如 `id=42`）
   - 通配：当前实现不产出参数

3. 典型用途
   - 精确：固定业务端点（`/ping`、`/health`）
   - 参数路由：资源型接口（`/user/:id`）
   - 通配：兜底/分组前缀处理（`/api/*`）

4. 当前框架优先级（非常重要）
   - **精确 > 参数 > 通配 > 默认**
   - 同一个请求如果同时可命中多种规则，按这个顺序选择

#### 2. 请求处理拦截器（Interceptor）
- 为避免和底层网络层 `hook` 命名冲突，本阶段统一使用 `Interceptor` 命名
- `ServletDispatch` 新增：
  - `addPreInterceptor()`
  - `addPostInterceptor()`
- pre interceptor 返回 `false` 时可中断后续业务处理
- post interceptor 在业务处理后、响应发送前执行

拦截器定义：
- 拦截器就是在“真正业务 `servlet->handle()` 前后”插入统一处理逻辑的机制。

当前实现形态：
- `PreInterceptor`：前置拦截器，返回 `bool`
- `PostInterceptor`：后置拦截器，返回 `void`

执行顺序（核心）：

在 `ServletDispatch::handle(...)` 里执行流程为：

1. 依次执行所有 pre
   - 若某个 pre 返回 `false`：
     - 业务 handler 不再执行
     - 仍然执行所有 post
     - 直接返回
2. 若所有 pre 都通过：
   - 执行路由匹配 + 目标 `servlet->handle(...)`
3. 执行所有 post
4. 返回业务 handler 的返回值

因此当前机制属于“可拦截 + 可统一收尾”模型。

这个设计能解决什么：

1. 减少重复代码
   - 不用在每个 servlet 里重复写日志/header/鉴权逻辑

2. 统一拒绝策略
   - pre 可在业务前统一拦截（如鉴权失败、参数非法）

3. 统一收尾能力
   - 即使 pre 拦截了请求，post 仍会执行，便于统一打点/收尾

实现细节（容易忽略）：

- 当 pre 返回 `false` 时，框架不会自动构造错误响应
- 因此 pre 自己需要把 `response` 填好（状态码/响应体/必要 header）
- 这一点在 `test_http_dispatch` 与 `test_http_server` 的 `/blocked` 路径有验证

当前取舍：
- 先做轻量拦截器，不直接上完整中间件链
- 先解决通用日志 / 统一 header / 简单拦截场景

#### 3. 统一错误响应模型
- 新增 `ApplyErrorResponse()` 统一构造错误响应
- 支持：
  - `400 Bad Request`
  - `404 Not Found`
  - `413 Payload Too Large`
  - `500 Internal Server Error`
- 默认错误格式升级为 JSON：

```json
{"code":400,"message":"Bad Request","details":"..."}
```

当前取舍：
- 优先统一框架错误出口
- 先做最小 JSON 错误体，不扩展业务码体系

#### 4. 配置化
- 新增 `HttpFrameworkConfig` 统一管理：
  - 请求头大小限制
  - 请求体大小限制
  - Session 清理周期
  - SSE 心跳建议值
  - 错误响应格式
- `HttpRequestParser` 的大小限制配置改为通过框架配置获取
- `HttpServer` 启动时自动按配置接入 Session 清理定时器

当前取舍：
- 先做代码级静态配置入口
- 暂不接外部配置文件系统

##### 学习问答记录（配置系统）

**Q1：为什么设置了回调不等于“完整热加载”？**

**A：**

回调只是热加载链路的最后一环。完整流程应包含：

1. 文件监控：监控配置文件变化（如 `inotify` / `stat`）
2. 触发重载：检测到变化后重新读取配置文件
3. 解析更新：`Config::LoadFromYaml` 更新 `ConfigVar`
4. 变更通知：`setValue()` 触发监听器回调
5. 业务响应：回调执行实际更新逻辑

当前项目状态：

- 已实现 3/4/5（配置更新、监听通知、业务响应）
- 尚未实现 1/2（文件监控与自动触发重载）

结论：当前具备“配置值变化后的热响应能力”，但不是完整的“文件级自动热加载系统”。

**Q2：通过 `ConfigVar` 实现配置项有哪些形式？**

**A：**

1. 启动参数型（构造时读取一次）
   - 示例：`scheduler.use_caller`、`iomanager.use_caller`
   - 特征：更适合启动时确定，运行中通常不变

2. 运行时动态读取型（每次使用前读取）
   - 示例：`fiber.pool.enabled`
   - 特征：配置变化后，后续调用立即体现

3. 新对象生效型（对后续创建对象生效）
   - 示例：`fiber.stack_size`、`fiber.use_shared_stack`、`fiber.shared_stack_size`
   - 特征：已创建对象不受影响，新创建对象使用新值

4. 监听器驱动缓存同步型（推荐热更新模式）
   - 示例：`tcp.connect.timeout`
   - 特征：配置变化通过 `addListener()` 立即同步到运行时缓存

**Q3：`tcp.connect.timeout` 的热重载完整流程是什么？**

**A：**

1. 在 `hook.cc` 注册配置项：
   - `Config::Lookup("tcp.connect.timeout", 5000, ...)`

2. `_HookIniter` 初始化时读取初值：
   - `s_connect_timeout = g_tcp_connect_timeout->getValue()`

3. 注册监听器：
   - `g_tcp_connect_timeout->addListener(...)`
   - 配置变化时将 `new_value` 写入 `s_connect_timeout`

4. 业务路径使用缓存值：
   - `connect` hook 最终调用 `connect_with_timeout(..., s_connect_timeout)`

5. 生效结果：
   - 配置变化后，监听器立刻更新缓存
   - 后续新的 `connect` 立即使用新超时

一句话：`tcp.connect.timeout` 能热生效，是 `ConfigVar + addListener + 运行时缓存变量` 联动的结果。

**Q4：网络层和 HTTP 层配置项分别属于哪种生效形式？哪些能真热生效？**

**A：**

先记三种常见生效形式：

1. 启动参数型：通常在对象构造或服务启动时读取，运行中改值不自动作用到已运行对象
2. 运行时动态读取型：每次业务路径都会读取，改值后后续调用立即生效
3. 新对象生效型：改值后仅影响后续新创建对象，已存在对象不变

网络层（`FiberFrameworkConfig`）对应关系：

1. 启动参数型
   - `scheduler.use_caller`
   - `iomanager.use_caller`
   - `fiber.shared_stack_size`（建议启动前确定）

2. 运行时动态读取型（真热生效）
   - `tcp.connect.timeout`
   - `fiber.pool.enabled`
   - `fiber.pool.max_size`
   - `fiber.pool.min_keep`
   - `fiber.pool.idle_timeout`

3. 新对象生效型
   - `fiber.stack_size`
   - `fiber.use_shared_stack`

HTTP 层（`HttpFrameworkConfig`）对应关系：

1. 运行时动态读取型（真热生效）
   - `http.max_header_size`
   - `http.max_body_size`
   - `http.error_response_format`

2. 启动参数型
   - `http.session_sweep_interval_ms`（当前在 `HttpServer` 启动时读取）

3. 新对象生效型
   - `http.sse_heartbeat_interval_ms`（供后续新启动的 SSE 发送逻辑读取）

一句话速记：

- **真热生效**：路径上“每次都读配置”的项
- **非真热生效**：只在“启动/构造时读一次”的项

#### 5. HttpServer 行为升级
- `HttpServer::handleClient()` 使用统一错误响应输出 400/413/500
- 增加异常捕获，避免业务处理异常直接炸掉服务线程
- `HttpServer::stop()` 接管 `SessionManager` 定时器关闭，避免后台清理任务阻止服务退出

---

### 3. 测试与验证
#### 当前验证结果
第三阶段新增/增强测试：

- `test_http_dispatch`
  - 参数路由命中
  - 精确路由优先级
  - pre/post interceptor 执行
- `test_http_framework_config`
  - 配置项读写
  - JSON / text 错误响应格式切换
- `test_http_server`
  - 参数路由集成验证
  - 拦截器集成验证
  - 404 / 413 JSON 错误响应验证

回归通过：

- `./bin/test_http_dispatch`
- `./bin/test_http_framework_config`
- `./bin/test_http_parser`
- `./bin/test_http_server`
- `./bin/test_session_manager`
- `./bin/test_sse_writer`
- `./bin/test_sse_server`

---

### 4. 当前实现与后续优化
#### 设计取舍

**当前实现：**
- 参数路由是最小版本
- Interceptor 是轻量前后置处理机制
- 错误响应已统一但仍较简洁
- 配置入口已集中，但仍是代码级静态配置

**后续可优化：**
- 参数路由支持正则 / 多段捕获 / 路由分组
- Interceptor 演进为完整中间件链
- 错误响应增加业务码、trace id、可观测字段
- 配置系统接入外部配置文件或热更新机制

---

### 5. 阶段总结
#### 一句话总结
第三阶段让这个项目从“能跑通 HTTP 请求”进一步升级成了“更适合承载真实业务”的通用 HTTP 框架：

- 路由表达能力更强
- 请求处理可统一拦截
- 错误输出更一致
- 关键行为可配置
- 测试体系更完整

#### 当前结论
到这里，通用 HTTP 框架的第三阶段主目标已经完成。

### 6. 下一阶段
#### 第四阶段会怎么做
第四阶段建议按两条路线二选一推进：

1. 继续深挖通用框架
   - 文件级自动热加载（补齐配置热加载 1/2 步）
   - 路由进一步增强（正则/多段捕获/分组）
   - Interceptor 演进为中间件链
   - 可观测性（trace id、统一日志字段、指标埋点）

2. 进入 AI 应用层
   - 基于当前框架实现 `/chat` 与 `/chat/stream`
   - 对话上下文管理、会话策略与流式输出约定

当前项目目标仍偏向“通用 HTTP 框架”，因此建议优先走路线 1。

---

## 第四阶段：模块独立化与工程化升级

### 0. 第四阶段改动类速览
#### 0.1 `session_storage.h / session_storage.cc`
- 新增 `SessionStorage` 抽象接口
- 新增默认实现 `MemorySessionStorage`

#### 0.2 `session_manager.h / session_manager.cc`
- `SessionManager` 改为依赖 `SessionStorage`
- 不再直接持有底层 `map<SID, Session>`

#### 0.3 `middleware.h / middleware.cc`
- 新增 `Middleware` 抽象
- 新增 `MiddlewareChain`
- 新增 `CallbackMiddleware` 便于快速桥接已有逻辑

#### 0.4 `servlet.h / servlet.cc`
- 路由升级为 method-aware
- 新增 `addMiddleware()`
- 分发流程升级为：middleware before -> interceptor pre -> servlet -> interceptor post -> middleware after

#### 0.5 测试改动
- 增强：`test_http_dispatch`
  - method-aware 路由
  - middleware before/after
- 增强：`test_session_manager`
  - `SessionStorage` 注入验证

### 1. 本阶段目标
#### 模块作用
第四阶段的目标，是把第三阶段已经具备的能力进一步模块独立化：

- Session 生命周期管理和存储后端解耦
- 拦截器能力抽象成中间件链
- 路由从 path-aware 升级到 method-aware

这一阶段依然停留在“通用 HTTP 框架层”，不进入 AI 业务协议。

---

### 2. 本阶段完成项
#### 设计要点

#### 1. SessionStorage 抽象
这一项是第四阶段最关键的“边界拆分”改动：

- 第三阶段以前，`SessionManager` 同时承担两类职责：
  1) **生命周期策略**（创建、touch、过期判断）
  2) **存储细节**（底层 `map<SID, Session>` 的读写）
- 第四阶段把第 2 类职责抽离为 `SessionStorage`，让 `SessionManager` 只保留策略层逻辑。

##### 接口设计
新增 `SessionStorage` 抽象接口（`session_storage.h`）：

- `save(Session::ptr session)`
  - 语义：保存或覆盖同 SID 的会话对象
  - 使用场景：`create()` 新建、`get()` 命中后 touch 回写

- `load(const std::string& session_id)`
  - 语义：按 SID 读取会话
  - 约定：未命中返回空指针

- `remove(const std::string& session_id)`
  - 语义：删除会话
  - 返回：是否删除成功

- `sweepExpired(uint64_t now_ms)`
  - 语义：按“当前时间”批量清理过期会话
  - 返回：本轮清理数量

这一组接口保持“最小闭环”原则：只暴露当前框架真实需要的最小能力，不提前引入复杂查询/索引接口。

##### 默认实现
新增 `MemorySessionStorage`（`session_storage.cc`）：

- 数据结构：`std::map<std::string, Session::ptr>`
- 并发保护：`Mutex` 锁住 `save/load/remove/sweepExpired`
- 过期清理：遍历 map，调用 `Session::isExpired(now_ms)` 删除过期项

这保证了：
- 单进程下功能完整可用
- 接口已稳定，可在不动上层调用链的前提下替换底层存储

##### SessionManager 接入方式
`SessionManager` 构造函数新增可注入参数：

- `SessionManager(max_inactive_ms, SessionStorage::ptr storage = MemorySessionStorage())`

调用链变化：

- `create()`：生成 SID -> 创建 Session -> `m_storage->save(session)`
- `get(id)`：`m_storage->load(id)` -> 过期则 `remove(id)` -> 未过期则 touch 后 `save(session)`
- `remove(id)`：委托 `m_storage->remove(id)`
- `sweepExpired()`：委托 `m_storage->sweepExpired(GetCurrentMS())`

可以看到：策略仍在 `SessionManager`，存储实现下沉到 `SessionStorage`。

##### 为后续 Redis/DB 预留的扩展点
如果下一步接 Redis/DB，主要工作是新增实现类（如 `RedisSessionStorage`），并在构造 `SessionManager` 时注入：

- 业务流程（`getOrCreate`、cookie 下发、touch 语义）可复用
- 主要新增点在存储侧：序列化格式、TTL/过期策略、连接失败处理、监控日志

当前取舍：
- 先只落地内存实现，确保接口语义先稳定
- 先完成“生命周期策略”和“存储后端”解耦，再逐步上外部存储

#### 2. MiddlewareChain 落地
这一项的目标，是把“前后置处理”从分散回调提升为可组合、可扩展的统一模块。

##### 抽象设计
新增 `Middleware` 基类（`middleware.h`）：

- `before(request, response, session) -> bool`
  - 在业务处理前执行
  - 返回 `false` 表示短路主流程（不再进入 servlet）

- `after(request, response, session) -> void`
  - 在主流程结束后执行
  - 用于收尾逻辑（日志、打点、补充响应头等）

相比第三阶段的 interceptor，这一层把“前后处理”合并到同一组件里，语义更聚合。

##### 链式执行模型
新增 `MiddlewareChain`：

- `addMiddleware()`：按注册顺序追加
- `processBefore()`：按顺序执行每个 middleware 的 `before`
  - 任一返回 `false` 即短路
- `processAfter()`：按顺序执行每个 middleware 的 `after`

当前实现是“线性顺序模型”，避免一开始就引入递归式 `next()` 调度带来的复杂性。

##### 快速桥接能力
新增 `CallbackMiddleware`：

- 用 `std::function` 快速注入 `before/after`
- 适合测试、PoC 和渐进改造
- 不需要先写完整继承类，就可以快速验证中间件语义

##### 与第三阶段 interceptor 的关系
第四阶段没有直接移除 interceptor，而是做“兼容叠加”：

- middleware 放在分发主链路最外层
- interceptor 保持原有行为
- 这样可以在不破坏旧逻辑的前提下，引入新的统一扩展入口

当前取舍：
- 先做顺序执行模型，不引入复杂递归 `next` 调度
- 先保留 interceptor，降低升级风险和迁移成本
- 先验证“可组合前后置能力”是否稳定，再推进统一入口

#### 3. method-aware 路由升级
这一项的核心，是把路由匹配从“只看路径”升级为“方法 + 路径联合匹配”。

##### 为什么要升级
第三阶段如果同时存在：

- `GET /items`
- `POST /items`

仅靠 path 无法准确区分读写语义。method-aware 升级后，路由语义和 REST 风格保持一致。

##### 数据结构升级
`ServletDispatch` 内部路由项新增 `HttpMethod` 字段：

- `ExactItem`：`method + uri + servlet`
- `ParamItem`：`method + pattern + segments + servlet`
- `GlobItem`：`method + pattern + servlet`

并新增方法匹配函数 `MatchMethod(route_method, request_method)`：

- `route_method == INVALID_METHOD` 视为“不限制方法”（兼容旧接口）
- 否则必须与请求方法严格一致

##### API 兼容策略
新增 method-aware 注册接口：

- `addServlet(HttpMethod, path, ...)`
- `addParamServlet(HttpMethod, path, ...)`
- `addGlobServlet(HttpMethod, path, ...)`

旧接口继续保留：

- 旧接口内部统一注册为 `INVALID_METHOD`
- 这让旧代码无需改动即可继续运行

##### 匹配优先级保持不变
即使升级到 method-aware，匹配顺序仍保持第三阶段语义：

1. 精确匹配
2. 参数路由
3. 通配路由
4. 默认路由

变化点只在每一层内部多了一次方法过滤，不改变原有优先级认知。

当前取舍：
- 先让 method-aware 与 path-only 兼容共存，避免破坏已有路由注册
- 先不引入正则路由、多段捕获、路由分组等高复杂特性
- 先把“方法维度”这条主线稳定下来，再做语法增强

#### 4. 分发主链路升级
这一项是第四阶段“能力编排”层面的升级：不是新增单点能力，而是重排主执行链路。

##### 新执行顺序
`ServletDispatch::handle(...)` 当前顺序：

1. `MiddlewareChain::processBefore(...)`
2. pre interceptor
3. 路由匹配 + `servlet->handle(...)`
4. post interceptor
5. `MiddlewareChain::processAfter(...)`

这条顺序的意图是：

- middleware 作为最外层横切入口
- interceptor 继续承接第三阶段既有逻辑
- servlet 仍是核心业务处理点

##### 短路与收尾语义
主链路重点保证“即使短路也尽量收尾”：

- 若 middleware before 返回 `false`，会直接跳过业务处理，但仍执行 middleware after
- 若 pre interceptor 返回 `false`，会跳过业务 handler，但仍执行 post interceptor 和 middleware after

这样做的好处：

- 响应头补充、访问日志、埋点统计等后置逻辑更稳定
- 不容易因中途拦截导致收尾逻辑漏执行

##### 兼容与演进策略
第四阶段的策略不是“推翻重来”，而是“外围加壳、内部兼容”：

- 先把 middleware 放到最外层
- 保留 interceptor 语义不变
- 让已有代码几乎零迁移成本

当前取舍：
- 保持第三阶段 interceptor 行为不变，优先保证兼容稳定
- 通过 middleware 外围增强，为后续统一前后置入口做过渡
- 先验证链路编排正确性，再考虑逐步淡化 interceptor

---

### 3. 测试与验证
#### 当前验证结果
第四阶段新增/增强验证：

- `test_http_dispatch`
  - 参数路由优先级
  - method-aware 路由区分 GET / POST
  - middleware before/after
  - interceptor 与 middleware 共存

- `test_session_manager`
  - `SessionStorage` 注入验证

回归通过：

- `./bin/test_http_dispatch`
- `./bin/test_http_framework_config`
- `./bin/test_http_parser`
- `./bin/test_http_server`
- `./bin/test_session_manager`
- `./bin/test_sse_writer`
- `./bin/test_sse_server`
- `./bin/test_hook`
- `./bin/test_fiber_pool`

---

### 4. 当前实现与后续优化
#### 设计取舍

**当前实现：**
- SessionStorage 已抽象，但仅提供内存实现
- MiddlewareChain 已落地，但仍是轻量顺序执行模型
- method-aware 路由已完成，但仍保持最小匹配语义
- 旧 Interceptor 机制仍保留，便于平滑迁移

**后续可优化：**
- 增加 Redis / DB SessionStorage 实现
- 让 middleware 成为唯一统一前后置处理入口，逐步淡化 interceptor
- method-aware 路由进一步增强到正则、多段捕获、路由分组
- 继续引入 `HttpContext` 或 SSL 以贴近更完整框架形态

---

### 5. 阶段总结
#### 一句话总结
第四阶段的核心不是再补单点能力，而是把已有能力拆成更稳定的模块边界：

- Session 有了存储抽象
- 拦截器开始演进到中间件链
- 路由从“只看 path”升级到“method + path”

#### 当前结论
到这里，通用 HTTP 框架已经从“教学型最小闭环”进一步走向“工程型小框架”。

### 6. 下一阶段
#### 第五阶段会怎么做
第五阶段建议继续深挖框架层，优先级建议如下：

1. `HttpContext`（请求解析状态机独立化）
2. 正则/多段捕获/路由分组
3. MiddlewareChain 继续演进
4. SessionStorage 增加外部存储实现
5. SSL/HTTPS 支持

如果届时你决定转入 AI 层，那么也已经具备了足够稳定的 HTTP 基础设施。

---

## 第五阶段：HttpContext 独立化与解析上下文收拢

### 0. 第五阶段改动类速览
#### 0.1 `http_context.h / http_context.cc`
- 新增 `HttpContext`
- 收拢连接级 `buffer + parser`
- 对外提供 `recvRequest()` 与解析错误查询接口

#### 0.2 `http_session.h / http_session.cc`
- `HttpSession` 不再直接持有 `HttpRequestParser` 与 `m_buffer`
- 改为持有 `HttpContext`
- `recvRequest()` 与解析错误查询统一委托 `HttpContext`

#### 0.3 `CMakeLists.txt`
- 新增 `http_context.cc` 编译项
- 新增 `test_http_context`

#### 0.4 测试改动
- 新增：`test_http_context`
  - 半包解析
  - 粘包/多请求连续解析
  - 非法请求错误传播
  - 413 错误传播

### 1. 本阶段目标
#### 模块作用
第五阶段的目标，是把当前 HTTP 请求解析链路中的“连接级上下文状态”独立出来。

第四阶段结束时，解析职责分散在三处：

- `HttpRequestParser`：负责纯解析逻辑
- `HttpSession`：负责持有 `m_parser + m_buffer` 并循环读 socket
- `HttpServer`：负责把解析错误映射成 400 / 413

这种实现已经可用，但“连接级解析状态”还没有自己的模块边界。

所以第五阶段引入 `HttpContext`，让职责分工变为：

- `HttpRequestParser`：buffer -> request 的纯解析器
- `HttpContext`：连接级 parser/buffer 状态管理
- `HttpSession`：HTTP 连接封装
- `HttpServer`：请求处理主循环

这一阶段依然聚焦通用 HTTP 框架，不进入 AI 层。

---

### 2. 本阶段完成项
#### 设计要点

#### 1. HttpContext 新增
这一项是第五阶段最关键的结构改造：把“连接级解析状态”从 `HttpSession` 中拆出来，形成独立 `HttpContext` 模块。

##### 为什么要新增 HttpContext
在第四阶段以前，`HttpSession` 既要做连接封装，又要维护：

- `m_buffer`（跨多次 `read()` 的连接级缓冲区）
- `m_parser`（请求解析器与其错误状态）
- `recvRequest()` 的读循环 + 解析循环

这会带来两个问题：

1. 类职责过重：`HttpSession` 同时处理网络收发与解析上下文管理
2. 扩展成本高：后续若做增量状态机、chunked、streaming body，改动会扩散到 session/server

所以第五阶段先做边界独立化，把解析上下文单独抽成 `HttpContext`。

##### HttpContext 的职责边界
`HttpContext` 现在只做一件事：管理“一个连接上的请求解析上下文”。

具体包括：

- 持有连接级缓冲区 `m_buffer`
- 持有请求解析器 `m_parser`
- 从 `SocketStream` 持续读数据，直到解析出一条完整请求或失败
- 保留未消费字节（粘包/keep-alive 连续请求场景）
- 统一暴露解析错误信息给上层查询

这意味着：

- `HttpRequestParser` 继续是“纯解析器”
- `HttpContext` 成为“解析状态容器 + 解析驱动器”

##### 核心执行链路（recvRequest）
`HttpContext::recvRequest(SocketStream& stream)` 的流程如下：

1. 先拿当前 `m_buffer` 调 `m_parser.parse(buffer, consumed)`
2. 若解析成功：
   - `erase(0, consumed)` 删除已消费字节
   - 返回本次 `HttpRequest`
3. 若解析失败且 parser 进入 error：返回空指针
4. 若数据不完整：继续 `stream.read(...)`，把新字节 `append` 到 `m_buffer`
5. 回到步骤 1，直到成功或失败退出

这条逻辑与旧 `HttpSession::recvRequest()` 行为保持一致，只是职责迁移到了独立模块。

##### 对外接口与语义
当前 `HttpContext` 对外接口：

- `recvRequest(SocketStream& stream)`：解析一条完整请求
- `hasError()`：当前 parser 是否报错
- `getError()`：最近一次错误描述
- `isRequestTooLarge()`：是否属于请求过大（用于 413）
- `reset()`：清空 parser 错误状态并清空上下文缓冲区

其中 `reset()` 目前主要用于显式复位/测试预留，正常请求主链路并不依赖频繁手动调用。

##### 与 keep-alive 的关系（容易误解）
`HttpContext` 里没有显式写“是否继续 keep-alive”的分支，这是职责划分决定的：

- `HttpContext` 只负责“能不能解析出下一条请求”
- `HttpServer` 负责“拿到请求后是否继续下一轮处理”

keep-alive 能成立的关键并不是一个 if，而是：

- `m_buffer` 会保留剩余未消费字节
- 同一连接会反复调用 `recvRequest()`

##### 当前取舍
- 先采用“薄封装”方案，不重写 `HttpRequestParser` 内部算法
- 先确保外部行为不变（400/413、keep-alive、SSE 均不回归）
- 先把边界立住，再在后续阶段把 `HttpContext` 演进成更完整增量状态机

#### 2. HttpSession 职责收敛
第五阶段对 `HttpSession` 的调整，不是增强功能，而是做职责瘦身。

##### 调整前
`HttpSession` 同时管理：

- 底层 `SocketStream`
- `HttpRequestParser`
- 连接级缓冲区 `m_buffer`
- `recvRequest()` 里的读循环与解析循环

##### 调整后
`HttpSession` 只保留：

- HTTP 连接封装
- `sendResponse()`
- 对 `HttpContext` 的轻量委托接口

现在 `recvRequest()` 直接转调 `m_context.recvRequest(*this)`。

##### 当前取舍
- 先不改变 `HttpSession` 对外 API，避免影响 `HttpServer`
- 先通过内部委托完成架构重构，降低回归风险

#### 3. 解析错误语义保持兼容
虽然引入了 `HttpContext`，但上层 `HttpServer` 的使用方式基本不变：

- 仍通过 `session->recvRequest()` 获取请求
- 仍通过 `session->hasParserError()` / `getParserError()` / `isRequestTooLarge()` 判断错误
- 仍把非法请求映射为 400，请求过大映射为 413

这说明第五阶段是“边界独立化”，不是“上层语义推翻重做”。

##### 当前取舍
- 保持 `HttpServer` 主链路稳定
- 避免把 `HttpContext` 重构与 server 行为调整混在一个阶段

---

### 3. 测试与验证
#### 当前验证结果
第五阶段新增 `test_http_context`，覆盖：

- 半包请求：拆成两段读取后仍能成功解析
- 粘包/多请求：一次读入两条请求，第一条解析后第二条仍能继续解析
- 非法请求：错误方法能正确透传解析错误
- 请求过大：header/body 超限时能透传 413 类型错误

回归通过：

- `./bin/test_http_context`
- `./bin/test_http_parser`
- `./bin/test_http_server`
- `./bin/test_http_dispatch`
- `./bin/test_session_manager`
- `./bin/test_sse_writer`
- `./bin/test_sse_server`

---

### 4. 当前实现与后续优化
#### 设计取舍

**当前实现：**
- `HttpContext` 已独立，但内部仍直接复用 `HttpRequestParser`
- `HttpSession` 已从“自己维护 parser/buffer”收敛成“连接封装 + 委托入口”
- `HttpServer` 主流程保持稳定，外部行为未发生破坏性变化

**后续可优化：**
- 把 `HttpContext` 继续演进为更完整的增量状态机
- 为 chunked / streaming body 预留更细粒度状态
- 让解析上下文承接更多请求级中间状态，而不是只保存 buffer

---

### 5. 阶段总结
#### 一句话总结
第五阶段的核心不是“解析能力变强了多少”，而是把“请求解析状态”从 `HttpSession` 里拆出来，形成独立模块边界。

#### 当前结论
到这里，HTTP 框架在结构上又向参考仓库靠近了一步：

- 有了独立的 `HttpContext`
- `HttpSession` 更轻
- 后续继续做解析状态机或 HTTPS/SSL 时，改动会更集中

### 6. 下一阶段
#### 第六阶段会怎么做
第六阶段建议继续在“工程化能力”上推进，优先级建议如下：

1. 路由继续增强（分组 / 更强匹配）
2. MiddlewareChain 继续演进，逐步统一前后置入口
3. 模块目录边界继续整理
4. 为 SSL/HTTPS 支持做结构准备

---

## 第六阶段：Router 独立化与 Middleware 执行模型收口

### 0. 第六阶段改动类速览
#### 0.1 `router.h / router.cc`
- 新增 `Router`
- 从 `ServletDispatch` 中拆出 exact / param / glob 路由注册与匹配逻辑
- 新增 `RouteMatch` 结构化匹配结果

#### 0.2 `servlet.h / servlet.cc`
- `ServletDispatch` 不再直接持有三套路由表
- 改为持有 `Router`
- 分发逻辑聚焦为：middleware -> interceptor -> route match -> servlet handle

#### 0.3 `middleware.h / middleware.cc`
- 新增 `ExecutionState`
- `before()` 进入过的中间件才会执行 `after()`
- `after()` 改为逆序执行，更符合“入栈/出栈”语义

#### 0.4 测试改动
- 新增 `test_http_router`
- 扩展 `test_http_dispatch`
  - middleware 逆序收尾
  - 短路时只收尾已进入中间件
  - handler 抛异常时 post/after 仍执行

### 1. 本阶段目标
#### 模块作用
第六阶段的核心，不是继续加新 HTTP 功能点，而是把“路由层”和“执行链层”继续工程化拆开。

第五阶段结束后，`ServletDispatch` 仍然承担过多职责：

- 路由注册
- 路由匹配
- 默认 404
- middleware 编排
- interceptor 编排
- servlet 调用

这意味着：

- 路由能力和执行链耦合在一起
- middleware 虽已落地，但执行语义还不够像真正的进入/退出栈
- 后续若继续做组路由、更强路由或统一前后置入口，改动会继续堆在 `ServletDispatch`

所以第六阶段的目标是：

- 拆出独立 `Router`
- 让 `ServletDispatch` 收敛为“编排层”
- 把 middleware 的执行语义修正为更稳定的工程模型

---

### 2. 本阶段完成项
#### 设计要点

#### 1. Router 新增
这一项是第六阶段的主线：把路由注册与匹配能力从 `ServletDispatch` 中抽到独立 `Router`。

##### 解决的问题
在抽离之前，`ServletDispatch` 内部直接维护：

- `m_exact`
- `m_params`
- `m_globs`

所有注册与匹配细节都写在 `servlet.cc` 中，导致类边界偏重。

##### Router 的职责
`Router` 现在统一负责：

- exact 路由注册与匹配
- param 路由注册与匹配
- glob 路由注册与匹配
- method-aware 匹配
- 默认路由保存与返回

##### RouteMatch 的意义
第六阶段没有只返回一个 `Servlet::ptr`，而是引入了 `RouteMatch`：

- `servlet`
- `route_params`
- `type`（exact / param / glob / default）

这样做的好处是：

- 路由匹配结果不再只是“命中了谁”
- 还带有“命中时附带的结构化信息”
- 为后续组路由、组级 middleware、命中类型统计预留了空间

##### 当前取舍
- 先保持优先级不变：`exact > param > glob > default`
- 先不做正则路由 / 路由分组
- 先把 Router 边界独立好，再扩展更强路由语法

#### 2. ServletDispatch 收敛为编排层
第六阶段后，`ServletDispatch` 的定位更明确了：

- 它仍然是对外暴露的分发入口
- 但它自己不再直接管理三套路由表
- 注册行为统一委托给 `Router`
- 匹配行为统一委托给 `Router`

##### 当前主链路
`ServletDispatch::handle(...)` 现在更像一个调度器，顺序为：

1. middleware before
2. pre interceptor
3. `Router::match()`
4. `servlet->handle(...)`
5. post interceptor
6. middleware after

##### 兼容性策略
- 旧的 `addServlet/addParamServlet/addGlobServlet` API 继续保留
- `getMatched(const std::string&)` 继续保留兼容
- 外部使用 `ServletDispatch` 的方式几乎不需要改

这保证了：

- 内部边界变了
- 外部注册习惯不变

#### 3. Middleware 执行模型修正
这一项是第六阶段里很重要的语义修复。

##### 旧问题
第四阶段的 `MiddlewareChain::processAfter()` 会直接对“所有已注册 middleware”执行 `after()`。

这会带来两个问题：

- 某个 middleware 的 `before()` 还没执行到，就可能在短路后被执行 `after()`
- `after()` 是正序执行，不符合更自然的“进入/退出栈”模型

##### 第六阶段改法
新增 `ExecutionState`，在 `processBefore()` 中记录“哪些 middleware 成功进入过”。

然后 `processAfter()`：

- 只对 `state.entered` 中的 middleware 执行 `after()`
- 按逆序执行

这样语义就更稳定：

- 成功进入的才负责退出
- 后进先出更符合嵌套收尾逻辑

##### 当前取舍
- 先不做 `next()` 洋葱模型
- 先把现有 before/after 语义修正确保稳定

#### 4. 异常路径的收尾补齐
第六阶段还补了一个容易忽略但很重要的点：

- 当 `servlet->handle(...)` 抛异常时
- `ServletDispatch` 内部会先执行 post interceptor 与 middleware after
- 然后再把异常继续抛给 `HttpServer`

这样 `HttpServer` 依然负责统一把异常转成 500，
而 `ServletDispatch` 负责保证“分发层的收尾逻辑不因异常丢失”。

这让执行链更接近工程里常见的 finally 语义。

---

### 3. 测试与验证
#### 当前验证结果
第六阶段新增与增强测试覆盖：

- `test_http_router`
  - 验证 exact/param/glob 优先级
  - 验证 method-aware 路由区分

- `test_http_dispatch`
  - 验证 middleware after 逆序执行
  - 验证短路后只收尾已进入中间件
  - 验证 handler 抛异常时 post/after 仍执行
  - 验证 interceptor 兼容路径未回归

回归通过：

- `./bin/test_http_router`
- `./bin/test_http_dispatch`
- `./bin/test_http_server`
- `./bin/test_http_context`
- `./bin/test_http_parser`
- `./bin/test_session_manager`
- `./bin/test_sse_writer`
- `./bin/test_sse_server`

---

### 4. 当前实现与后续优化
#### 设计取舍

**当前实现：**
- `Router` 已独立，但仍由 `ServletDispatch` 对外承载 API
- interceptor 仍保留，作为兼容层继续存在
- middleware 执行模型已更合理，但仍是轻量 before/after 模型

**后续可优化：**
- 继续做 route group / group middleware
- 进一步淡化 interceptor，让 middleware 成为唯一主入口
- 若需要更强表达能力，再引入正则或分组 DSL

---

### 5. 阶段总结
#### 一句话总结
第六阶段的核心，是把 `ServletDispatch` 从“路由与执行链全都自己管”收敛为“编排层”，同时让 middleware 的执行语义更符合真正的进入/退出模型。

#### 当前结论
到这里，HTTP 框架已经进一步向参考仓库的 HTTP 层结构靠拢：

- 有了独立 `Router`
- `ServletDispatch` 更聚焦编排职责
- middleware 行为更稳定、可预期

### 6. 下一阶段
#### 第七阶段会怎么做
第七阶段建议完成 HTTP 框架层的最后一轮工程化收尾，优先级建议如下：

1. SSL/HTTPS 支持
2. 模块目录边界继续整理
3. route group / group middleware 视需要补充
4. 对照参考仓库做 HTTP 层级别的最终查缺补漏

到第七阶段完成时，目标就是达到“HTTP 框架层基本对齐参考仓库”：

- 模块边界对齐
- 核心能力对齐
- 工程结构接近
- 但不追求逐文件逐接口复制

---

## 第七阶段：HTTPS/SSL 接入与 HTTP 模块工程化收尾

### 0. 第七阶段改动类速览
#### 0.1 `ssl/` 模块新增
- 新增 `ssl_types.h`
- 新增 `ssl_config.h / ssl_config.cc`
- 新增 `ssl_context.h / ssl_context.cc`
- 新增 `ssl_socket.h / ssl_socket.cc`
- 新增 `ssl_socket_stream.h / ssl_socket_stream.cc`

##### `SslContext` 类说明（`src/http/ssl/ssl_context.h/.cc`）
`SslContext` 的定位是“TLS 运行环境上下文”，负责管理 OpenSSL 的 `SSL_CTX` 生命周期。

它的职责边界：
- 负责基于 `SslConfig` 初始化 `SSL_CTX`；
- 负责在服务端模式下加载证书/私钥并校验匹配；
- 负责加载可选 CA 与设置证书校验策略；
- 不负责单连接握手与数据收发（那是 `SslSocket` 的职责）。

核心流程（`initialize()`）：
1. 执行 OpenSSL 全局初始化（只做一次）；
2. 根据模式选择 `TLS_server_method()` 或 `TLS_client_method()`；
3. 调 `SSL_CTX_new` 创建上下文；
4. 服务端模式下加载证书、私钥并执行 `SSL_CTX_check_private_key`；
5. 如配置了 CA 文件则加载；
6. 按 `verifyPeer` 设置 `SSL_VERIFY_PEER/SSL_VERIFY_NONE`。

学习时要抓住的点：
- `SslContext` 是“连接之前”的上下文，不是“连接本身”；
- `SSL_CTX` 可被多个连接复用；
- 真正每个连接的 TLS 会话对象是 `SSL*`，由 `SslSocket` 基于 `SSL_CTX` 创建。

###### 学习问答记录（`SSL_CTX* m_ctx`）
**Q：`SslContext` 里的 `SSL_CTX* m_ctx` 是什么？**

**A：**
`m_ctx` 是 OpenSSL 的“上下文句柄”，可以理解成 TLS 连接工厂的核心资源。

它保存的是“可复用配置与状态”，例如：
- 证书与私钥配置；
- 算法与协议参数；
- 证书校验策略。

在当前实现中：
1. `initialize()` 里通过 `SSL_CTX_new(...)` 创建 `m_ctx`；
2. 析构函数里通过 `SSL_CTX_free(...)` 释放 `m_ctx`。

**Q：什么叫“连接级别之上”？**

**A：**
意思是它不属于某一条具体连接，而是可以被多条连接共享复用：

- `SSL_CTX*`：上下文级（共享，通常一个server实例一份）；
- `SSL*`：连接级（每条连接一个，会话对象）。

也就是说，每个连接的 `SSL*` 都是基于 `SSL_CTX*` 创建出来的。

##### `SslSocket` 类说明（`src/http/ssl/ssl_socket.h/.cc`）
`SslSocket` 的定位是“TLS 连接执行层”：它继承 `Socket`，但把原本明文收发替换为基于 `SSL*` 的加密收发。

它和网络层 `Socket` 的关系：
- `Socket`：只负责 TCP 连接和明文 `send/recv`；
- `SslSocket`：在 TCP 之上增加 TLS 会话，负责 `initializeSsl + handshake + SSL_read/SSL_write`。

这意味着上层调用接口仍然是 socket 风格，但底层传输语义已从“明文”升级为“加密”。

###### 学习问答记录（`SslSocket`）
**Q：`typedef struct ssl_st SSL;` 只是别名吗？**

**A：**
是的，本质上就是给 OpenSSL 的 `struct ssl_st` 起了一个可用名 `SSL`。

但它同时有一个工程意义：
- 这是“前置声明 + 类型别名”写法；
- 能在头文件里直接使用 `SSL*`，而不需要立刻包含 `<openssl/ssl.h>`；
- 从而减少头文件耦合与编译开销。

**Q：`SslSocket` 相比 `Socket` 重写了哪些函数？新增了哪些？**

**A：**
`SslSocket` 对 `Socket` 的核心重写是“连接与收发路径”四件事：

1. 重写 `connect(...)`：TCP 建连后补 `initializeSsl()` + `handshake()`；
2. 重写 `close()`：先关闭 TLS 会话（`SSL_shutdown/SSL_free`）再关底层 fd；
3. 重写 `send(...) / recv(...)`（含 iovec 版本）：改为走 `SSL_write/SSL_read`，这两个函数就是 TLS 版的 send/recv；
4. 保持接口形态兼容，便于复用现有 `SocketStream/HttpSession` 链路。

此外新增了 TLS 专属能力：
- `initializeSsl()`：创建 `SSL*` 并绑定套接字文件描述符 fd；
- `handshake()`：按模式执行 `SSL_accept/SSL_connect`；
- `isHandshakeDone()`：握手状态查询；
- `CreateTCPSocket(...)` / `FromSocket(...)`：创建或包装 TLS 连接对象。

学习时要抓住的点：
- `SslContext` 负责“共享上下文（SSL_CTX）”；
- `SslSocket` 负责“单连接会话（SSL*）”；
- 二者共同完成 HTTPS 的传输层接入。

#### 0.2 `http_server.h / http_server.cc`
- `HttpServer` 新增 `setSslConfig()`
- `HttpServer` 可基于 `SslContext` 把普通 TCP 客户端包装为 `SslSocket`
- 维持原有 HTTP 主循环、分发、400/413、keep-alive 语义

#### 0.3 `middleware/cors/` 模块新增
- 新增 `cors_config.h / cors_config.cc`
- 新增 `cors_middleware.h / cors_middleware.cc`
- 提供框架级内建 CORS middleware 示例实现

##### 类/模块作用
第七阶段新增 `middleware/cors` 的目标，不是引入新的分发机制，而是在现有 `MiddlewareChain` 上落地一个可直接使用的“框架级能力”。

这个模块负责统一处理跨域响应策略，避免每个业务路由重复设置：
- `Access-Control-Allow-Origin`
- `Access-Control-Allow-Methods`
- `Access-Control-Allow-Headers`
- `Access-Control-Allow-Credentials`
- `Access-Control-Max-Age`
- `Access-Control-Expose-Headers`

##### 新增内容拆分
1. `CorsConfig`（策略对象）
   - 负责描述跨域策略参数，不参与请求分发；
   - 关键字段：`allowedOrigins`、`allowedMethods`、`allowedHeaders`、`exposeHeaders`、`maxAge`、`allowCredentials`；
   - 提供 `isOriginAllowed(origin)` 作为来源判断入口。

2. `CorsMiddleware`（执行对象）
   - 基于 `Middleware` 抽象实现 CORS 规则落地；
   - `before()` 负责预检短路与基础头写入；
   - `after()` 负责兜底补头与 `Expose-Headers` 输出。

##### 执行语义（重点）
- 普通请求：
  - `before()` 写通用头后返回 `true`；
  - 继续进入路由/handler；
  - `after()` 再兜底补齐头部。

- `OPTIONS` 预检请求：
  - `before()` 直接构造 `204 No Content`；
  - 写入 `Allow-Methods/Allow-Headers/Max-Age`；
  - 返回 `false`，短路业务 handler。

##### 与 Middleware 抽象的关系
`CorsMiddleware` 和 `Middleware` 不是并列关系，而是“抽象层与实现层”的关系：
- `Middleware` 解决“怎么挂扩展点”；
- `CorsMiddleware` 解决“跨域策略怎么落地”。

这标志着框架从“有机制”进入“有内建成品能力”的阶段。

##### 当前取舍
- 先实现最常用 CORS 主链路；
- 先不引入动态 Origin 白名单源加载；
- 先不做路径级/租户级细粒度策略分层。

##### 对应测试关注点
- `test_cors_middleware` 验证普通跨域请求头是否正确；
- 验证 `OPTIONS` 预检是否返回 204 并正确短路；
- 验证模块与现有 `ServletDispatch + MiddlewareChain` 链路兼容。

#### 0.4 测试改动
- 新增 `test_http_ssl_server`
- 新增 `test_cors_middleware`
- 回归现有 HTTP / router / session / SSE 测试

### 1. 本阶段目标
#### 模块作用
第七阶段的目标，不再是继续扩很多新功能点，而是完成“HTTP 框架层基本对齐参考仓库”的最后一轮工程化收口。

第六阶段结束后，核心 HTTP 模块边界已经基本形成：

- `http_context`
- `router`
- `middleware`
- `session`
- `http_server`

但与参考仓库相比，还差三块关键拼图：

1. 独立 `ssl/` 模块与 HTTPS 能力
2. 至少一个框架级内建 middleware 成品模块
3. 更接近参考仓库的目录级工程边界

所以第七阶段的重点是：

- 补齐 HTTPS/SSL
- 补齐内建 CORS middleware
- 完成 HTTP 层最后的结构收尾

---

### 2. 本阶段完成项
#### 设计要点

#### 1. `ssl/` 模块落地
这一项是第七阶段的主线：给 HTTP 层补齐独立 SSL 边界。

##### 新增模块职责
- `SslTypes`：定义 SSL 侧基础枚举
- `SslConfig`：承载证书、私钥、CA、校验开关等配置
- `SslContext`：管理 OpenSSL 的 `SSL_CTX`
- `SslSocket`：把已有 `Socket` 扩展为可进行 TLS 握手与加解密收发的连接对象
- `SslSocketStream`：把 `SslSocket` 包装成 Stream/SocketStream 风格接口，方便沿用现有收发模型

##### 设计取舍
- 继续复用现有 `Socket` / `SocketStream` / `HttpSession` 主链路
- 不单独重写一套 HTTPS 版 `HttpSession`
- 通过 `SslSocket` 在 socket 层完成“加密连接替换”，上层 HTTP 解析逻辑保持不变

##### 当前效果
- HTTPS 连接在进入 `HttpSession` 之前完成 TLS 握手
- `HttpContext`、`HttpParser`、`ServletDispatch`、`SessionManager` 不需要因为 HTTPS 单独改写逻辑

##### 学习问答记录（SSL/TLS）
**Q：什么是 SSL？**

**A：**
SSL 是历史叫法，现在标准名称是 TLS。它不是业务协议，而是“传输安全层”，位置在：

```text
HTTP
  ↓
TLS(SSL)
  ↓
TCP
```

TLS 的核心能力有三件事：

1. **机密性（Confidentiality）**：传输数据被加密，抓包也看不到明文；
2. **完整性（Integrity）**：可检测篡改，防止中间人改包；
3. **身份认证（Authentication）**：通过证书证明服务端身份，避免连到伪造站点。

---

**Q：为什么 HTTP 框架必须支持 SSL/TLS？**

**A：**
因为明文 HTTP 在真实网络环境下风险很高，核心问题是：

1. **可窃听**：Cookie、Token、业务数据会直接暴露；
2. **可篡改**：中间链路可插入/修改响应内容；
3. **难信任对端**：客户端无法确认“对面真的是目标服务器”。

对框架层来说，TLS 不是“锦上添花”，而是生产可用性的基础能力。

---

**Q：TLS 握手和 TCP 握手是什么关系？**

**A：**
两者是不同层次、不同目标的握手：

1. **TCP 三次握手**：先建立“可通信的传输连接”；
2. **TLS 握手**：在 TCP 建连后进行，协商加密套件、校验证书、生成会话密钥；
3. **应用数据传输**：握手完成后，HTTP 请求/响应才会进入加密通道。

也就是说：TCP 解决“能不能连上”，TLS 解决“连上后是否安全可信”。

---

**Q：SSL/TLS 的原理是什么？**

**A：**
TLS 的核心原理可以概括为：

```text
用非对称加密做身份认证与密钥协商，
再用对称加密做高效数据传输，
并用消息认证/AEAD保证完整性。
```

可拆成三个层次理解：

1. **身份认证层**
   - 服务端通过证书证明身份；
   - 客户端校验证书链、有效期、域名匹配，防止连接到伪造服务。

2. **密钥协商层**
   - 通过握手过程协商会话密钥（现代常见 ECDHE）；
   - 目标是双方在不暴露密钥明文的前提下，得到同一份会话密钥。

3. **数据保护层**
   - 握手完成后，应用层数据（HTTP）使用会话密钥做对称加密；
   - 同时附带完整性校验，确保报文未被篡改。

为什么不是全程用非对称加密：

- 非对称加密计算开销大，适合做握手阶段的认证与协商；
- 对称加密性能高，适合承载后续高频业务数据传输。

所以 TLS 采用“握手期偏非对称、传输期偏对称”的组合模型。

---

**Q：TLS 握手发生在什么时候？是每个请求都握手吗？**

**A：**
TLS 握手是“连接级”而不是“请求级”动作：

1. 每个**新建 TCP 连接**在发送第一条 HTTPS 请求前，通常会进行一次 TLS 握手；
2. 在 keep-alive 场景下，同一连接上的多个请求会复用该安全会话，不会每个请求都重新完整握手；
3. 连接断开后重连，通常会再次握手（可通过会话恢复降低开销）。

当前第七阶段实现也遵循这个语义：
- `HttpServer` 在连接进入 `HttpSession` 之前先完成 TLS 握手；
- 握手成功后，上层仍按原 HTTP 请求循环处理。

#### 2. `HttpServer` 接入 HTTPS
第七阶段让 `HttpServer` 从“只能处理明文 HTTP”扩展为“也能处理 HTTPS”。

##### 新增能力
- `setSslConfig()`：加载并初始化 `SslContext`
- `isSSLEnabled()`：查询当前是否启用 HTTPS
- `addMiddleware()`：补一个更像框架门面的 middleware 注册入口

##### 请求链路变化
现在 `handleClient()` 会先判断：

- 若未启用 SSL：沿用原普通 `Socket -> HttpSession` 路径
- 若启用 SSL：先把 `Socket` 包装为 `SslSocket` 并完成握手，再交给 `HttpSession`

##### 当前取舍
- 保持 `HttpServer` 仍然是统一编排入口
- 保持普通 HTTP 行为完全兼容
- 先实现服务端 HTTPS 主链路，不在本阶段扩展复杂双向证书认证场景

#### 3. 内建 `CorsMiddleware` 新增
这一项是第七阶段“框架成品感”增强的关键。

##### 为什么选 CORS
- 参考仓库已有 `CorsMiddleware`
- 它是非常典型的 HTTP 框架级 middleware
- 适合验证当前 `MiddlewareChain` 是否已经能承载真正的框架功能，而不只是抽象接口

##### 当前能力
- `CorsConfig`：配置允许源、允许方法、允许头、暴露头、最大缓存时间、是否允许凭证
- `CorsMiddleware`：
  - 普通跨域请求时补齐 CORS 响应头
  - `OPTIONS` 预检请求时直接构造 204 响应并短路主业务链路

##### 当前取舍
- 先做最常用的 CORS 核心能力
- 先不做更复杂的动态 origin 白名单源加载

#### 4. 工程结构继续向参考仓库靠拢
这一阶段没有强制一次性移动所有历史文件，但已经开始按目标目录组织新增模块：

- `http/ssl/`
- `http/middleware/cors/`

这意味着当前工程结构已经不再完全平铺，开始沿着“HTTP 模块 -> 子域目录”的方向收口。

---

### 3. 测试与验证
#### 当前验证结果
第七阶段新增测试：

- `test_http_ssl_server`
  - 启动 HTTPS 服务
  - 动态生成测试证书
  - 用 `SslSocket` 作为客户端完成 TLS 握手并发起 HTTP 请求
  - 验证 HTTPS 响应成功返回

- `test_cors_middleware`
  - 验证普通跨域请求的 CORS 响应头
  - 验证 `OPTIONS` 预检请求短路返回 204

回归通过：

- `./bin/test_cors_middleware`
- `./bin/test_http_ssl_server`
- `./bin/test_http_server`
- `./bin/test_http_dispatch`
- `./bin/test_http_context`
- `./bin/test_http_router`
- `./bin/test_http_parser`
- `./bin/test_session_manager`
- `./bin/test_sse_writer`
- `./bin/test_sse_server`

---

### 4. 当前实现与后续优化
#### 设计取舍

**当前实现：**
- 已具备独立 `ssl/` 模块，`HttpServer` 可跑 HTTPS
- 已具备一个框架级内建 middleware：`CorsMiddleware`
- 工程目录边界比第六阶段更接近参考仓库

**仍可继续优化：**
- 进一步整理旧文件目录，把更多模块迁移到子目录中
- 若未来需要，可补更复杂的 SSL 双向认证与更细粒度配置
- route group / group middleware 仍可作为增强项继续推进

---

### 5. 阶段总结
#### 一句话总结
第七阶段的核心，是补齐 HTTPS/SSL 与内建 CORS middleware，让 HTTP 框架层从“边界已经拆清”进一步走到“核心能力和工程结构都基本对齐参考仓库”。

#### 当前结论
到这里，当前项目已经可以认为达成了“HTTP 框架层基本对齐参考仓库”的目标：

- 模块边界对齐
- 核心能力对齐
- 工程结构接近
- 但不追求逐文件逐接口复制

### 6. 后续方向
#### 若继续演进可以做什么
后续如果还要继续深化，不再是“补齐参考仓库差距”，而是走自己的增强路线，例如：

1. route group / group middleware
2. 更丰富的内建 middleware 生态
3. Redis/DB `SessionStorage`
4. 更强路由表达能力
5. 更完整的 HTTPS 配置与双向认证

---

## 开发补充记录：HTTP 压测驱动的 Bug 发现与修复

### 1. 这个 bug 是怎么发现的
这次问题不是先从单元测试里冒出来的，而是先在 HTTP 压测过程中被打出来的。

#### 当时的背景
为了对当前目录下的 HTTP server 做压测，我先补了一个独立的 `http_bench_server`，再用 `wrk` 跑三类场景：

- 基准吞吐：`GET /ping`
- 混合路由：`/ping`、`/user/me`、`/user/:id`
- 边界路径：`/blocked`、连接上限拒绝

#### 最开始看到的现象
先用 `curl` 探活时，一切都正常：

- `/ping` 能返回 `pong`
- `/user/me` 能返回 `exact-user`
- `/user/42` 能返回 `user:42`

但切到 `wrk` 之后，现象立刻不对：

- `wrk` 对 `/ping` 压测时出现 `0 requests`
- 同时伴随大量 `read error`
- 服务端并没有明显的业务异常，说明不是路由 handler 本身写错了

这说明问题不在“业务逻辑是否能跑通”，而在“HTTP 响应是否对高并发 keep-alive 客户端足够规范”。

#### 为什么这个现象很关键
`curl` 单次请求成功，只能说明：

- 服务端大体能收请求
- 服务端大体能回内容

但它并不能证明：

- 响应边界是明确的
- keep-alive 语义是稳定的
- 高并发客户端能够持续正确解析多条响应

而 `wrk` 正好会把这类问题放大。

---

### 2. Bug 1：普通 HTTP 响应缺少稳定的报文边界

#### 直接原因
当时 `HttpSession::sendResponse()` 对普通响应的发送方式是：

1. 先调用 `response->toHeaderString()` 发送响应头
2. 再单独发送 body

问题在于：

- `toHeaderString()` 只序列化响应头
- 它不会自动补 `Content-Length`
- 普通响应路径又没有在发送 header 前稳定补上 `Content-Length`

于是服务端虽然“确实发出了内容”，但对于需要复用连接、持续解析响应边界的客户端来说，边界是不稳定的。

#### 为什么 `curl` 看起来没事，`wrk` 却挂了
因为 `curl` 在很多简单场景下可以依赖“连接关闭”来判断响应结束，所以表面上还能读到 body。

但 `wrk` 这种压测客户端会更依赖：

- `Content-Length`
- keep-alive 连接复用
- 明确的响应边界

一旦边界不清晰，它就很容易把响应判成读失败，最终表现成：

- `0 requests`
- 大量 `Socket read error`

换句话说，这不是“服务完全没响应”，而是“服务响应对高并发客户端来说不够规范”。

---

### 3. Bug 2：修完主问题后，又暴露了连接上限拒绝路径的问题

把普通响应修到能被 `wrk` 正确解析之后，我又回归跑了 `./bin/test_http_server`。

这时连接上限场景开始失败，断言位置是：

- 预期响应里应该包含 `too many active connections`

但测试有时只能读到 `503 Service Unavailable`，拿不到 body 里的详细错误信息。

#### 根因
`HttpServer` 在超过 `max_connections` 时，原来的处理方式接近于：

1. 接到连接
2. 立刻构造 `503`
3. 发送响应
4. 直接关闭连接

问题在于此时服务端几乎没有消费客户端刚写进来的请求数据。

在 TCP 语义下，如果：

- 客户端已经发了请求
- 服务端没把入站数据读掉
- 却很快 `send + close`

那么对端更容易看到异常关闭，或者拿不到完整 body。

所以这个问题的本质不是“503 没发出去”，而是“拒绝路径没有保证客户端稳定收到完整错误响应”。

---

### 4. 修复过程
这次修复不是一下就改对，而是分成了两轮定位。

#### 第一步：先确认问题不是路由或业务 handler
先用 `curl` 验证常规路由：

- `/ping`
- `/user/me`
- `/user/:id`

结果都正常，所以很快可以排除：

- 路由注册错误
- handler 没被调用
- 响应体完全没写

#### 第二步：对比 `curl -v` 和 `wrk` 的行为
接着对 `/ping` 做最小化复现：

- `curl -v` 能看到响应头和 body
- `wrk` 单连接都可能出现 `0 requests`

这一步把问题收敛到了协议层，而不是业务层。

进一步看响应头时，发现一个关键点：

- 普通响应里没有稳定出现 `Content-Length`

于是怀疑点就落到了：

- `HttpResponse::toHeaderString()`
- `HttpResponse::toString()`
- `HttpSession::sendResponse()`

之间的组合关系上。

#### 第三步：修复普通响应发送路径
最终的修法不是继续维持“header/body 两次写”，而是直接改成：

- 普通响应走 `response->toString()`
- 一次性序列化完整 HTTP 报文
- 一次性发送完整响应

这样同时解决了两个问题：

1. `toString()` 会自动补齐 `Content-Length`
2. 避免 header/body 分开发送时，在异常关闭路径上丢 body

改完之后重新跑压测：

- `throughput` 场景恢复正常
- `mixed` 场景恢复正常
- `wrk` 不再出现一开始那种 `0 requests` 的主故障

#### 第四步：回归时发现连接上限拒绝路径又出问题
修完主问题后，重新跑 `./bin/test_http_server`，结果旧测试挂了。

失败点不是普通 `/ping`，而是连接上限拒绝场景：

- 状态码能到 `503`
- 但 body 中的 `too many active connections` 有时读不到

这说明主链路修好了，但边缘拒绝路径还不够稳。

#### 第五步：定位到超限拒绝分支的关闭时机
继续看 `HttpServer` 的超限逻辑，发现它在超过 `max_connections` 时，几乎不读请求就直接返回错误并关闭连接。

这就解释了为什么测试会不稳定：

- 客户端请求已经发出
- 服务端没有把这条请求先消费掉
- 随后直接拒绝并关闭

对端就更容易只读到部分响应，或者在错误关闭下丢掉 body。

#### 第六步：修复拒绝路径
最终的处理方式改成：

1. 在超限分支先快速读入一条请求
2. 若成功读到请求，则让错误响应继承请求的 HTTP 版本
3. 再构造 `503 Service Unavailable`
4. 返回完整错误响应后关闭连接

这样做的核心目的不是“让逻辑更复杂”，而是保证：

- 拒绝路径也按 HTTP 语义稳定收尾
- 客户端能更可靠地拿到完整错误响应

修完之后再跑回归：

- `./bin/test_http_server` 恢复通过
- `wrk` 的三类场景也都能正常跑通

---

### 5. Bug 3：`Release` 下 `edge/conn-limit` 收尾偶发崩溃

#### 现象

- `edge conn-limit` 表面上是连接上限场景，但实际 `Release` 下复现到的崩溃点主要发生在前一个 `edge blocked` 场景结束、bench server 收尾退出时
- shell 侧表现为 `benchmarks/http/run.sh` 在 `http_bench_server` 退出阶段报 `Segmentation fault (core dumped)`

#### 根因

1. **底层对象池存在对齐错误**
   - `src/base/memorypool/memory_pool.cpp`
   - 原实现按“对象大小”做槽位对齐和步进，某些对象尺寸下会把后续槽位放到未对齐地址
   - `Release` 优化下这属于未定义行为，会把故障随机炸到 HTTP 压测场景

2. **HTTP stop 路径引入了额外竞态**
   - `src/http/server/http_server.cc`
   - 为了让 bench server 更快退出，曾在 `HttpServer::stop()` 里跨线程主动关闭活跃 client sockets
   - `wrk` 场景结束后连接本身已经会自然收尾，这段“主动关连接”反而把停机阶段变成了多线程 close 竞态点

#### 代码修复

- 修复 `MemoryPool` 槽位按 `alignof(std::max_align_t)` 对齐，并把 block 最小尺寸计算同步修正
- 回退 `HttpServer::stop()` 中跨线程追踪/主动关闭活跃 client 的逻辑，只保留 stop accept + worker 正常收尾
- 在 `tests/test_memory_pool.cc` 新增对齐回归测试，验证奇数槽位大小下返回地址仍满足最大自然对齐

#### 回归验证

- `cmake --build build-release --target http_bench_server test_http_server test_memory_pool -j8`
- `./bin/test_memory_pool` 通过
- `./bin/test_http_server` 通过
- `SERVER_IO_THREADS=8 SERVER_ACCEPT_THREADS=2 SERVER_SESSION_ENABLED=0 benchmarks/http/run.sh edge`
  - 连续回归 `10` 轮，无 `Segmentation fault`
  - `edge blocked` 全部稳定返回 `4xx`
  - `edge conn-limit` 全部稳定返回 `5xx`

#### 当前结论

- `conn-limit` 场景现在可以继续用于压测和结果留档
- 如果后续还要继续做 stop 优化，优先沿 worker 内部协作退出去做，不要再从外部线程直接批量 close 活跃 client

---

### 6. 这次问题的收获
这次 bug 的价值很高，因为它不是那种“一眼能看出来写错”的问题，而是典型的协议细节问题。

#### 收获 1：`curl` 正常不等于 HTTP 语义正确
单请求探活只能说明“基本能通”，不能说明：

- 响应边界正确
- keep-alive 兼容性正确
- 高并发客户端解析一定正确

#### 收获 2：压测工具不只是测性能，也是在帮忙找协议 bug
这次 `wrk` 暴露出来的，首先不是“性能瓶颈”，而是：

- 普通响应报文边界不明确
- 拒绝路径关闭时机不稳定

这类问题平时用功能测试很容易漏掉。

#### 收获 3：错误路径和拒绝路径也必须按完整 HTTP 响应来设计
服务端代码里“已经调用了 `sendResponse()`”不代表客户端一定能稳定读到完整结果。

真正要关心的是：

- 是否有明确的 `Content-Length`
- 是否在合适的时机关闭连接
- 是否在关闭前保证请求/响应状态足够一致

#### 收获 4：一次性发送完整普通响应，比 header/body 分开发送更稳
在当前这个项目里，普通 HTTP 响应优先保证正确性比省一点拷贝更重要。

所以这次修复最后采取的是：

- 普通响应直接 `toString()` 一次性序列化
- 再一次性发送

这样更符合当前阶段“先保证协议行为稳定”的目标。

---

### 7. 一句话总结
这次压测发现的核心问题，不是“服务器不会回包”，而是“服务器在普通响应和拒绝路径上，没有把 HTTP 报文边界与关闭时机处理到足够严谨”；后续又继续暴露出 `Release` 停机阶段的内存对齐和 close 竞态问题；而真正把问题打出来的，不是单请求功能测试，而是 `wrk` 这种 keep-alive 压测客户端。

---

## 2026-03-13 HTTP Server 核心架构矩阵压测报告

### 1. 压测目标与背景
- **目标**：探究在相同物理线程数（4个工作线程）下，单 IOM 架构与双 IOM 架构、独立栈与共享栈、协程池开关对性能的影响。
- **环境**：Linux 6.x, CPU 4-Core, Memory 8GB+, `wrk` 压力机。
- **变量控制**：
  - **UC=1 (单 IOM)**：1个主线程参与调度 + 3个后台 Worker = 4 线程 (Accept+IO 混合)。
  - **UC=0 (双 IOM)**：主线程睡眠，1个 Accept 专用后台线程 + 3个 IO 专用后台线程 = 4 线程。

### 2. 核心压测数据矩阵 (QPS)
| 架构组合 (SS:共享栈, FP:协程池, UC:Caller) | /ping (Baseline) | IO Intensive (20ms) | POST & Parse | 1MB Download |
| :--- | :--- | :--- | :--- | :--- |
| **组合 1: SS=0, FP=0, UC=0 (双 IOM)** | 54,225 | 41,659 | 51,248 | 514 |
| **组合 2: SS=0, FP=0, UC=1 (单 IOM)** | **59,271** | **43,594** | **57,258** | 499 |
| **组合 3: SS=0, FP=1, UC=0** | 54,983 | 42,046 | 50,903 | 502 |
| **组合 4: SS=0, FP=1, UC=1** | 58,842 | 43,501 | 56,837 | 498 |
| **组合 5: SS=1, FP=0, UC=0** | 41,253 | 13,487 | 10,274 | 506 |
| **组合 6: SS=1, FP=0, UC=1** | 22,197 | 3,660 | 13,547 | 482 |
| **组合 7: SS=1, FP=1, UC=0** | 41,218 | 11,035 | 10,161 | 506 |
| **组合 8: SS=1, FP=1, UC=1** | 22,724 | 4,240 | 13,187 | 518 |

### 3. 技术结论
1.  **单 IOM + use_caller (Single Reactor) 胜出**：
    在 4 线程对齐测试中，开启 `use_caller=1` 后的单 IOM 架构在纯吞吐和 POST 解析上领先双 IOM 架构约 **10%~12%**。这证明了消除“跨线程 Accept 到 IO 的 tickle 唤醒开销”能带来实质性的吞吐提升。
2.  **共享栈 (Shared Stack) 的局限性**：
    在 HTTP 高频请求场景下，共享栈频繁的 `memcpy` 导致 QPS 暴跌约 **40%~60%**。尤其是在 `use_caller=1` 时，由于主线程栈与协程池的共享内存块在极高并发下产生了严重的 CPU 缓存颠簸或内存页缺页中断，性能损耗最严重。共享栈仅适用于极海量协程（>10万）且对内存极度敏感的非高频 IO 场景。
3.  **协程池 (Fiber Pool) 的稳定性**：
    在独立栈模式下，协程池对纯 QPS 提升不明显，但在 IO 密集型场景（20ms 延时）下能提供更平稳的响应延迟分布。

### 4. 最佳配置建议
对于高性能 HTTP 服务场景，**推荐配置：独立栈 (SS=0) + 协程池 (FP=1) + 单 IOM 主线程参与调度 (UC=1)**。

---

## 2026-03-13 Release 版压测归档（修正版，以本节为准）

这次记录对应的是完成 benchmark 清理后的正式复测版本，和上面那段“核心架构矩阵压测报告”不是同一轮实验条件。

本次变化包括：

- benchmark 默认关闭自动 Session，避免 `/ping` 一类场景被 `SID` 创建和 `Set-Cookie` 污染
- `accept_threads` 已修正为真正生效
- `POST` 场景改为真实 JSON 解析
- `1MB download` 改为静态 payload，减少每请求分配噪音
- matrix 脚本改为每个 case 独立起服，避免状态串扰

### 1. 日志归档位置

本次压测原始日志最初写在 `/tmp`，随后已经归档到项目目录：

- `benchmarks/http/results/2026-03-13/base_release_wsl2_8io2accept.log`
- `benchmarks/http/results/2026-03-13/matrix_release_wsl2_8io2accept.log`

### 2. 压测环境

- **构建方式**：`Release`
- **系统**：`Linux 6.6.87.2-microsoft-standard-WSL2 x86_64`
- **CPU 并发**：`16 vCPU`
- **压测地址**：`127.0.0.1` 回环
- **服务端参数**：`SERVER_IO_THREADS=8`、`SERVER_ACCEPT_THREADS=2`
- **HTTP Session**：`SERVER_SESSION_ENABLED=0`

### 3. 基础场景对照

| 场景 | 压测参数 | Requests/sec | 关键结果 |
| :--- | :--- | ---: | :--- |
| `throughput /ping` | `t=4 c=256 d=10s` | 99,663.25 | p99=`5.70ms`，`timeout=253` |
| `mixed` | `t=4 c=256 d=10s` | 93,050.76 | 全部 `2xx` |
| `edge blocked` | `t=2 c=16 d=5s` | 11,553.49 | 全部 `4xx`，行为符合预期 |
| `edge conn-limit` | `t=2 c=8 d=5s` | 2,024.03 | 全部 `5xx`，结束时 bench server 出现一次崩溃 |

### 4. 调度矩阵对照表

| shared_stack | fiber_pool | use_caller | `/ping` req/s | `Timer Wait` req/s | `POST JSON` req/s | `1MB 下载` req/s |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 0 | 0 | 92,930.69 | 45,136.40 | 47,600.25 | 497.39 |
| 0 | 0 | 1 | 103,880.36 | 44,911.13 | 47,644.89 | 489.64 |
| 0 | 1 | 0 | 102,131.99 | 44,917.68 | 47,462.43 | 470.23 |
| 0 | 1 | 1 | 92,896.01 | 44,813.69 | 47,847.46 | 571.07 |
| 1 | 0 | 0 | 23,107.56 | 4,933.51 | 34,433.53 | 510.72 |
| 1 | 0 | 1 | 22,881.72 | 4,900.64 | 35,827.57 | 484.79 |
| 1 | 1 | 0 | 22,350.19 | 4,663.37 | 34,639.12 | 484.45 |
| 1 | 1 | 1 | 25,641.62 | 4,894.81 | 28,172.62 | 481.03 |

### 5. 当前结论

1. **`shared_stack=0` 明显优于 `shared_stack=1`**  
   这是本轮数据里最稳定、最明确的结论。无论是 `/ping`、定时等待场景，还是 `POST JSON`，共享栈模式都明显落后。

2. **在 `shared_stack=0` 前提下，`fiber_pool` 和 `use_caller` 的差异远小于共享栈开关本身**  
   非共享栈四组之间差异存在，但都还在可接受范围内；整体属于“微调优化”，不是决定性因素。

3. **连接上限场景还有稳定性问题**  
   `edge conn-limit` 的业务语义是对的，`wrk` 也已经稳定打到 `5xx`；但 bench server 在该场景结束后出现过一次 `Segmentation fault`，所以这个分支目前不能算完全压测通过。

### 6. 当前推荐

- 如果目标是继续做 HTTP 框架吞吐压测，优先使用：`shared_stack=0`
- 若继续细调，可在以下几组内比较：
  - `shared_stack=0, fiber_pool=0, use_caller=1`
  - `shared_stack=0, fiber_pool=1, use_caller=0`
  - `shared_stack=0, fiber_pool=1, use_caller=1`
- 在修掉 `conn-limit` 崩溃前，不建议把连接上限场景的结果直接作为“稳定性已验证”的结论
