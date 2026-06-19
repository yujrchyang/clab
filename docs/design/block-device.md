# blk — 块设备抽象层

## 概述

`blk` 模块为 Linux 上的直接 I/O 提供可移植的块设备抽象。它封装了原始块设备（如 NVMe、SSD），基于 `libaio` 实现了同步和异步读写接口。

## 架构

```plaintext
┌───────────────────────────────────────────────────────────────┐
│                      BlockDevice (抽象基类)                    │
│  同步: read / write / flush / read_random                     │
│  异步: aio_read / aio_write / aio_submit                      │
│  管理: discard / invalidate_cache / collect_metadata           │
└──────────────┬────────────────────────────────────────────────┘
               │ 继承
┌──────────────▼────────────────────────────────────────────────┐
│                      KernelDevice                              │
│  - fd_direct_ (O_DIRECT | O_RDWR)                              │
│  - fd_buffered_ (O_RDWR)                                       │
│  - io_queue_ (aio_queue_t)                                     │
│  - aio_thread_ (完成收割循环)                                   │
└──────────────┬────────────────────────────────────────────────┘
               │ 拥有/使用
┌──────────────▼───────────┐   ┌────────────────────────────────────┐
│      IOContext           │   │     io_queue_t (抽象基类)          │
│  pending_aios / running  │   │  submit_batch / get_next_completed │
│  num_pending / num_run   │   │           │                        │
│  aio_wait / try_aio_wake │   │  ┌────────▼────────┐               │
└──────────────┬───────────┘   │  │  aio_queue_t    │               │
               │ 包含          │  │  (libaio 实现)  │               │
┌──────────────▼───────────┐   │  └─────────────────┘               │
│         aio_t            │   └────────────────────────────────────┘
│  封装 struct iocb        │
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

**内部回退标志：**

- `dio_`（默认 `true`）：设为 `false` 时回退到带缓冲 I/O
- `aio_`（默认 `true`）：设为 `false` 时回退到同步 `pread`/`pwritev`（而非 libaio）
- `write_hint`：`WRITE_LIFE_*` 枚举已在 `write()` / `aio_write()` 中接受，但目前**未转发**到内核（`fcntl(F_SET_RW_HINT)`）；此为一个已知缺陷

**AIO 完成线程**（`_aio_thread`）：

- 后台线程每 50 ms 轮询 `io_queue_->get_next_completed(...)`。
- 对每个完成的 `aio_t`，确定完成模式：
  - **回调模式**: 如果 `ioc->priv` 和 `aio_callback` 均已设置，则最后一个正在运行的 IO 完成时调用 `aio_callback(aio_callback_priv, ioc->priv)`。
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

```plaintext
aio_read/aio_write
        │
        ▼
KernelDevice 创建 aio_t，
追加到 ioc->pending_aios
  num_pending++
        │
        ▼
aio_submit(ioc)
  将 pending 移至 running
  num_running += num_pending, num_pending = 0
  io_queue_->submit_batch(...)
        │
        ▼
_aio_thread (轮询循环)
  io_queue_->get_next_completed()
  检查 res == length
  回调模式 或 try_aio_wake()
  io_since_flush_ = true
```

## 构建

`CMakeLists.txt` 构建共享库 `libblk.so`，链接 `common` 和 `aio`（libaio）。

## 文件

| 文件 | 角色 |
| --- | --- |
| `block_device.h/cc` | 抽象 `BlockDevice` 基类 + 工厂 |
| `kernel_device.h/cc` | `KernelDevice` 实现 |
| `io_context.h/cc` | `IOContext` — 进行中 IO 跟踪器 |
| `aio.h/cc` | `aio_t`（单次操作）+ `aio_queue_t`（libaio 队列） |
