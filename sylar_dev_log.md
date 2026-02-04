# Sylar 从零开发笔记

## 项目初始化 (2026-02-03)
- 编译器: GCC 13.3.0
- 构建工具: CMake 3.28.3
- 目录结构: 采用模块化设计 (base, concurrency, fiber, net, http, log)。

---

## 日志模块 (Log Module)

### 1. LogLevel (日志级别)

#### 设计理念
定义日志的严重程度，用于后续的过滤控制（如只打印 ERROR 级别以上的日志）。

#### 设计要点
- 采用 `DEBUG < INFO < WARN < ERROR < FATAL` 的递增数值逻辑。

#### 技巧
- 使用宏 (Macro) `XX(name)` 配合 `#name` 字符串化，极大地减少了枚举与字符串相互转换的代码量。

#### 遇到的问题
##### Q: 为什么不使用 `enum class`?
**A**: 虽然 `enum class` (强类型枚举) 更安全，但在日志系统中，我们经常需要把日志级别和整数阈值进行比较（例如 `if (current_level >= limit_level)`）。如果使用 `enum class`，每次比较都需要显式强转 `static_cast<int>(level)`，代码会显得非常啰嗦。而普通的 `enum` 支持隐式转换为整型，且我们将 `enum` 包裹在 `class LogLevel` 内部，已经起到了良好的作用域隔离效果。

---

### 2. LogEvent (日志现场)

#### 设计理念
“案发现场的档案袋”。在日志发生的那一瞬间，抓取并封装所有相关信息（文件名、行号、时间戳、线程/协程ID、日志内容）。

#### 设计要点
- 使用 `std::stringstream` 提供流式输入支持，使得日志打印像 `std::cout` 一样方便。
- 统一使用 `typedef std::shared_ptr<LogEvent> ptr;` 进行内存管理。

#### 遇到的问题
##### Q: 为什么 `LogEvent` 要持有 `Logger` 的指针？
**A**: 为了实现上下文传递。
1.  **格式化需求**: 某些格式化项（如 `%c`）需要打印 Logger 的名称，`LogEvent` 必须知道自己是由谁产生的才能提供这个信息。
2.  **高级路由**: 在复杂的日志系统中，可能存在全局的拦截器或钩子。持有 Logger 指针可以让拦截器知道日志的来源模块（是 "system" 还是 "business"），从而进行分流处理（例如将核心模块的日志额外发送到监控告警系统）。

---

### 3. LogFormatter (格式化器)

#### 设计理念
负责将结构化的 `LogEvent` 转换为人类可读的字符串。支持用户自定义格式（Pattern）。

#### 设计要点
- **组合模式**: 将复杂的 pattern (如 `%d %m %n`) 拆解为多个独立的 `FormatItem` 子类。
- **多态应用**: `LogFormatter` 持有 `vector<FormatItem::ptr>`，遍历调用虚函数 `format`，实现高度可定制的日志格式。

#### 遇到的问题
##### Q: 如何解决 LogFormatter 和 Logger 的循环引用？
**A**: 这是一个经典的依赖死锁问题：`LogFormatter` 里的 `%c` 需要调用 `Logger` 的方法，而 `Logger` 又包含 `LogFormatter` 成员。
**解决方法**: 使用**前置声明 (Forward Declaration)**。
在 `.h` 头文件中，我们只声明 `class Logger;`，这告诉编译器“有一个叫 Logger 的类”，但不涉及它具体的内存布局或方法。这允许我们在头文件中定义 `shared_ptr<Logger>`。具体的 `#include "logger.h"` 延迟到 `.cc` 实现文件中才进行，从而打破编译时的头文件包含循环。

##### Q: 为什么父类析构函数必须是虚函数 (`virtual`)？
**A**: 这是一个 C++ 内存管理的铁律。
当我们在容器中存储基类指针（`FormatItem::ptr`），但实际指向子类对象（`MessageFormatItem`）时，如果我们销毁这个容器，会调用 `delete` 基类指针。
- 如果析构函数**不是**虚函数：编译器只会静态绑定调用基类 `~FormatItem()`，子类的析构函数完全不会执行，子类特有的资源（如果有）就会泄漏。
- 如果析构函数**是**虚函数：编译器会动态绑定，先调用子类析构，再调用基类析构，确保对象被完整清理。

##### Q: `auto& i : m_items` 中的 `&` 作用？
**A**: 这里的 `&` 表示引用。`m_items` 存储的是 `shared_ptr`。
- 如果不加 `&`：每次循环都会触发 `shared_ptr` 的拷贝构造函数，导致原子引用计数频繁 `+1` 和 `-1`，这虽然是线程安全的，但有微小的性能开销。
- 加上 `&`：`i` 只是该元素的别名，完全没有拷贝开销。

##### Q: `init` 解析逻辑是怎么工作的？
**A**: 本质是一个词法分析状态机。
它遍历模式串，维护一个状态位 `fmt_status`：
- **状态 0 (普通模式)**: 逐个字符读取。如果遇到 `%`，进入解析模式；否则当作普通字符串。
- **状态 1 (格式模式)**: 识别 `%d` 这样的指令。如果遇到 `{`，说明有参数（如日期格式），进入参数提取阶段；如果遇到非字母，说明指令结束，创建对应的 Item。

---

### 4. LogAppender (输出地)

#### 设计理念
决定日志的去向（控制台、文件、网络等）。

#### 设计要点
- **线程安全**: 核心成员变量 `std::mutex m_mutex`。
- **独立锁**: 每个 Appender 实例拥有独立的锁，不同文件输出可并行，同一文件输出需排队。

#### 遇到的问题
##### Q: 锁 (Mutex) 到底锁住了什么？
**A**: 锁住的是 **“临界区 (Critical Section)”** 的访问权。
在我们的代码中，临界区就是“往流里写数据”的那几行代码。当一个线程获得了锁，其他线程就必须挂起等待。这保证了**操作的原子性**：即一条日志的输出过程（时间+级别+内容+换行）是连续的，绝对不会被其他线程的日志内容“插队”打断，防止输出乱码。

##### Q: 两个 Logger 同时输出到控制台会乱吗？
**A**: 会。
因为 `std::cout` 是全局唯一的资源。如果 Logger A 和 Logger B 各自拥有一个 `StdoutLogAppender` 实例，它们就会各自拥有一把独立的锁。线程 A 拿着 Lock A 写 `cout`，线程 B 拿着 Lock B 写 `cout` —— 两把锁互不干扰，导致两个线程同时往 `cout` 写东西，输出内容就会交错。
**解决思路**: 让所有 Logger 共享**同一个** `StdoutLogAppender` 实例（单例模式）。这样大家抢的都是同一把锁，就能完美排队了。

---

### 5. Logger (日志器)

#### 设计理念
日志系统的入口和组装者。它负责将 Appender、Formatter、Level 组合在一起，对外提供统一的接口。

#### 设计要点
- **继承 `enable_shared_from_this`**: 允许在成员函数中安全地获取自身的 `shared_ptr`。
- **默认兜底机制**: 包含 `m_root` (根日志器)。如果当前 Logger 没有配置 Appender，可以委托给 Root Logger 输出，防止日志丢失。
- **单例 Manager**: 使用 `LoggerManager` 全局管理所有 Logger，确保相同名称的 Logger 在系统中是同一个实例。

#### 遇到的问题
##### Q: 为什么要继承 `std::enable_shared_from_this`？
**A**: 为了解决 **Double Free (双重释放)** 问题。
在 `Logger::log` 方法中，我们需要创建一个 `LogEvent`，而 `LogEvent` 的构造函数需要一个 `Logger::ptr` (即 `shared_ptr<Logger>`) 来记录是谁产生了日志。
- **错误做法**: `new LogEvent(std::shared_ptr<Logger>(this), ...)`。这会创建一个**全新的引用计数控制块**。当外部持有的 `logger` 指针析构时会 delete 一次，这个临时的 `shared_ptr` 析构时又会 delete 一次，导致程序崩溃。
- **正确做法**: `new LogEvent(shared_from_this(), ...)`。`enable_shared_from_this` 内部维护了一个弱引用 (`weak_ptr`)，指向最初创建该对象的 `shared_ptr` 控制块。调用 `shared_from_this()` 会安全地从这个弱引用提升出一个新的 `shared_ptr`，共享同一个引用计数。
**前提**: 对象必须是通过 `std::make_shared` 或 `std::shared_ptr` 创建的，否则调用该方法会抛异常。

##### Q: Logger 里的互斥锁保护的是什么？
**A**: 保护的是 **Logger 自身的结构配置（成员变量）**。
- **主要保护对象**: `m_appenders` 列表。如果在线程 A 遍历列表写日志时，线程 B 调用了 `addAppender` 修改了列表，会导致迭代器失效，引发程序崩溃。
- **保护范围**: 凡是涉及修改 Logger 私有成员（如 `m_level`, `m_formatter`, `m_appenders`）的操作，以及需要读取这些成员并进行后续逻辑的操作（如 `log` 过程中的遍历），都必须加锁。
- **区分**: `Appender` 里的锁是保护“输出过程”不被打断；而 `Logger` 里的锁是保护“配置结构”不被破坏。

##### Q: `m_root.reset(new Logger)` 中的 `reset` 是什么意思？
**A**: `reset` 是 `std::shared_ptr` 的一个成员方法。
- **作用**: 重新设置智能指针所指向的对象。它会先让旧对象的引用计数 `-1`（若减至 0 则释放旧对象），然后接管新 `new` 出来的对象的生命周期。
- **原因**: 智能指针出于安全考虑，禁止了从裸指针到智能指针的隐式转换。因此不能直接写 `m_root = new Logger`，必须使用 `reset()` 方法或 `m_root = std::shared_ptr<Logger>(new Logger)`。
- **实践**: 在 `LoggerManager` 的构造函数中使用 `reset` 来初始化全局根日志器。

##### Q: 为什么要设计 `LogEventWrap`？如何实现流式宏打印？
**A**: 为了实现 `SYLAR_LOG_INFO(logger) << "msg"` 这种极其简洁的语法，我们利用了 **RAII (资源获取即初始化)** 和 **临时对象析构** 的特性。

**宏展开解析**：
```cpp
// 原始宏
#define SYLAR_LOG_LEVEL(logger, level) \
    if(logger->getLevel() <= level) \
        sylar::LogEventWrap(sylar::LogEvent::ptr(new sylar::LogEvent(...))).getSS()

// 调用代码
SYLAR_LOG_INFO(logger) << "hello";

// 展开后的实际执行流程
if(logger->getLevel() <= level) { // 1. 级别判断
    sylar::LogEvent::ptr event(new sylar::LogEvent(...)); // 2. 创建事件
    sylar::LogEventWrap wrap(event); // 3. 创建临时包装对象
    wrap.getSS() << "hello"; // 4. 往流里写数据
} // 5. 语句结束，wrap 对象析构
```

**执行步骤详解**：
1.  **级别判断 (Check)**: 先判断级别，如果不满足直接跳过，避免了后续所有开销（性能关键）。
2.  **创建包装 (Construct)**: 创建一个临时的 `LogEventWrap` 对象。
3.  **流式写入 (Stream)**: `getSS()` 返回 `stringstream`，用户通过 `<<` 写入内容。
4.  **自动提交 (Destruct & Submit)**: 当这一行代码执行完毕，临时的 `wrap` 对象离开作用域被销毁。**在其析构函数中**，会自动调用 `logger->log(event)` 将日志真正提交。

**结果**: 实现了“写完即触发”的丝滑体验，且用户无需手动调用任何发送函数。

##### Q: 宏 `SYLAR_LOG_ROOT()` 是如何工作的？
**A**: 它是一个便捷宏，底层封装了对 `LoggerManager` 单例的调用。
- 展开前: `SYLAR_LOG_ROOT()`
- 展开后: `sylar::LoggerMgr::GetInstance()->getRoot()`
这隐藏了复杂的单例获取逻辑，让用户感觉像是直接获取了一个全局变量。同理，`SYLAR_LOG_NAME(name)` 封装了 `getLogger(name)`。

---

## 基础模块 (Base Module)

### 1. Util (通用工具类)

#### 设计理念
提供系统底层的封装，主要用于获取运行时信息（线程ID、堆栈信息等），为其他模块（如日志、协程、配置）提供基础支撑。

#### 设计要点
- **GetThreadId**: 封装 `syscall(SYS_gettid)`。
- **Backtrace**: 利用 `<execinfo.h>` 库提供的 `backtrace` 和 `backtrace_symbols` 函数获取函数调用栈。
- **Demangle**: 编译器在生成符号时会进行名字修饰（Name Mangling），使用 `abi::__cxa_demangle` 将丑陋的机器符号转换回人类可读的 C++ 函数名。

#### 技巧
- **符号提取**: `backtrace_symbols` 返回的字符串包含 `文件名(函数名+偏移量) [地址]`。通过字符串解析提取出 `函数名` 部分再进行 demangle，可以获得最清晰的堆栈输出。

#### 遇到的问题
##### Q: 为什么 `GetThreadId` 不使用 `pthread_self()`?
**A**: `pthread_self()` 返回的是 POSIX 线程库内部维护的线程 ID（通常是一个很大的内存地址），它在进程内唯一，但在不同进程间不直观。而 `SYS_gettid` 返回的是 Linux 内核分配的真实线程 ID（PID），在 `top` 或 `ps` 命令中能直接看到，更利于调试。

##### Q: 为什么日志输出中会出现 `<<error_format %N>>`?
**A**: 这是因为在 `Logger` 的默认格式模板中使用了 `%N`（线程名称），但在 `LogFormatter` 的映射表中漏掉了该指令的注册。
**解决**: 
1. 在 `LogEvent` 中增加 `m_threadName` 成员。
2. 实现 `ThreadNameFormatItem` 类。
---

### 2. Config (配置模块)

#### 设计理念
实现“约定优于配置”的设计思想。支持类型安全、复杂容器支持以及配置变更的回调通知（热更新）。

#### 设计要点
- **ConfigVarBase**: 非模板基类，用于统一管理不同类型的配置项（存入同一个 Map）。
- **ConfigVar<T>**: 模板子类，负责存储实际值并处理序列化/反序列化。
- **LexicalCast**: 类型转换中心。通过**模板偏特化**支持基础类型与 YAML 字符串之间的自动转换。
- **变更回调**: 每个 `ConfigVar` 持有一个回调函数列表，当 `setValue` 被调用时触发，实现热加载。

#### 技巧
- **静态初始化保护**: 使用 `GetDatas()` 函数内的静态变量来存储配置 Map，避免了 C++ 全局静态变量初始化顺序不确定的坑 (Static Initialization Order Fiasco)。

#### 遇到的问题
##### Q: 为什么 `LexicalCast` 需要针对 STL 容器进行大量特化？
**A**: 因为 `boost::lexical_cast` 默认只支持基础类型（如 int 转 string）。为了让配置系统支持 `vector<int>`, `map<string, int>` 等复杂结构，我们必须利用**模板偏特化**技术。
- **序列化流程**: 容器 (如 `vector<T>`) -> 遍历并转换每个元素为 YAML 节点 -> 将整个 YAML 节点转为字符串。
- **反序列化流程**: 字符串 -> `YAML::Load` 解析为节点 -> 遍历 YAML 节点并利用 `LexicalCast<string, T>` 转换每个元素 -> 塞回容器。
通过这种递归调用（`LexicalCast` 内部调用 `LexicalCast`），我们甚至可以支持嵌套容器，如 `vector<list<int>>`。

##### Q: 在 Lookup 查找配置时，为什么要用 `dynamic_pointer_cast`?
**A**: 全局 Map 存储的是父类 `ConfigVarBase::ptr`。获取配置时，必须确认其实际类型与用户请求的类型 `T` 一致。`dynamic_pointer_cast` 提供了运行时的类型安全检查。

##### Q: 如何实现从复杂的 YAML 文件批量加载配置？
**A**: 采用了 **“树形结构扁平化”** 的策略。
1. **递归遍历**: 实现辅助函数 `ListAllMember`，递归遍历 YAML 树。
2. **路径拼接**: 将嵌套的结构转换为点分隔的路径字符串（如 `system -> port` 转换为 `system.port`）。
3. **节点存储**: 将所有叶子节点及其对应的完整路径存入一个 `std::list` 中。
4. **统一更新**: 遍历该列表，在 `ConfigVarMap` 中按路径名查找配置项。若匹配成功，则调用 `fromString` 利用之前特化的 `LexicalCast` 进行自动类型转换并赋值。

##### Q: 配置变更回调（Listener）的 ID 为什么用 `static` 变量？
**A**: 为了保证每个监听器在注册时都能获得一个唯一的编号（Key），以便后续可以精准地删除某个特定的监听器（`delListener`）。使用静态变量可以实现自增 ID 的分配。

##### Q: 为什么设置了回调就能实现“热加载”？
**A**: 回调函数本身只是热加载流程的**最后一环**。完整的热加载机制如下：
1.  **文件监控 (File Watcher)**: 系统后台有一个线程或定时任务（利用 `inotify` 或 `stat`）监控配置文件（如 `conf.yml`）的修改时间。
2.  **触发重载 (Trigger Reload)**: 一旦检测到文件变化，程序自动读取新文件内容。
3.  **解析更新 (Parse & Update)**: 调用 `Config::LoadFromYaml`，将新配置解析为内存对象，并调用对应 `ConfigVar` 的 `setValue()` 方法。
4.  **变更通知 (Notification)**: `setValue()` 内部检测到值发生变化，遍历 `m_cbs` 列表，依次执行注册的回调函数。
5.  **业务响应 (Reaction)**: 回调函数执行具体的业务逻辑（如重置数据库连接池、重启 Listener），从而完成“热更新”。
目前我们实现了 3, 4, 5 步，第 1, 2 步将在后续的文件监控模块中实现。

---
*(持续更新中...)*