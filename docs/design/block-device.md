# blk — 块设备抽象层

> **实现状态**: 已实现（KernelDevice + libaio，6 test files）

## 概述

`blk` 模块为 Linux 上的直接 I/O 提供可移植的块设备抽象。它封装了原始块设备（如 NVMe、SSD），基于 `libaio` 实现了同步和异步读写接口。

## 1. 需求分析

### 1.1 背景

BlueStore 和 BTier 都需要绕过内核 page cache，直接对裸块设备执行对齐 I/O。`blk` 模块提供统一的块设备抽象层，向上暴露同步/异步读写接口，向下封装 `libaio` 和 Linux `ioctl` 设备探测。

### 1.2 约束条件

| 维度 | 说明 |
| --- | --- |
| 操作系统 | 仅 Linux（x86\_64 + AArch64） |
| I/O 模式 | O\_DIRECT（默认）+ O\_RDWR（缓冲回退） |
| 异步引擎 | libaio（`io_setup` / `io_submit` / `io_getevents`） |
| 对齐要求 | Direct I/O：offset、length、buffer 均须与 `block_size`（通常 4KB）对齐 |
| 缓冲 I/O | 内核 page cache 处理对齐，跳过 `is_valid_io` 检查 |

### 1.3 核心功能

| 功能 | 说明 |
| --- | --- |
| 同步读 | `read(off, len, &bl)` / `read_random(off, len, buf)` |
| 同步写 | `write(off, bl)` — 支持 `pwritev` scatter/gather |
| 异步读 | `aio_read(off, len, &bl, ioc)` — 通过 libaio 提交 |
| 异步写 | `aio_write(off, bl, ioc)` — 通过 libaio 提交 |
| 批量提交 | `aio_submit(ioc)` — 将 pending 队列提交到内核 |
| 刷盘 | `flush()` — `fdatasync`，无写时为空操作 |
| 丢弃 | `discard(off, len)` — `ioctl(BLKDISCARD)` |
| 缓存失效 | `invalidate_cache(off, len)` — `posix_fadvise(DONTNEED)` |

## 2. 架构

```plaintext
┌───────────────────────────────────────────────────────────────┐
│                      BlockDevice (abstract base)              │
│  Sync: read / write / flush / read_random                     │
│  Async: aio_read / aio_write / aio_submit                     │
│  Mgmt: discard / invalidate_cache / collect_metadata          │
└──────────────┬────────────────────────────────────────────────┘
               │ inherit
┌──────────────▼────────────────────────────────────────────────┐
│                      KernelDevice                             │
│  - fd_direct_ (O_DIRECT | O_RDWR)                             │
│  - fd_buffered_ (O_RDWR)                                      │
│  - io_queue_ (aio_queue_t)                                    │
│  - aio_thread_ (completion reaping loop)                      │
└──────────────┬────────────────────────────────────────────────┘
               │ owns/uses
┌──────────────▼───────────┐   ┌────────────────────────────────────┐
│      IOContext           │   │     io_queue_t (abstract)          │
│  pending_aios / running  │   │  submit_batch / get_next_completed │
│  num_pending / num_run   │   │           │                        │
│  aio_wait / try_aio_wake │   │  ┌────────▼────────┐               │
└──────────────┬───────────┘   │  │  aio_queue_t    │               │
               │ include       │  │    (libaio)     │               │
┌──────────────▼───────────┐   │  └─────────────────┘               │
│         aio_t            │   └────────────────────────────────────┘
│  Wrap struct iocb        │
│  iov / bl / fd / priv    │
│  pwritev / preadv        │
│  boost::intrusive hook   │
└──────────────────────────┘
```

## 组件详情

### BlockDevice (`block_device.h` / `block_device.cc`)

抽象基类，提供：

- **属性**: `size`、`block_size`、`optimal_io_size`、`rotational`、`support_discard`
- **校验**: `is_valid_io(off, len)` — 检查与 `block_size` 的对齐及范围是否在 `size` 内
- **工厂**: `BlockDevice::create(path, cb, priv)` 在当前平台始终返回 `KernelDevice`
- **回调**: `aio_callback_t` 在回调模式下，一批 IO 完成时被调用

### KernelDevice (`kernel_device.h` / `kernel_device.cc`)

具体实现，将块设备路径**打开两次**：

| 描述符 | 标志 | 用途 |
| --- | --- | --- |
| `fd_direct_` | `O_RDWR \| O_DIRECT \| O_CLOEXEC` | 直接 I/O（无缓冲） |
| `fd_buffered_` | `O_RDWR \| O_CLOEXEC` | 带缓冲 I/O 回退 |

打开时通过 `ioctl`（BLKSSZGET、BLKIOOPT、BLKROTATIONAL、BLKDISCARD）探测设备几何参数，并使用自适应 iodepth（`max(16, min(128, size/blocksize/4))`）初始化 `aio_queue_t`。

**fd 双开策略**:

- `fd_direct_` 用于所有 Direct I/O 路径（`aio_read` / `aio_write` / 同步 `read` / `write`），要求 offset/length/buffer 与 `block_size` 对齐
- `fd_buffered_` 用于缓冲 I/O 回退（`buffered = true` 时），内核 page cache 自动处理对齐
- `read` / `write` / `aio_write` 根据 `buffered` 参数选择 fd；`aio_read` 始终使用 `fd_direct_`
- 缓冲 I/O 路径跳过 `is_valid_io()` 对齐检查（kernel page cache 处理 misalignment）

**对齐约束**:

```cpp
bool is_valid_io(uint64_t off, uint64_t len) const {
    return (off % block_size == 0) && (len % block_size == 0)
        && (off + len <= size);
}
```

- Direct I/O (`buffered = false`)：`is_valid_io` 检查 offset 和 length 与 `block_size` 对齐，且不越界
- 缓冲 I/O (`buffered = true`)：跳过 `is_valid_io`，内核 page cache 处理 misalignment
- Direct I/O 写入前调用 `bl.rebuild_aligned(block_size)` 确保 buffer 对齐
- Direct I/O 读取使用 `buffer::create_aligned(len, block_size)` 分配对齐缓冲

**内部回退标志：**

- `dio_`（默认 `true`）：设为 `false` 时回退到带缓冲 I/O
- `aio_`（默认 `true`）：设为 `false` 时回退到同步 `pread`/`pwritev`（而非 libaio）
- `write_hint`：`WRITE_LIFE_*` 枚举已在 `write()` / `aio_write()` 中接受，但目前**未转发**到内核（`fcntl(F_SET_RW_HINT)`）；此为一个已知缺陷

**flush 优化**:

`flush()` 通过 `fdatasync(fd_direct_)` 实现。使用 `io_since_flush_` 原子标志跟踪是否有写操作发生——若无写操作则 `flush()` 为空操作（通过 `compare_exchange_strong` 原子检测并跳过），避免不必要的 `fdatasync` 系统调用。

**AIO 完成线程**（`_aio_thread`）：

- 后台线程每 50ms 轮询 `io_queue_->get_next_completed(50, ...)`，单次最多收割 256（`kMaxReap`）个完成事件
- 对每个完成的 `aio_t`，检查返回值：
  - `res < 0` → 设置 `ioc->set_return_value(-EIO)`
  - `res != length` → 设置 `ioc->set_return_value(-EIO)`（部分写入视为错误）
  - `res == length` → 正常完成
- 确定完成模式：
  - **回调模式**: 如果 `ioc->priv` 和 `aio_callback` 均已设置，则最后一个正在运行的 IO 完成时（`num_running` 递减至 0）调用 `aio_callback(aio_callback_priv, ioc->priv)`。
  - **等待模式**: 调用 `ioc->try_aio_wake()` 减少正在运行计数，当计数归零时通知条件变量。

**同步 I/O** 回退到 `pread` / `pwritev`。带缓冲写入会跟踪 `io_since_flush_`，使得 `flush()`（通过 `fdatasync(2)`）在无写入发生时为空操作。

### IOContext (`io_context.h` / `io_context.cc`)

跟踪每个 IO 上下文中正在进行的操作：

| 状态 | 链表 | 计数器 |
| --- | --- | --- |
| 尚未提交 | `pending_aios` | `num_pending` |
| 已提交/正在进行 | `running_aios` | `num_running` |

- `aio_wait()` — 阻塞调用者（通过 `condition_variable`），直到 `num_running == 0`
- `try_aio_wake()` — 线程安全地减少 `num_running`；归零时通知
- `release_running_aios()` — 清空 `running_aios`（调用者必须保证无 IO 正在进行）

### aio_t (`aio.h` / `aio.cc`)

封装单个 `struct iocb`（libaio）及其关联元数据：

- `io_prep_pwritev` / `io_prep_preadv` 构建 iocb
- `iov`: 一个 `boost::container::small_vector<struct iovec, 4>` 用于 scatter/gather
- `bl`: 持有 `bufferlist` 引用，确保异步写入期间数据存活
- 侵入式链表钩子（`boost::intrusive::list_member_hook<>`）支持 `aio_list_t` — 一种用于零分配批量跟踪的侵入式链表
- `rval` 存储完成结果，由收割循环通过 `reinterpret_cast<aio_t*>(event.obj)` 设置

### io_queue_t / aio_queue_t (`aio.h` / `aio.cc`)

抽象接口：

- `init(fds)` — 设置提交队列
- `shutdown()` — 销毁
- `submit_batch(begin, end, priv, retries)` — 提交一组 IO；遇到 `EAGAIN` 时以指数退避重试
- `get_next_completed(timeout_ms, paio, max)` — 收割最多 `max` 个完成项

`aio_queue_t` 通过 `libaio`（`io_setup` / `io_submit` / `io_getevents` / `io_destroy`）实现上述接口。

## I/O 生命周期

### 异步 I/O 路径（aio\_read / aio\_write）

```plaintext
aio_read/aio_write(off, len/bl, ioc)
  │
  ├── 1. is_valid_io(off, len) 对齐检查 (Direct I/O)
  │     失败 → 返回 -EINVAL
  │
  ├── 2. 若 aio_ && dio_ (默认路径):
  │     ├── 读: buffer::create_aligned(len, block_size) 分配对齐缓冲
  │     ├── 写: bl.rebuild_aligned(block_size) 对齐 bufferlist
  │     ├── 构造 aio_t, io_prep_preadv/pwritev 设置 iocb
  │     ├── 追加到 ioc->pending_aios (侵入式链表)
  │     └── ioc->num_pending++
  │
  └── 3. 否则回退到同步 read()/write()

aio_submit(ioc)
  │
  ├── 1. 若 num_pending == 0 → 直接返回
  ├── 2. pending_aios splice 到 running_aios
  │     num_running += num_pending, num_pending = 0
  ├── 3. io_queue_->submit_batch(begin, end, priv, &retries)
  │     ├── io_submit() 提交到内核 AIO 上下文
  │     └── 遇到 EAGAIN → 指数退避重试 (最多 16 次)
  └── 4. 失败 → ioc->set_return_value(r)

_aio_thread (后台轮询循环, 50ms 间隔)
  │
  ├── io_queue_->get_next_completed(50, aios, 256)
  │     ├── io_getevents() 收割完成事件
  │     └── 超时 50ms 无事件 → 继续循环
  │
  └── for each completed aio_t:
        ├── io_since_flush_ = true
        ├── res = aio->get_return_value()
        ├── res < 0 或 res != length → ioc->set_return_value(-EIO)
        ├── 回调模式: num_running-- 归零时 → aio_callback(priv, ioc->priv)
        └── 等待模式: ioc->try_aio_wake() → num_running-- 归零时 notify
```

### 同步 I/O 路径（read / write）

```plaintext
read(off, len, &pbl, buffered)
  │
  ├── buffered=false: is_valid_io(off, len) 对齐检查
  ├── 选择 fd: buffered ? fd_buffered_ : fd_direct_
  ├── buffer::create_aligned(len, block_size) 分配对齐缓冲
  └── pread(fd, buf, len, off) → pbl->push_back(buf)

write(off, bl, buffered, write_hint)
  │
  ├── buffered=false: is_valid_io(off, len) + bl.rebuild_aligned(block_size)
  ├── 选择 fd: buffered ? fd_buffered_ : fd_direct_
  ├── bl.prepare_iov(&iov) 构建 scatter/gather
  └── pwritev(fd, iov, off) 循环直到全部写入
        └── io_since_flush_ = true

flush()
  ├── io_since_flush_.compare_exchange_strong(true, false)
  │     失败 (原值为 false) → 返回 0 (无写操作, 跳过 fdatasync)
  └── fdatasync(fd_direct_)
```

## 构建

`CMakeLists.txt` 构建共享库 `libblk.so`，链接 `common` 和 `aio`（libaio）。

## 线程安全

| 资源 | 保护方式 | 说明 |
| ------ | --------- | ------ |
| `IOContext` 内部状态 | `std::mutex` + `std::condition_variable` | `aio_wait` 阻塞等待 `num_running == 0`；`try_aio_wake` 线程安全递减计数 |
| `aio_queue_t` (libaio) | libaio 内部 | `io_submit` / `io_getevents` 线程安全 |
| `KernelDevice` 设备 fd | 无锁 | `fd_direct_` / `fd_buffered_` 在 `open` 后只读，多线程并发 `aio_read`/`aio_write` 安全 |
| AIO 完成线程 | 单线程 `_aio_thread` | 后台轮询线程，50ms 间隔收割完成事件 |
| `io_since_flush_` | 隐式串行 | `flush()` 通过 `fdatasync` 保证；无并发写时为空操作 |

**AIO 完成线程模型**:

- `_aio_thread` 是单线程后台轮询循环，每 50ms 调用 `io_queue_->get_next_completed()`
- 完成的 `aio_t` 通过两种模式通知调用者：
  - **回调模式**: `ioc->priv` 和 `aio_callback` 均已设置时，最后一个 running IO 完成时调用回调
  - **等待模式**: `ioc->try_aio_wake()` 递减 `num_running`，归零时通知条件变量

## 简化与决策

| Ceph 实现 | cxxlab 简化策略 |
| ----------- | ---------------- |
| `CephContext` / `PerfCounters` | 移除（仅统计用途） |
| `AdminSocketHook` debug 接口 | 移除 |
| `bdev_label` 完整标签 | 由上层（BlueStore）管理，KernelDevice 不处理 |
| `bdev_async_device_reset` | 不实现 |
| `discard` | 保留接口，实现通过 `ioctl(BLKDISCARD)` |
| 多路径设备 | 不实现 |

### 已知待办

- [ ] `write_hint`（`WRITE_LIFE_*`）目前未转发到内核（`fcntl(F_SET_RW_HINT)`），这是一个已知缺陷
- [ ] 自适应 iodepth 计算可进一步优化（当前 `max(16, min(128, size/blocksize/4))`）

## 文件

| 文件 | 角色 |
| --- | --- |
| `block_device.h/cc` | 抽象 `BlockDevice` 基类 + 工厂 |
| `kernel_device.h/cc` | `KernelDevice` 实现 |
| `io_context.h/cc` | `IOContext` — 进行中 IO 跟踪器 |
| `aio.h/cc` | `aio_t`（单次操作）+ `aio_queue_t`（libaio 队列） |

## 参考

- Ceph source: `src/os/bluestore/BlockDevice.h` / `.cc`
- Ceph source: `src/os/bluestore/KernelDevice.h` / `.cc`
- 本项目 [docs/design/overview.md](overview.md): 架构总览
- 本项目 [docs/design/allocator.md](allocator.md): Allocator 设计
