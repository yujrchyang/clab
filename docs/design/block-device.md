# blk — Block Device Abstraction Layer

## Overview

The `blk` module provides a portable block device abstraction for direct I/O on
Linux. It wraps raw block devices (e.g. NVMe, SSD) and exposes synchronous and
asynchronous read/write interfaces built on `libaio`.

## Architecture

```plaintext
┌───────────────────────────────────────────────────────────────┐
│                      BlockDevice (abstract)                   │
│  sync: read / write / flush / read_random                     │
│  async: aio_read / aio_write / aio_submit                     │
│  mgmt: discard / invalidate_cache / collect_metadata          │
└──────────────┬────────────────────────────────────────────────┘
               │ inherit
┌──────────────▼────────────────────────────────────────────────┐
│                      KernelDevice                             │
│  - fd_direct_ (O_DIRECT | O_RDWR)                             │
│  - fd_buffered_ (O_RDWR)                                      │
│  - io_queue_ (aio_queue_t)                                    │
│  - aio_thread_ (completion reap loop)                         │
└──────────────┬────────────────────────────────────────────────┘
               │ owns / uses
┌──────────────▼───────────┐   ┌────────────────────────────────────┐
│      IOContext           │   │     io_queue_t (abstract)          │
│  pending_aios / running  │   │  submit_batch / get_next_completed │
│  num_pending / num_run   │   │           │                        │
│  aio_wait / try_aio_wake │   │  ┌────────▼────────┐               │
└──────────────┬───────────┘   │  │  aio_queue_t    │               │
               │ contains      │  │  (libaio impl)  │               │
┌──────────────▼───────────┐   │  └─────────────────┘               │
│         aio_t            │   └────────────────────────────────────┘
│  wraps struct iocb       │
│  iov / bl / fd / priv    │
│  pwritev / preadv        │
│  boost::intrusive hook   │
└──────────────────────────┘
```

## Component Details

### BlockDevice (`block_device.h` / `block_device.cc`)

Abstract base class with:

- **Properties**: `size`, `block_size`, `optimal_io_size`, `rotational`,
  `support_discard`.
- **Validation**: `is_valid_io(off, len)` — checks alignment to `block_size`
  and range within `size`.
- **Factory**: `BlockDevice::create(path, cb, priv)` always returns a
  `KernelDevice` on this platform.
- **Callback**: `aio_callback_t` is invoked when a batch of IOs completes in
  callback mode.

### KernelDevice (`kernel_device.h` / `kernel_device.cc`)

Concrete implementation that opens the block device path **twice**:

| Descriptor | Flags | Purpose |
| --- | --- | --- |
| `fd_direct_` | `O_RDWR \| O_DIRECT \| O_CLOEXEC` | Direct (unbuffered) I/O |
| `fd_buffered_` | `O_RDWR \| O_CLOEXEC` | Buffered I/O fallback |

On open it probes device geometry via `ioctl` (BLKSSZGET, BLKIOOPT,
BLKROTATIONAL, BLKDISCARD) and sets up a `aio_queue_t` with an adaptive
iodepth (`max(16, min(128, size/blocksize/4))`).

**AIO Completion Thread** (`_aio_thread`):

- Background thread polling `io_queue_->get_next_completed(...)` every 50 ms.
- For each completed `aio_t`, determines completion mode:
  - **Callback mode**: if `ioc->priv` and `aio_callback` are set, calls
    `aio_callback(aio_callback_priv, ioc->priv)` when the last running IO
    completes.
  - **Wait mode**: calls `ioc->try_aio_wake()` which decrements the running
    counter and notifies the condition variable when it reaches zero.

**Synchronous I/O** falls back to `pread` / `pwritev`. Buffered writes
track `io_since_flush_` so `flush()` (via `fdatasync(2)`) is a no-op
when no writes have occurred.

### IOContext (`io_context.h` / `io_context.cc`)

Tracks per-I/O-context in-flight operations:

| State | List | Counter |
| --- | --- | --- |
| Not yet submitted | `pending_aios` | `num_pending` |
| Submitted / in-flight | `running_aios` | `num_running` |

- `aio_wait()` — blocks the caller (via `condition_variable`) until
  `num_running == 0`.
- `try_aio_wake()` — thread-safe decrement of `num_running`; notifies
  when zero.
- `release_running_aios()` — clears `running_aios` (caller must guarantee
  no IOs are in-flight).

### aio_t (`aio.h` / `aio.cc`)

Wraps a single `struct iocb` (libaio) and associated metadata:

- `io_prep_pwritev` / `io_prep_preadv` to build the iocb.
- `iov`: a `boost::container::small_vector<struct iovec, 4>` for
  scatter/gather.
- `bl`: holds a `bufferlist` reference to keep data alive during async
  writes.
- Intrusive list hook (`boost::intrusive::list_member_hook<>`) enables
  `aio_list_t` — an intrusive list used for zero-allocation batch tracking.
- `rval` stores the completion result, set by the reap loop via
  `reinterpret_cast<aio_t*>(event.obj)`.

### io_queue_t / aio_queue_t (`aio.h` / `aio.cc`)

Abstract interface:

- `init(fds)` — set up the submission queue.
- `shutdown()` — tear down.
- `submit_batch(begin, end, priv, retries)` — submit a range of IOs;
  retries on `EAGAIN` with exponential backoff.
- `get_next_completed(timeout_ms, paio, max)` — reap up to `max`
  completions.

`aio_queue_t` implements these with `libaio` (`io_setup` / `io_submit` /
`io_getevents` / `io_destroy`).

## I/O Lifecycle

```plaintext
aio_read/aio_write
        │
        ▼
KernelDevice creates aio_t,
appends to ioc->pending_aios
  num_pending++
        │
        ▼
aio_submit(ioc)
  splice pending → running
  num_running += num_pending, num_pending = 0
  io_queue_->submit_batch(...)
        │
        ▼
_aio_thread (polling loop)
  io_queue_->get_next_completed()
  check res == length
  callback mode OR try_aio_wake()
  io_since_flush_ = true
```

## Build

`CMakeLists.txt` builds a shared library `libblk.so` linking against `common`
and `aio` (libaio).

## Files

| File | Role |
| --- | --- |
| `block_device.h/cc` | Abstract `BlockDevice` base + factory |
| `kernel_device.h/cc` | `KernelDevice` implementation |
| `io_context.h/cc` | `IOContext` — in-flight IO tracker |
| `aio.h/cc` | `aio_t` (single op) + `aio_queue_t` (libaio queue) |
