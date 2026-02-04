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
1. **挑战**: `SYLAR_LOG_INFO` 必须能返回一个 `stringstream` 供用户输入，但又必须在用户输入完成后（即这一行结束时）自动触发 `logger->log()`。
2. **解决**: 
   - 宏展开后，会创建一个**临时匿名对象** `LogEventWrap`。
   - 用户通过 `<<` 操作符往该临时对象持有的 `LogEvent` 中写入内容。
   - **关键**: 根据 C++ 标准，临时对象的生命周期直到这一行（语句）结束才终止。
   - **析构触发**: 当这一行执行完，`LogEventWrap` 临时对象被销毁，其析构函数被调用。我们在析构函数中写下 `m_event->getLogger()->log(m_event)`。
3. **结果**: 实现了“写完即触发”的丝滑体验，且用户无需手动调用任何发送函数。

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
3. 在 `LogFormatter.cc` 的 `s_format_items` 映射表中添加 `XX(N, ThreadNameFormatItem)` 注册项。

---
*(持续更新中...)*