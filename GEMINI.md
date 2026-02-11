# Sylar Project Context

## Project Overview
**Sylar** is a high-performance C++ server framework designed from scratch. It employs a modular architecture to provide a robust foundation for building scalable network applications. The core philosophy is "asynchronous IO with synchronous programming" achieved through a custom coroutine (fiber) scheduling system and system call hooking.

### Key Technologies
- **Language:** C++11
- **Build System:** CMake (3.0+)
- **Concurrency:** POSIX Threads (pthread), custom Fiber (ucontext-based)
- **IO Multiplexing:** epoll (Linux)
- **Serialization/Config:** YAML (via yaml-cpp)
- **Networking:** Socket API with non-blocking IO hooks

## Module Overview
The project is organized into several key modules:

- **`sylar/log`**: A powerful logging system supporting multiple levels, custom formatting, and various output destinations (console, files).
- **`sylar/base`**: Fundamental utilities including a YAML-based configuration system (`config.h`), singleton patterns, and system utilities (stack trace, thread IDs).
- **`sylar/concurrency`**: Threading wrappers and synchronization primitives like semaphores, mutexes (including read-write and spinlocks), and RAII-style lock guards.
- **`sylar/fiber`**: The heart of the framework, providing N:M coroutine scheduling. It includes the `Fiber` class, a `Scheduler`, and a `Timer` for time-based tasks.
- **`sylar/net`**: Handles file descriptor management and system call hooking to transform blocking operations into non-blocking fiber-aware calls.
- **`sylar/http`**: (In development) HTTP protocol parsing and server implementation.

## Building and Running

### Prerequisites
- GCC 13.3.0 or compatible C++11 compiler
- CMake 3.28.3 or higher
- Libraries: `yaml-cpp`, `pthread`, `dl`

### Build Commands
```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

### Running Tests
After building, test executables are located in the `bin/` directory.
```bash
# Example: Run the fiber test
./bin/test_fiber

# Example: Run the scheduler test
./bin/test_scheduler
```

## Development Conventions

### Memory Management
- Extensively uses `std::shared_ptr`. Most classes define a `ptr` typedef:
  ```cpp
  typedef std::shared_ptr<ClassName> ptr;
  ```
- Use `std::enable_shared_from_this` for classes that need to pass `shared_ptr` to themselves.

### Resource Protection
- Resource-managing classes should inherit from `sylar::Noncopyable` to prevent accidental copies.
- Use RAII-style locks (`sylar::Mutex::Lock`, `sylar::RWMutex::ReadLock`, etc.) for thread safety.

### Coding Style
- **Naming:** `PascalCase` for classes, `camelCase` or `snake_case` for methods (matches surrounding code), `m_` prefix for member variables.
- **Header Guards:** Uses `#ifndef ... #define ... #endif`.
- **Namespaces:** All core logic resides within the `sylar` namespace.

### Contribution Workflow
1. Implement features in the relevant module directory (`sylar/module_name`).
2. Add a corresponding test file in `tests/test_module_name.cc`.
3. Update `CMakeLists.txt` to include new source files and create a test executable.
4. Verify changes by running the test and checking logs.

## Current Progress
Currently, the core modules (Log, Config, Threads, Mutexes, Fibers, Scheduler) are implemented. The project is focusing on finalizing the **IOManager** and **Timer** integration to support full asynchronous networking.
