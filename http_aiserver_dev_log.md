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
