# Sylar 底层同步包（可直接复制粘贴）

下面是“当前分支最新实现”的文件清单与一键同步命令。  
你不需要手抄代码，直接复制命令执行即可把这批实现带到其他分支。

## 1) 要同步的文件（Sylar 底层）

```text
src/sylar/fiber/fd_manager.h
src/sylar/fiber/fd_manager.cc
src/sylar/fiber/hook.cc
src/sylar/fiber/iomanager.h
src/sylar/fiber/iomanager.cc
src/sylar/fiber/scheduler.h
src/sylar/fiber/scheduler.cc
src/sylar/net/tcp_server.h
src/sylar/net/tcp_server.cc
```

如果你也要带上本次 TLS 回滚后的实现，再额外加：

```text
src/http/ssl/ssl_socket.cc
```

## 2) 方案A（推荐）：先导出快照，再切分支覆盖

在当前分支（`assembly-context`）执行：

```bash
set -euo pipefail
SNAPSHOT_DIR=/tmp/sylar_core_snapshot_20260325
mkdir -p "$SNAPSHOT_DIR/src/sylar/fiber" "$SNAPSHOT_DIR/src/sylar/net" "$SNAPSHOT_DIR/src/http/ssl"

cp src/sylar/fiber/fd_manager.h "$SNAPSHOT_DIR/src/sylar/fiber/"
cp src/sylar/fiber/fd_manager.cc "$SNAPSHOT_DIR/src/sylar/fiber/"
cp src/sylar/fiber/hook.cc "$SNAPSHOT_DIR/src/sylar/fiber/"
cp src/sylar/fiber/iomanager.h "$SNAPSHOT_DIR/src/sylar/fiber/"
cp src/sylar/fiber/iomanager.cc "$SNAPSHOT_DIR/src/sylar/fiber/"
cp src/sylar/fiber/scheduler.h "$SNAPSHOT_DIR/src/sylar/fiber/"
cp src/sylar/fiber/scheduler.cc "$SNAPSHOT_DIR/src/sylar/fiber/"
cp src/sylar/net/tcp_server.h "$SNAPSHOT_DIR/src/sylar/net/"
cp src/sylar/net/tcp_server.cc "$SNAPSHOT_DIR/src/sylar/net/"
# 可选：TLS
cp src/http/ssl/ssl_socket.cc "$SNAPSHOT_DIR/src/http/ssl/"

echo "snapshot ready: $SNAPSHOT_DIR"
```

切到目标分支后执行：

```bash
set -euo pipefail
SNAPSHOT_DIR=/tmp/sylar_core_snapshot_20260325

cp "$SNAPSHOT_DIR/src/sylar/fiber/fd_manager.h" src/sylar/fiber/
cp "$SNAPSHOT_DIR/src/sylar/fiber/fd_manager.cc" src/sylar/fiber/
cp "$SNAPSHOT_DIR/src/sylar/fiber/hook.cc" src/sylar/fiber/
cp "$SNAPSHOT_DIR/src/sylar/fiber/iomanager.h" src/sylar/fiber/
cp "$SNAPSHOT_DIR/src/sylar/fiber/iomanager.cc" src/sylar/fiber/
cp "$SNAPSHOT_DIR/src/sylar/fiber/scheduler.h" src/sylar/fiber/
cp "$SNAPSHOT_DIR/src/sylar/fiber/scheduler.cc" src/sylar/fiber/
cp "$SNAPSHOT_DIR/src/sylar/net/tcp_server.h" src/sylar/net/
cp "$SNAPSHOT_DIR/src/sylar/net/tcp_server.cc" src/sylar/net/
# 可选：TLS
cp "$SNAPSHOT_DIR/src/http/ssl/ssl_socket.cc" src/http/ssl/

git status --short
```

## 3) 方案B：导出 patch 再在目标分支 apply

在当前分支执行：

```bash
git diff -- \
  src/sylar/fiber/fd_manager.h \
  src/sylar/fiber/fd_manager.cc \
  src/sylar/fiber/hook.cc \
  src/sylar/fiber/iomanager.h \
  src/sylar/fiber/iomanager.cc \
  src/sylar/fiber/scheduler.h \
  src/sylar/fiber/scheduler.cc \
  src/sylar/net/tcp_server.h \
  src/sylar/net/tcp_server.cc \
  src/http/ssl/ssl_socket.cc \
  > /tmp/sylar_core_sync.patch
```

在目标分支执行：

```bash
git apply --reject --whitespace=fix /tmp/sylar_core_sync.patch
git status --short
```

## 4) 同步后最小验证

```bash
cmake --build build -j8
./build/bin/test_iomanager
./build/bin/test_fd_manager
```
