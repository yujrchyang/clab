# common — 基础库

> **实现状态**: 已实现（所有模块的基础依赖）

## 1. 概述

`common` 模块是整个 cxxlab 的基石库，提供序列化框架、缓冲区管理、校验和、UUID、断言和数学工具等基础能力。所有其他模块（`blk`、`kv`、`bluestore`、`btier`）均 PUBLIC 链接 `common`。

## 2. 架构

```plaintext
┌─────────────────────────────────────────────────────────────┐
│                      common (libcommon.so)                  │
│                                                             │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────────┐   │
│  │ bufferlist  │  │ DENC         │  │ CRC32C            │   │
│  │ (buffer.h)  │  │ (denc.h)     │  │ (crc32.h)         │   │
│  └─────────────┘  └──────────────┘  └───────────────────┘   │
│                                                             │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────────┐   │
│  │ uuid_d      │  │ cxxlab_assert│  │ intarith          │   │
│  │ (uuid.h)    │  │ (cassert.h)  │  │ (intarith.h)      │   │
│  └─────────────┘  └──────────────┘  └───────────────────┘   │
│                                                             │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────────┐   │
│  │ error       │  │ safe_io      │  │ formatter         │   │
│  │ (error.h)   │  │ (safe_io.h)  │  │ (formatter.h)     │   │
│  └─────────────┘  └──────────────┘  └───────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### 编译依赖

| 库 | 依赖 (PUBLIC) | 依赖 (PRIVATE) | 构建产物 |
| --- | --- | --- | --- |
| `common` | Boost::boost, isal | spdlog, fmt, pthread | libcommon.so |

> `HAVE_ISA_L=1` 编译宏为 PUBLIC，供 CRC32C 使用 Intel ISA-L 加速库。

## 3. 核心组件

### 3.1 TOPNSPC 宏 (`common_fwd.h`)

```cpp
#define TOPNSPC cxxlab
```

所有模块统一在 `cxxlab` 命名空间下。`common_fwd.h` 是最底层的前置声明头文件，被所有其他头文件包含。

### 3.2 bufferlist (`buffer.h` / `buffer.cc`)

`bufferlist` 是零拷贝缓冲区管理器，核心设计：

- **`buffer::ptr`**: 指向 `buffer::raw`（底层内存块）的智能指针 + offset + length，实现零拷贝切片
- **`buffer::list`**: 有序 `ptr` 集合，支持追加、拼接、顺序/随机遍历
- **`raw` 子类**: 多种内存后端——`raw_malloc`（堆分配）、`raw_posix_aligned`（posix_memalign 对齐分配）、`raw_static`（静态缓冲）
- **对齐支持**: `buffer::create_aligned(len, align)` 用于 Direct I/O 场景（`blk` 模块）
- **`rebuild_aligned(align)`**: 重建 bufferlist 使每个 ptr 对齐到指定边界
- **`prepare_iov(&iov)`**: 生成 `struct iovec` 数组供 `preadv`/`pwritev` scatter/gather
- **CRC 缓存**: 内置 CRC32C 缓存机制（`get_cached_crc` / `track_cached_crc`），避免重复计算

```cpp
bufferlist bl;
bl.append("hello");                    // 追加数据
bl.append((const char*)&val, sizeof(val));  // 追加 POD
bl.rebuild_aligned(4096);              // 对齐到 4KB（Direct I/O 用）

// 遍历
for (auto &ptr : bl) {
    process(ptr.c_str(), ptr.length());
}
```

### 3.3 DENC 序列化框架 (`denc.h`)

基于模板 traits 的编译期序列化框架，通过 `denc(o, p)` 入口统一调度 encode/decode。

#### 设计原理

```plaintext
denc_traits<T>  ── 编译期分发 ──>  encode(T, appender)
                                   decode(T&, iterator)
                                   bound_encode(T&)
```

- `denc_traits<T>` 是主 traits 模板，定义 `supported`、`bounded`、`featured` 等编译期属性
- 每种类型通过特化 `denc_traits` 提供 `encode` / `decode` / `bound_encode` 静态方法
- 顶层入口 `encode(v, bl)` / `decode(v, iter)` 委托给 `denc_traits<T>`

#### 内置支持类型

| 类别 | 类型 |
| --- | --- |
| POD 定长 | `uint8_t`–`uint64_t`、`int8_t`–`int64_t`、`bool` |
| 字符串 | `std::string` |
| buffer | `buffer::ptr`、`buffer::list` |
| 容器 | `std::vector<T>`、`std::list<T>`、`std::set<T>`、`std::map<K,V>` |
| Boost 容器 | `boost::container::flat_map`、`flat_set`、`small_vector` |
| optional | `std::optional<T>`、`boost::optional<T>` |
| pair | `std::pair<A, B>` |

#### 自定义类型序列化

#### 方式 A: DENC 宏（推荐，结构体内联）

```cpp
struct Extent {
    uint64_t offset;
    uint32_t length;
    DENC(Extent, v, p) {
        DENC_START(1, 1, p);    // struct_v=1, compat_v=1
        denc(v.offset, p);
        denc(v.length, p);
        DENC_FINISH(p);
    }
};
```

`DENC_START(struct_v, compat_v, p)` 写入版本头，`DENC_FINISH(p)` 写入结束标记。版本号用于前向/后向兼容。

#### 方式 B: WRITE_CLASS_DENC（traits 特化）

```cpp
struct Blob {
    uint64_t id;
    void encode(buffer::list::contiguous_appender &p) const { ... }
    void decode(buffer::ptr::const_iterator &p) { ... }
    void bound_encode(size_t &n) const { ... }
};
WRITE_CLASS_DENC(Blob);  // 在 TOPNSPC 命名空间内
```

#### 使用规则

- **简单 POD 字段**（uint64/int32 等用于 FreelistManager meta）：直接 `bl.append((const char*)&v, sizeof(v))` / `p.copy(...)`，不需要 DENC
- **简单容器/vector/string**：直接 `#include "common/denc.h"`，模板已内置支持
- **复合结构体**（onode\_t、extent\_map 等含多成员 + map + vector）：用 `DENC` 宏或 `WRITE_CLASS_DENC`

### 3.4 CRC32C (`crc32.h` / `crc32.cc`)

```cpp
uint32_t calc_crc32(const uint8_t *data, size_t length,
                    uint32_t previous_crc = 0);
```

- 使用 Intel ISA-L 加速库（`HAVE_ISA_L=1`），支持流式增量计算（`previous_crc` 参数）
- 用于 BlueFS 超级块校验、BlueStore blob 校验和、BTier ExtentHeader CRC

### 3.5 uuid\_d (`uuid.h`)

```cpp
struct uuid_d {
    std::array<uint8_t, 16> uuid{};
    void generate();         // /dev/urandom + RFC 4122 v4
    bool is_zero() const;
    std::string to_string() const;
};
```

RFC 4122 version 4 UUID，通过 `/dev/urandom` 生成。用于 BlueFS 超级块（`uuid` + `osd_uuid`）和 BlueStore 设备标签。

### 3.6 断言 (`cassert.h` / `cassert.cc`)

```cpp
#define cxxlab_assert(expr)         // 致命断言 → abort
#define cxxlab_assertf(expr, ...)   // 带格式化消息的断言
#define cxxlab_abort(...)           // 主动终止 + 格式化消息
#define assert_warn(expr)           // 警告但不终止
```

- `cxxlab_assert` 是 `common_assert` 的别名，断言失败时调用 `__common_assert_fail` 打印文件/行号/函数名后 `abort()`
- `assert_warn` 仅打印警告，不终止程序
- `__PRETTY_FUNCTION__` / `__func__` 根据编译器支持自动选择

### 3.7 数学工具 (`intarith.h`)

模板化的编译期数学函数，全部 `constexpr inline`：

| 函数 | 说明 |
| --- | --- |
| `div_round_up(n, d)` | 向上取整除法 `(n + d - 1) / d` |
| `round_up_to(n, d)` | 向上对齐到 `d` 的倍数 |
| `round_down_to(n, d)` | 向下对齐到 `d` 的倍数 |
| `shift_round_up(x, y)` | `(x + (1<<y) - 1) >> y` |
| `isp2(x)` | 判断是否为 2 的幂 |
| `p2align(x, align)` | 2 的幂对齐（位运算） |
| `p2roundup(x, align)` | 2 的幂向上对齐（位运算） |
| `p2phase(x, align)` | 2 的幂取模（位运算） |

> `p2align` 等函数要求 `align` 为 2 的幂，使用位运算而非取模，性能更优。非 2 的幂对齐使用 `round_up_to` / `round_down_to`。

### 3.8 interval\_set (`blk/extent_types.h`)

> **注意**: `interval_set` 定义在 `blk/extent_types.h` 而非 `common/`，因为它是空间管理的核心类型，与 `pextent_t` 紧密关联。

```cpp
template <typename T>
class interval_set {
    void insert(T off, T len);   // 插入区间，自动合并相邻
    void erase(T off, T len);    // 移除区间
    bool empty() const;
    T range_start() const;
    T range_end() const;
};
```

基于 `std::map<T, T>`（key=offset, value=length），`insert` 时自动合并相邻和重叠区间。用于 Allocator `release()` 接口和 BlueStore `txc->allocated` / `txc->released`。

### 3.9 其他工具

| 组件 | 文件 | 说明 |
| --- | --- | --- |
| `safe_io` | `safe_io.h/cc` | 安全读写封装（处理 EINTR 重试） |
| `error` | `error.h/cc` | 错误码工具函数 |
| `formatter` | `formatter.h/cc` | JSON 格式化输出 |
| `logger` | `logger.h/cc` | spdlog 日志封装 |
| `page` | `page.h` | 页大小常量（`PAGE_SIZE`） |
| `spinlock` | `spinlock.h/cc` | 自旋锁实现 |
| `byteorder` | `byteorder.h` | 字节序工具（big-endian 编码） |

## 4. 构建

`common` 编译为 `libcommon.so`（SHARED），是所有模块的基础依赖：

```cmake
add_library(common SHARED buffer.cc cassert.cc crc32.cc ...)
target_link_libraries(common PUBLIC Boost::boost isal)
target_link_libraries(common PRIVATE spdlog fmt pthread)
target_compile_definitions(common PUBLIC HAVE_ISA_L=1)
```

## 5. 参考

- Ceph source: `src/common/buffer.h` / `buffer.cc`
- Ceph source: `src/common/denc.h`
- Ceph source: `src/common/crc32c.h` / `crc32c.cc`
- 本项目 [docs/design/overview.md](overview.md): 架构总览
- 本项目 `common/buffer.h`: bufferlist 定义
- 本项目 `common/denc.h`: DENC 序列化框架
- 本项目 `common/intarith.h`: 数学工具函数
- 本项目 `blk/extent_types.h`: `interval_set` / `pextent_t` 定义
