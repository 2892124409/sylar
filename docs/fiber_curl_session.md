# FiberCurlSession 技术文档

## 1. 概述

`FiberCurlSession` 是 sylar 项目中将 libcurl 网络请求与协程调度器深度整合的核心类。它的目标是：**让一次 HTTP 请求在等待网络 IO 期间自动挂起当前 fiber，把线程资源让给其他 fiber，当 IO 就绪后再恢复执行**，从而在不阻塞工作线程的前提下完成 AI 接口调用。

---

## 2. curl_easy_perform 原理

### 2.1 同步阻塞模型

`curl_easy_perform` 是 libcurl 最简单的接口，调用后**当前线程被完全占用**，直到请求完成或超时才返回。

```
调用线程
  │
  ▼
curl_easy_perform(easy)
  │  ┌─────────────────────────────────────────┐
  │  │  内部循环：                              │
  │  │  1. connect()  → 阻塞等待 TCP 握手      │
  │  │  2. send()     → 阻塞等待发送缓冲区     │
  │  │  3. recv()     → 阻塞等待服务器响应     │
  │  │  4. 重复直到传输完成                    │
  │  └─────────────────────────────────────────┘
  │
  ▼
返回 CURLcode
```

### 2.2 内部实现流程

libcurl 在 `curl_easy_perform` 内部维护一个状态机，大致步骤：

1. **DNS 解析**：调用系统 `getaddrinfo`（可能阻塞数百毫秒）
2. **TCP 连接**：`connect()` 系统调用，等待三次握手
3. **TLS 握手**（HTTPS）：多轮 `read/write` 交换证书和密钥
4. **发送请求**：`write()` 写入 HTTP 请求头和 body
5. **接收响应**：`read()` 循环读取，直到响应完整
6. **关闭连接**：`close()` 或放回连接池

每一步都是**同步系统调用**，线程在内核态等待 IO 完成期间无法做任何其他事情。

### 2.3 在协程模型下的问题

sylar 的工作线程数量有限（通常等于 CPU 核数）。如果每个 AI 请求都用 `curl_easy_perform`，一次请求（通常耗时 1~30 秒）就会独占一个工作线程，导致：

- 并发请求数 ≤ 工作线程数
- 线程大部分时间在等待网络，CPU 利用率极低
- 无法利用协程调度器的 IO 多路复用能力

### 2.4 为什么 sylar hook 无法解决这个问题

sylar 的 hook 机制通过 `dlsym(RTLD_NEXT, ...)` 拦截 `read`/`write`/`connect` 等系统调用，将阻塞 IO 转化为协程友好的非阻塞等待。`main.cc` 在启动时调用了 `sylar::set_hook_enable(true)`，看起来似乎 `curl_easy_perform` 内部的 IO 调用也会被自动协程化。

**但实际上 hook 对 libcurl 完全失效，原因有两层：**

**原因一：libcurl 主动设置 O_NONBLOCK，绕过 do_io 的 hook 逻辑**

libcurl 在创建 socket 后会主动调用 `fcntl(fd, F_SETFL, O_NONBLOCK)` 将其设为非阻塞。hook 的 `fcntl` 拦截到这个调用后，会在 `FdCtx` 中记录 `userNonblock = true`：

```cpp
// hook.cc
ctx->setUserNonblock(arg & O_NONBLOCK);
```

而 `do_io` 模板函数在入口处有如下判断：

```cpp
// hook.cc
if (!ctx || ctx->isClose() || !ctx->isSocket() || ctx->getUserNonblock())
{
    return fun(fd, std::forward<Args>(args)...);  // 直接调原始函数，不做任何 fiber 化
}
```

`getUserNonblock()` 为 `true`，`do_io` 直接 bypass，`read`/`write`/`recv`/`send` 的 hook 全部失效。

**原因二：libcurl 用 select()/poll() 等待 IO，而这两个函数根本没被 hook**

libcurl easy 模式内部在 socket 返回 `EAGAIN` 后，会调用 `select()` 或 `poll()` 阻塞等待 IO 就绪。sylar hook 的函数列表为：

```
sleep / usleep / nanosleep
socket / connect / accept
read / readv / recv / recvfrom / recvmsg
write / writev / send / sendto / sendmsg
close / fcntl / ioctl / getsockopt / setsockopt
```

`select` 和 `poll` 不在列表中，调用后线程直接阻塞在内核，协程调度器完全感知不到，工作线程被占死。

**结论**

`curl_easy_perform` 在 hook 开启的环境下依然会阻塞工作线程，`FiberCurlSession` 通过 `curl_multi` 的 socket/timer 回调机制绕开了这两个问题，是必要的设计而非多余的封装。

---

## 3. curl_multi + socket/timer 替代方案原理

### 3.1 事件驱动模型

`curl_multi` 接口将 libcurl 的内部状态机暴露为**事件驱动**模式：libcurl 不再自己阻塞等待 IO，而是告诉上层"我需要监听哪些 fd 的哪些事件"，由上层的事件循环（epoll/kqueue 等）负责等待，事件就绪后再通知 libcurl 继续推进。

```
上层事件循环（epoll）
       │
       │ fd 可读/可写 或 超时
       ▼
curl_multi_socket_action(multi, fd, action, &running)
       │
       │ libcurl 内部推进状态机
       ▼
  继续传输 / 完成 / 错误
```

### 3.2 两个关键回调

#### CURLMOPT_SOCKETFUNCTION（socket 回调）

libcurl 通过此回调通知上层**应该监听哪个 fd 的哪种事件**：

```c
int socket_callback(CURL* easy, curl_socket_t s, int what, void* userp, void* socketp);
```

`what` 的取值：

| 值                | 含义                         |
|-------------------|------------------------------|
| `CURL_POLL_IN`    | 监听 fd 可读                 |
| `CURL_POLL_OUT`   | 监听 fd 可写                 |
| `CURL_POLL_INOUT` | 同时监听可读和可写           |
| `CURL_POLL_REMOVE`| 取消对该 fd 的所有监听       |

上层收到回调后，应将对应 fd 注册到自己的事件循环（如 epoll）。

#### CURLMOPT_TIMERFUNCTION（timer 回调）

libcurl 通过此回调通知上层**下一次超时应在多久后触发**：

```c
int timer_callback(CURLM* multi, long timeout_ms, void* userp);
```

`timeout_ms` 的语义：

| 值      | 含义                                         |
|---------|----------------------------------------------|
| `-1`    | 取消定时器，libcurl 不需要超时               |
| `0`     | 立即触发（调用 `curl_multi_socket_action`）  |
| `> 0`   | 在 `timeout_ms` 毫秒后触发                  |

### 3.3 驱动状态机：curl_multi_socket_action

每当 fd 事件就绪或定时器触发，上层调用：

```c
curl_multi_socket_action(multi, fd, action, &running);
```

- `fd`：就绪的 socket fd，或 `CURL_SOCKET_TIMEOUT`（定时器触发）
- `action`：`CURL_CSELECT_IN`（可读）、`CURL_CSELECT_OUT`（可写）、或 `0`（超时）
- `running`：输出参数，表示仍在进行中的传输数量

libcurl 内部根据这些信息推进对应连接的状态机，可能触发新的 socket/timer 回调（更新监听配置），最终通过 `curl_multi_info_read` 报告完成状态。

### 3.4 完整驱动循环

```
初始化：curl_multi_socket_action(CURL_SOCKET_TIMEOUT, 0)  ← 踢一脚启动
    │
    ├─ 触发 SocketCallback → 上层注册 fd 到 epoll
    └─ 触发 TimerCallback  → 上层设置定时器

等待事件（epoll_wait / 定时器）
    │
    ├─ fd 可读/可写 → curl_multi_socket_action(fd, CURL_CSELECT_IN/OUT)
    └─ 定时器触发  → curl_multi_socket_action(CURL_SOCKET_TIMEOUT, 0)
         │
         ├─ 可能再次触发 SocketCallback/TimerCallback（更新监听）
         └─ curl_multi_info_read → CURLMSG_DONE → 传输完成
```

### 3.5 独立示例：用 epoll 驱动 curl_multi（无协程）

下面是一个不依赖任何协程框架、仅用 epoll 驱动 `curl_multi` 的完整示例，展示 socket/timer 回调的标准用法。

```cpp
#include <curl/curl.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <map>
#include <cstring>
#include <cstdio>

// ---- 全局状态 ----
static CURLM*  g_multi   = nullptr;
static int     g_epfd    = -1;   // epoll fd
static int     g_timerfd = -1;   // timerfd 用于 curl 超时
static int     g_running = 0;    // 仍在传输的 easy handle 数量

// ---- socket 回调：libcurl 告知上层应监听哪个 fd ----
static int socket_cb(CURL*, curl_socket_t s, int what, void*, void*)
{
    if (what == CURL_POLL_REMOVE)
    {
        epoll_ctl(g_epfd, EPOLL_CTL_DEL, s, nullptr);
        return 0;
    }

    uint32_t events = 0;
    if (what == CURL_POLL_IN  || what == CURL_POLL_INOUT) events |= EPOLLIN;
    if (what == CURL_POLL_OUT || what == CURL_POLL_INOUT) events |= EPOLLOUT;

    epoll_event ev{};
    ev.events  = events;
    ev.data.fd = s;

    // 先尝试 MOD，失败则 ADD（fd 可能是第一次注册）
    if (epoll_ctl(g_epfd, EPOLL_CTL_MOD, s, &ev) != 0)
        epoll_ctl(g_epfd, EPOLL_CTL_ADD, s, &ev);

    return 0;
}

// ---- timer 回调：libcurl 告知上层下次超时时间 ----
static int timer_cb(CURLM*, long timeout_ms, void*)
{
    if (timeout_ms < 0)
    {
        // 取消定时器
        itimerspec its{};
        timerfd_settime(g_timerfd, 0, &its, nullptr);
        return 0;
    }

    if (timeout_ms == 0) timeout_ms = 1; // timerfd 不支持 0，设为 1ms

    itimerspec its{};
    its.it_value.tv_sec  = timeout_ms / 1000;
    its.it_value.tv_nsec = (timeout_ms % 1000) * 1000000;
    timerfd_settime(g_timerfd, 0, &its, nullptr);
    return 0;
}

// ---- 主驱动循环 ----
static void event_loop()
{
    epoll_event events[16];

    while (g_running > 0)
    {
        int n = epoll_wait(g_epfd, events, 16, -1);
        for (int i = 0; i < n; ++i)
        {
            int fd = events[i].data.fd;

            if (fd == g_timerfd)
            {
                // 消费 timerfd 计数
                uint64_t exp;
                read(g_timerfd, &exp, sizeof(exp));
                // 超时事件：用 CURL_SOCKET_TIMEOUT 推进状态机
                curl_multi_socket_action(g_multi, CURL_SOCKET_TIMEOUT, 0, &g_running);
            }
            else
            {
                // socket IO 事件
                int action = 0;
                if (events[i].events & EPOLLIN)  action |= CURL_CSELECT_IN;
                if (events[i].events & EPOLLOUT) action |= CURL_CSELECT_OUT;
                curl_multi_socket_action(g_multi, fd, action, &g_running);
            }

            // 读取完成消息
            int remaining = 0;
            CURLMsg* msg;
            while ((msg = curl_multi_info_read(g_multi, &remaining)) != nullptr)
            {
                if (msg->msg == CURLMSG_DONE)
                    printf("transfer done, result=%d\n", msg->data.result);
            }
        }
    }
}

int main()
{
    curl_global_init(CURL_GLOBAL_ALL);

    // 创建 epoll 和 timerfd
    g_epfd    = epoll_create1(0);
    g_timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

    // 把 timerfd 注册到 epoll
    epoll_event tev{};
    tev.events  = EPOLLIN;
    tev.data.fd = g_timerfd;
    epoll_ctl(g_epfd, EPOLL_CTL_ADD, g_timerfd, &tev);

    // 初始化 multi handle 并注册回调
    g_multi = curl_multi_init();
    curl_multi_setopt(g_multi, CURLMOPT_SOCKETFUNCTION, socket_cb);
    curl_multi_setopt(g_multi, CURLMOPT_TIMERFUNCTION,  timer_cb);

    // 创建 easy handle 并挂载
    CURL* easy = curl_easy_init();
    curl_easy_setopt(easy, CURLOPT_URL, "https://example.com");
    curl_multi_add_handle(g_multi, easy);

    // 踢一脚启动状态机，触发首次 socket/timer 回调
    curl_multi_socket_action(g_multi, CURL_SOCKET_TIMEOUT, 0, &g_running);

    // 进入事件循环，直到传输完成
    event_loop();

    // 清理
    curl_multi_remove_handle(g_multi, easy);
    curl_easy_cleanup(easy);
    curl_multi_cleanup(g_multi);
    close(g_timerfd);
    close(g_epfd);
    curl_global_cleanup();
    return 0;
}
```

与 `curl_easy_perform` 的对比：

| | `curl_easy_perform` | `curl_multi` + epoll |
|---|---|---|
| 线程占用 | 全程阻塞 | `epoll_wait` 期间可做其他事 |
| 并发能力 | 1 线程 = 1 请求 | 1 线程可驱动多个 easy handle |
| 与协程集成 | 不可行（select 未被 hook） | 可行（将 epoll_wait 替换为 IOManager） |

`FiberCurlSession` 本质上就是把上面示例中的 `epoll_wait` 替换为 `IOManager::addEvent` + `Fiber::YieldToHold()`，让等待期间的线程资源归还给协程调度器。

### 3.6 纯 Reactor 与 sylar IOManager+Fiber 的对比

两种方案都能解决 `curl_easy_perform` 阻塞线程的问题，但在代码结构和使用方式上有本质区别。

**纯 Reactor 方案**

curl_multi 的 socket/timer 回调直接对接 epoll，整个请求流程被切割成碎片化的回调链，业务逻辑必须拆分到多个回调函数中：

```cpp
// 纯 Reactor：逻辑碎片化，完成后的处理必须在回调里继续
void on_transfer_done(CURLcode result) {
    if (result != CURLE_OK) { handle_error(); return; }
    parse_response();
    write_to_db();       // 如果这里也是异步的，又要再嵌一层回调
    send_http_response();
}
```

**sylar IOManager + Fiber 方案**

`FiberCurlSession` 将 curl_multi 的 socket/timer 回调对接到 `IOManager`，事件就绪后不执行业务回调，而是 `schedule(fiber)` 恢复挂起的协程，业务逻辑保持顺序书写：

```cpp
// sylar Fiber：逻辑连续，与同步代码结构相同
void handle_ai_request() {
    FiberCurlSession session(easy);
    CURLcode result = session.Perform();  // 内部 yield，不阻塞线程
    if (result != CURLE_OK) { handle_error(); return; }
    parse_response();
    write_to_db();       // 同样可以是 fiber 化的异步操作
    send_http_response();
}
```

**关键差异对比**

| 维度 | 纯 Reactor（epoll 回调） | sylar IOManager + Fiber |
|------|--------------------------|-------------------------|
| 代码结构 | 回调链，逻辑碎片化 | 顺序代码，结构清晰 |
| 错误处理 | 每个回调单独处理，容易遗漏 | 统一返回值检查 |
| 局部变量 | 需堆分配上下文对象传递状态 | 直接用栈变量，fiber 保存栈帧 |
| 并发能力 | 1 线程驱动多请求（回调复用） | 1 线程驱动多 fiber，每 fiber 一请求 |
| 调试难度 | 调用栈断裂，难以追踪 | 调用栈完整，断点调试友好 |

**IOManager 作为 Reactor 的角色**

sylar `IOManager` 本身就是一个标准的 Reactor（epoll 驱动），内部为每个 fd 维护 `FdContext`，存储 READ/WRITE 事件对应的 fiber 或回调：

```
IOManager 内部：
  epoll fd
    ├─ FdContext[fd0]: read.fiber / write.fiber（或 read.cb / write.cb）
    ├─ FdContext[fd1]: ...
    └─ ...

idle() 循环：
  epoll_wait()
    └─ 遍历就绪事件 → triggerEvent(fd, READ/WRITE)
           ├─ 若绑定了 fiber → schedule(fiber)   ← 与纯 Reactor 的分叉点
           └─ 若绑定了 cb   → schedule(cb)
```

`FiberCurlSession` 利用 `addEvent` 支持绑定 `std::function<void()>` 回调的能力，在回调里执行 `schedule(m_wait_fiber)`，把 Reactor 的事件通知转化为 Fiber 的恢复信号，完成两套模型的桥接。

---

## 4. sylar IOManager 与 Fiber 协程模型

### 4.1 IOManager

sylar 的 `IOManager` 基于 epoll 实现，核心能力：

- `addEvent(fd, event, callback)`：向 epoll 注册 fd 事件，事件触发时执行 callback
- `delEvent(fd, event)`：取消 fd 事件监听
- `addTimer(ms, callback, recurring)`：添加定时器
- `schedule(fiber, thread)`：将 fiber 调度到指定线程执行

**关键特性：一次性触发语义（one-shot）**

sylar IOManager 的 `addEvent` 使用 `EPOLLONESHOT` 语义——事件触发一次后自动从 epoll 中移除，下次需要重新 `addEvent`。这与 Linux 默认的 level-triggered 不同，需要特别处理。

### 4.2 Fiber 协程

sylar 的 `Fiber` 是基于 `ucontext` 实现的用户态协程：

- `Fiber::YieldToHold()`：当前 fiber 挂起，让出执行权给调度器
- `IOManager::schedule(fiber)`：将 fiber 重新加入调度队列，等待被执行

一个典型的 IO 等待模式：

```
fiber A 执行
  │
  ├─ 注册 fd 事件，callback = [唤醒 fiber A]
  ├─ YieldToHold()  ← fiber A 挂起，线程去执行其他 fiber
  │
  │  ... 其他 fiber 在此线程上执行 ...
  │
  │  epoll 检测到 fd 就绪 → 执行 callback → schedule(fiber A)
  │
  └─ fiber A 被恢复，继续执行
```

---

## 5. FiberCurlSession：curl_multi 与协程的桥接

### 5.1 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                      FiberCurlSession                       │
│                                                             │
│  ┌──────────┐    socket/timer 回调    ┌──────────────────┐  │
│  │ curl_multi│ ─────────────────────▶│ OnSocketEvent()  │  │
│  │  (状态机) │                        │ UpdateTimer()    │  │
│  └──────────┘                         └────────┬─────────┘  │
│       ▲                                       │             │
│       │ socket_action()                       │ push event  │
│       │                                       ▼             │
│  ┌────┴─────┐                        ┌──────────────────┐  │
│  │ Perform()│◀── resume ────────────│ m_pending_actions│  │
│  │  主循环   │                        └──────────────────┘  │
│  └────┬─────┘                                │             │
│       │ YieldToHold()                        │ schedule()  │
│       ▼                                      ▼             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │                  sylar IOManager                     │  │
│  │   addEvent(fd, READ/WRITE, cb)  addTimer(ms, cb)     │  │
│  │   epoll_wait → 触发 cb → schedule(m_wait_fiber)      │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────┘
```

### 5.2 关键数据结构

| 字段 | 类型 | 作用 |
|------|------|------|
| `m_easy` | `CURL*` | 外部传入的 easy handle，不拥有生命周期 |
| `m_multi` | `CURLM*` | 本次请求私有的 multi handle，驱动状态机 |
| `m_iom` | `IOManager*` | 当前线程的 IOManager，构造时捕获 |
| `m_wait_fiber` | `Fiber::ptr` | 执行 Perform() 的 fiber，用于恢复 |
| `m_wait_thread` | `int` | fiber 绑定的线程 id，确保恢复到同一线程 |
| `m_pending_actions` | `deque<pair<fd, action>>` | socket 事件队列，回调写入，主循环消费 |
| `m_watch_events` | `map<fd, events>` | libcurl 期望监听的事件掩码 |
| `m_armed_events` | `map<fd, events>` | 已实际挂载到 IOManager 的事件掩码 |
| `m_timer` | `Timer::ptr` | curl 定时器在 IOManager 上的映射 |
| `m_waiting` | `bool` | fiber 是否处于 YieldToHold 状态 |
| `m_resume_scheduled` | `bool` | 防止同一轮唤醒多次 schedule |
| `m_done` | `bool` | 请求是否已完成 |
| `m_result` | `CURLcode` | 请求最终结果 |

### 5.3 核心流程 Perform()

```cpp
CURLcode Perform() {
    // Step 1: 无 IOManager 或不在 fiber 中 → 降级为阻塞调用
    if (!m_iom || !m_wait_fiber)
        return curl_easy_perform(m_easy);

    // Step 2: 初始化 multi handle
    m_multi = curl_multi_init();

    // Step 3: 注册回调，this 作为 userp 传入
    curl_multi_setopt(m_multi, CURLMOPT_SOCKETFUNCTION, SocketCallback);
    curl_multi_setopt(m_multi, CURLMOPT_TIMERFUNCTION,  TimerCallback);

    // Step 4: 挂载 easy handle
    curl_multi_add_handle(m_multi, m_easy);

    // Step 5: 踢一脚启动状态机（触发首次 socket/timer 回调）
    curl_multi_socket_action(m_multi, CURL_SOCKET_TIMEOUT, 0, &running);
    DrainMessages();

    // Step 6: 主循环
    while (!m_done && running > 0) {
        WaitForSignal();           // 队列空时 yield，等待 IO/Timer 唤醒
        while (PopPendingAction(fd, action)) {
            curl_multi_socket_action(m_multi, fd, action, &running);
            DrainMessages();       // 检查是否 DONE
        }
    }

    Cleanup();
    return m_result;
}
```

### 5.4 socket 事件处理链路

```
libcurl 内部发现需要监听某个 fd
    │
    ▼
SocketCallback(easy, fd, what, userp=this, socketp)
    │
    ▼
RegisterSocketWatch(fd, what)
    │
    ├─ what == CURL_POLL_REMOVE → CancelSocketWatch(fd)
    │
    └─ 计算 target_events（READ/WRITE 掩码）
           │
           ├─ 对比 m_armed_events[fd]，算出 to_del 和 to_add
           ├─ 乐观更新 m_armed_events（锁内先写，防并发重复 addEvent）
           │
           ├─ iom->delEvent(fd, READ/WRITE)   ← 删除不再需要的监听
           └─ iom->addEvent(fd, READ/WRITE,   ← 注册新增监听
                  [this, fd]() { OnSocketEvent(fd, CURL_CSELECT_IN/OUT); })
```

当 epoll 检测到 fd 就绪，执行 callback：

```
OnSocketEvent(fd, action)
    │
    ├─ 从 m_armed_events 中清除已触发的事件位（one-shot 语义补偿）
    ├─ RearmSocketWatch(fd)  ← 按 m_watch_events 重新挂载缺失的监听
    │
    ├─ m_pending_actions.push_back({fd, action})
    │
    └─ 若 m_waiting && !m_resume_scheduled：
           m_resume_scheduled = true
           iom->schedule(m_wait_fiber, m_wait_thread)  ← 唤醒 fiber
```

### 5.5 timer 事件处理链路

```
libcurl 需要更新超时时间
    │
    ▼
TimerCallback(multi, timeout_ms, userp=this)
    │
    ▼
UpdateTimer(timeout_ms)
    │
    ├─ 先取消旧定时器（m_timer->cancel()）
    │
    ├─ timeout_ms < 0  → 不设新定时器（libcurl 不需要超时）
    ├─ timeout_ms == 0 → 立即调用 OnSocketEvent(CURL_SOCKET_TIMEOUT, 0)
    └─ timeout_ms > 0  → iom->addTimer(timeout_ms,
                              [this]() { OnSocketEvent(CURL_SOCKET_TIMEOUT, 0); })
```

定时器触发时，`OnSocketEvent(CURL_SOCKET_TIMEOUT, 0)` 将超时事件压入队列，唤醒 fiber，主循环调用 `curl_multi_socket_action(CURL_SOCKET_TIMEOUT, 0)` 推进状态机。

### 5.6 Fiber yield/resume 机制

`WaitForSignal()` 是 fiber 挂起的核心：

```cpp
void WaitForSignal() {
    {
        lock_guard lock(m_mutex);
        if (!m_pending_actions.empty()) return;  // 有事件直接处理，不挂起
        m_waiting = true;
        m_resume_scheduled = false;
    }
    Fiber::YieldToHold();   // ← fiber 挂起，线程去执行其他 fiber
    {
        lock_guard lock(m_mutex);
        m_waiting = false;
        m_resume_scheduled = false;
    }
}
```

时序保证：

```
主 fiber（Perform 循环）          IO 线程（epoll 回调）
        │                                │
        │ 检查队列为空                    │
        │ m_waiting = true               │
        │ YieldToHold() ─────────────────┤
        │                                │ fd 就绪
        │                                │ OnSocketEvent()
        │                                │ push_back(event)
        │                                │ m_resume_scheduled = true
        │                                │ iom->schedule(m_wait_fiber)
        │◀──────────────────────────────┤
        │ 恢复执行                        │
        │ m_waiting = false              │
        │ PopPendingAction()             │
        │ curl_multi_socket_action()     │
```

`m_resume_scheduled` 标志确保即使多个 fd 同时就绪，也只 schedule 一次 fiber，避免重复唤醒。

### 5.7 one-shot 事件模型的处理（RearmSocketWatch）

先明确两个核心状态表（这也是本实现最容易混淆的点）：

| 成员 | 含义 | 谁来更新 |
|---|---|---|
| `m_watch_events[fd]` | **目标态**：libcurl 当前希望监听的事件位（READ/WRITE） | `RegisterSocketWatch(fd, what)` 根据 `what` 改写 |
| `m_armed_events[fd]` | **已挂载态**：当前已挂到 IOManager（含“已预占位、准备 addEvent”）的事件位 | `RegisterSocketWatch` / `OnSocketEvent` / `RearmSocketWatch` |

可以把它理解为：

- `m_watch_events` 回答“**应该监听什么**”（需求层）。
- `m_armed_events` 回答“**现在实际上监听着什么**”（执行层）。

sylar IOManager 使用 `EPOLLONESHOT`，事件触发一次后就失效。也就是说：

- `m_watch_events` 往往**不变**（libcurl 仍然希望继续监听 READ/WRITE）。
- 但 `m_armed_events` 会在触发后**掉位**（因为 one-shot 已消费）。

因此每次事件触发后都必须手动 rearm：

```
epoll 触发 fd 可读
    │
    ▼
OnSocketEvent(fd, CURL_CSELECT_IN)
    │
    ├─ m_armed_events[fd] &= ~READ   ← 标记 READ 已被消费
    │
    └─ RearmSocketWatch(fd)
           │
           ├─ desired = m_watch_events[fd]   ← libcurl 期望的监听
           ├─ armed   = m_armed_events[fd]   ← 当前实际挂载的
           ├─ to_add  = desired & ~armed     ← 需要补挂的
           └─ iom->addEvent(fd, READ, ...)   ← 重新挂载
```

一个具体例子（最直观）：

```
初始：watch = READ|WRITE, armed = READ|WRITE

收到一次 READ 回调后：
  OnSocketEvent 先清位 → armed = WRITE
  watch 仍然是 READ|WRITE（需求没有变）

RearmSocketWatch 计算：
  desired = READ|WRITE
  armed   = WRITE
  to_add  = READ
  => 只补挂 READ

补挂成功后：
  armed 回到 READ|WRITE
```

这就是 5.7 的本质：**用 `m_watch_events` 作为“期望真值”，持续修复 `m_armed_events`，抵消 one-shot 的自动失效语义。**

### 5.8 乐观更新 m_armed_events（防并发重复 addEvent）

sylar 的 `addEvent` 对同一 fd 同一事件类型不允许重复注册（会触发断言）。并发下的危险窗口是：

为什么会出现“多个线程同时进入 `RearmSocketWatch(fd)`”：

- 同一个 fd 在一次 `epoll_wait` 返回中可能同时命中 READ 和 WRITE；
- IOManager 会把 READ/WRITE 作为两个独立任务 `schedule` 到调度器队列；
- 调度器是线程池模型（thread=-1，可由任意工作线程执行），因此两个任务可能被不同线程并发执行；
- 两个回调最终都会进入 `OnSocketEvent`，并调用 `RearmSocketWatch(fd)`。

1. 线程 A、B 几乎同时进入 `RearmSocketWatch(fd)`；
2. 两者都读取到 `armed` 还没有 READ；
3. 两者都算出 `to_add` 包含 READ；
4. 两者都调用 `addEvent(fd, READ)`，第二次注册触发断言/错误。

解决方案：**在锁内先更新 `m_armed_events`，再锁外调用 `addEvent`**：

```cpp
{
    lock_guard lock(m_mutex);
    to_add = desired & ~armed;
    m_armed_events[fd] = armed | to_add;  // ← 乐观写入，后来的线程看到"已挂载"
}
// 锁外调用，此时其他线程已看到更新后的 m_armed_events
iom->addEvent(fd, READ, callback);
```

这里的关键不是“骗人”，而是把 `m_armed_events` 从“纯实际态”提升为“**实际态 + in-flight 计划态**”：

- 某线程一旦决定 `to_add`，先把位写进 `m_armed_events`（占位）。
- 后续线程再进来会看到该位已存在，`to_add` 就变成 0，不会重复 add。
- 若 add 失败，再把占位位回滚，恢复一致性。

若 `addEvent` 失败，再回滚 `m_armed_events`：

```cpp
if (rt != 0) {
    lock_guard lock(m_mutex);
    m_armed_events[fd] &= ~READ;  // 回滚
}
```

并发时序对比：

```
无乐观更新：
  A: 读 armed=0, to_add=READ
  B: 读 armed=0, to_add=READ
  A/B 都 addEvent(READ)  -> 重复注册风险

有乐观更新：
  A(锁内): to_add=READ, 写 armed|=READ
  B(锁内): 读到 armed 已含 READ, to_add=0
  只有 A 会 addEvent(READ)
  若 A 失败，再回滚 armed 的 READ 位
```

总结：5.8 的目的不是改变监听需求（那是 `m_watch_events` 的职责），而是让 `m_armed_events` 在并发下稳定地扮演“去重闸门”，保证同一事件位只被 add 一次。

### 5.9 降级路径

构造函数中捕获当前上下文：

```cpp
FiberCurlSession::FiberCurlSession(CURL* easy)
    : m_iom(IOManager::GetThis())   // 无 IOManager → nullptr
{
    if (m_iom) {
        m_wait_fiber = Fiber::GetThis();  // 不在 fiber 中 → nullptr
        if (m_wait_fiber)
            m_wait_thread = m_wait_fiber->getBoundThread();
    }
}
```

`Perform()` 开头检查：

```cpp
if (!m_iom || !m_wait_fiber)
    return curl_easy_perform(m_easy);  // 降级为阻塞调用
```

这保证了在普通线程、单元测试等无协程环境下也能正常工作。

### 5.10 资源清理（Cleanup）

`Cleanup()` 设计为幂等，可在错误路径、正常结束路径和析构路径重复调用：

```
1. 取消并释放定时器（m_timer->cancel()）
2. 收集所有已注册 fd，清空 m_watch_events / m_armed_events / m_pending_actions
3. 逐个 delEvent(fd, READ) 和 delEvent(fd, WRITE)
4. curl_multi_remove_handle(m_multi, m_easy)
5. curl_multi_cleanup(m_multi)
```

---

## 6. 完整时序图

```
调用方 fiber                FiberCurlSession            sylar IOManager / epoll
     │                           │                              │
     │ FiberCurlSession(easy)    │                              │
     │──────────────────────────▶│ 捕获 m_iom, m_wait_fiber    │
     │                           │                              │
     │ Perform()                 │                              │
     │─────────────────────────▶│                              │
     │                           │ curl_multi_init()            │
     │                           │ curl_multi_add_handle()      │
     │                           │ socket_action(TIMEOUT)       │
     │                           │──── SocketCallback ────────▶│
     │                           │     addEvent(fd, READ)       │
     │                           │──── TimerCallback ─────────▶│
     │                           │     addTimer(100ms)          │
     │                           │                              │
     │                           │ WaitForSignal()              │
     │◀── YieldToHold() ────────│                              │
     │                           │                              │
     │  (其他 fiber 在此线程执行) │                              │
     │                           │                              │
     │                           │◀──── fd 可读，触发 callback ─│
     │                           │ OnSocketEvent(fd, IN)        │
     │                           │ push_back({fd, IN})          │
     │                           │ schedule(m_wait_fiber) ────▶│
     │                           │                              │
     │──── 恢复执行 ────────────▶│                              │
     │                           │ PopPendingAction()           │
     │                           │ socket_action(fd, IN)        │
     │                           │──── SocketCallback ────────▶│
     │                           │     addEvent(fd, READ)       │
     │                           │ DrainMessages() → DONE       │
     │                           │                              │
     │                           │ Cleanup()                    │
     │◀── return CURLE_OK ──────│                              │
```

---

## 7. 总结

`FiberCurlSession` 通过以下三层桥接，将阻塞的 curl 请求转化为协程友好的异步操作：

1. **curl_multi 层**：将 libcurl 内部的 IO 等待暴露为 socket/timer 回调，不再自己阻塞
2. **IOManager 层**：将 curl 需要监听的 fd 和定时器注册到 epoll 事件循环，由内核负责等待
3. **Fiber 层**：在无事件可处理时 `YieldToHold()` 挂起，事件就绪后通过 `schedule()` 恢复，工作线程在等待期间可执行其他 fiber

核心难点在于处理 sylar one-shot 事件语义（`RearmSocketWatch`）和防止并发回调重复 `addEvent`（乐观更新 `m_armed_events`），这两点是该实现区别于简单 curl_multi 封装的关键所在。

---

## 8. 使用示例

下面给出一个最小示例：在 `IOManager` 中调度一个 fiber，使用 `FiberCurlSession` 发起 HTTP GET。

```cpp
#include "ai/llm/fiber_curl_session.h"
#include "sylar/fiber/iomanager.h"

#include <curl/curl.h>
#include <iostream>
#include <string>

static size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t n = size * nmemb;
    std::string* out = static_cast<std::string*>(userdata);
    out->append(ptr, n);
    return n;
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    sylar::IOManager iom(1, true, "curl_demo");
    iom.schedule([]() {
        CURL* easy = curl_easy_init();
        if (!easy) {
            std::cerr << "curl_easy_init failed\n";
            return;
        }

        std::string response_body;
        curl_easy_setopt(easy, CURLOPT_URL, "https://httpbin.org/get");
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 5000L);
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &WriteCallback);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, &response_body);

        ai::llm::FiberCurlSession session(easy);
        CURLcode rc = session.Perform();

        long http_code = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);
        std::cout << "curl_rc=" << rc
                  << " http_code=" << http_code
                  << " body_size=" << response_body.size() << std::endl;

        curl_easy_cleanup(easy);
    });

    curl_global_cleanup();
    return 0;
}
```

说明：

- 示例中 `session.Perform()` 在 fiber 环境下会走协程化路径（内部通过 `YieldToHold` 挂起等待 IO）。
- 如果不在 IOManager/fiber 上下文中调用，`Perform()` 会自动降级为 `curl_easy_perform` 阻塞调用（见 5.9）。
- `FiberCurlSession` 不拥有 `easy` 生命周期，仍需调用方执行 `curl_easy_cleanup`。
