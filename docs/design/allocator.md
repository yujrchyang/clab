# Allocator — 内存级空闲空间分配器

## 1. 需求分析

### 1.1 背景

在 BlueStore 单机引擎架构中，`Allocator` 负责**运行时内存中**的空间分配决策，与 `FreelistManager`（持久化分配状态）构成双层空间管理：

| 组件 | 角色 | 存储位置 |
| --- | --- | --- |
| `Allocator` | 运行时的分配决策（返回空闲 extent） | RAM |
| `FreelistManager` | 分配状态的持久化追踪 | KV store (RocksDB) |

两者的关系：

```plaintext
IO 写入路径:
  alloc->allocate(want, unit, max_alloc_size, hint, &extents)  // 内存中标记已分配
  → bdev->write(extents, data)                                 // 写入设备
  → _txc_finalize_kv: fm->allocate(off, len, txn)              // 持久化到 KV

回收路径:
  _txc_release_alloc: alloc->release(txc->released)            // 内存中归还（延迟到所有前置 IO 完成后）
  → _txc_finalize_kv: fm->release(off, len, txn)               // 持久化到 KV

恢复路径:
  fm->enumerate_next() → alloc->init_add_free(offset, length)  // 从 FM 重建
```

### 1.2 约束条件

- **操作系统**: 仅 Linux（x86\_64 + AArch64）
- **后端存储**: 依赖 `kv::KeyValueDB` 抽象层（FreelistManager 的持久化后端）
- **最小分配单元 (alloc unit)**: 与 `FreelistManager::bytes_per_block` 一致（通常 4KB/16KB/64KB）
- **分配器类型**: 通过配置项选择（`bluestore_allocator`），可选 `"bitmap"` / `"avl"` / `"hybrid"`

### 1.3 核心功能

| 功能 | 说明 |
| --- | --- |
| `allocate(want, unit, max_alloc_size, hint, extents)` | 分配 `want` 字节，返回一个或多个 extent（`PExtentVector`）。`hint` 提示起始搜索位置 |
| `release(release_set)` | 归还空间，相邻 extent 自动合并 |
| `init_add_free(offset, length)` | 启动恢复时添加空闲区间 |
| `init_rm_free(offset, length)` | 启动恢复时移除指定区域（标记已分配） |
| `get_free()` | 查询剩余空闲空间总量 |
| `get_fragmentation()` | 碎片率评估 |
| `foreach(notify)` | 遍历所有空闲区间 |

### 1.4 与 FreelistManager 对比

| 维度 | Allocator | FreelistManager |
| --- | --- | --- |
| 存储位置 | RAM | KV store (RocksDB) |
| 粒度 | alloc unit | block (通常 = alloc unit) |
| 数据结构 | AVL tree / 3-level bitmap | RocksDB KV (XOR merge) |
| 分配/释放 | `allocate()` / `release()` | `allocate()` / `release()` |
| 恢复时 | `init_add_free()` 从 FM 重建 | `enumerate_next()` 扫描空闲区间 |
| 线程安全 | 内部 `std::mutex` 保护 | 无锁 (allocate/release 通过 merge 操作) |

## 2. 架构设计

### 2.1 Allocator 抽象基类

```plaintext
┌───────────────────────────────────────────────┐
│               Allocator (abstract)            │
│  allocate / release / init_add_free /         │
│  init_rm_free / get_free / foreach / dump     │
│  get_name / get_capacity / get_block_size     │
│  get_fragmentation / get_fragmentation_score  │
│  create(type, capacity, block_size)           │
└──────┬──────────────┬──────────────┬──────────┘
       │              │              │
       ▼              ▼              ▼
┌─────────────┐ ┌──────────┐ ┌──────────────┐
│ AvlAllocator│ │ Bitmap   │ │HybridAllocator│
│             │ │Allocator │ │ (Avl + Bitmap)│
│ offset AVL  │ │ 3级位图  │ │              │
│ + size AVL  │ │ L0/L1/L2 │ │ spillover    │
│ first/best  │ │ 64bit op │ │ 机制        │
│ fit 策略    │ │ 硬件加速 │ │              │
└─────────────┘ └──────────┘ └──────────────┘
```

### 2.2 AvlAllocator 设计

#### 2.2.1 数据结构

两棵 `boost::intrusive::avl_set`：

```plaintext
range_tree (offset 排序):
  [0 ~ 1M] → [2M ~ 5M] → [8M ~ 10M] → ...

range_size_tree (size 排序):
  [8M ~ 10M](2M) → [0 ~ 1M](1M) → [2M ~ 5M](3M)
```

```cpp
struct range_seg_t {
    uint64_t start;
    uint64_t end;
    boost::intrusive::avl_set_member_hook<> offset_hook;
    boost::intrusive::avl_set_member_hook<> size_hook;
};
```

辅助结构：

- **`lbas[64]`**: 按 size 的 highest power-of-2 分桶的 cursor 数组，每个桶记录上次分配位置
- **`num_free`**: 内存中的总空闲字节数

#### 2.2.2 分配策略：First-fit + Best-fit 混合

```plaintext
_allocate(size, unit, &offset, &length):
  1. 获取 max_size = range_size_tree 中的最大区间长度
  2. 若 max_size < size → 降级到 max_size（若 max_size < unit 则返回 ENOSPC）
  3. 决策路径:
     a. force_range_size_alloc 或 max_size < threshold(128K) 或 free_pct < 4%:
        → 直接走 best-fit
     b. 否则:
        → first-fit: _pick_block_after(cursor, size, unit)
           - 从 cursor 位置开始在 range_tree 中顺序搜索
           - 搜索上限: max_search_count(100) / max_search_bytes(16MB)
           - 超过上限或找不到 → 降级到 best-fit
     c. best-fit: _pick_block_fits(size, unit)
        - 在 range_size_tree 中 lower_bound 找到 >= size 的最小区间
        - 若找不到 → size /= 2, 重复直到 size < unit
  4. _remove_from_tree(start, size) → 分裂/删除节点
```

#### 2.2.3 释放策略

```plaintext
_add_to_tree(start, size):
  1. 在 range_tree 中用 upper_bound 找到插入位置 rs_after
  2. 获取前驱节点 rs_before
  3. 尝试合并:
     - rs_before->end == start → 合并到前驱
     - rs_after->start == end → 合并到后继
     - 两边都满足 → 三合一
     - 都不满足 → 新建节点插入
  4. 插入 size_tree
  5. 若插入 size_tree 时达到 range_count_cap 上限:
     → 小于最小节点长度的 segment 被转移到派生类处理 (HybridAllocator::_spillover_range)
```

#### 2.2.4 配置参数

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `range_size_alloc_threshold` | 128KB | 最大连续空闲 < 该值时强制 best-fit |
| `range_size_alloc_free_pct` | 4% | 空闲率 < 该值时强制 best-fit |
| `max_search_count` | 100 | first-fit 最大搜索次数 |
| `max_search_bytes` | 16MB | first-fit 最大搜索字节数 |
| `range_count_cap` | 取决于 max_mem | AVL 树节点数上限 (0=无限制) |

### 2.3 BitmapAllocator 设计

#### 2.3.1 三级位图结构

```plaintext
    | AU | AU |                    ......                    |   磁盘
    | 0  | 1  |                    ......                    |   L0
    .                         .
       .     64 bytes (512 bits) .
          .    (1 slotset)    .
              .         .
              | 00 | 01 | 11 |                               |   L1
               .           .
            | 0 | 0 | 1 |                                    |   L2
```

**层级定义**:

| 层级 | 粒度 | 每 slot 条目 | 条目含义 |
| --- | --- | --- | --- |
| L0 | alloc unit (e.g. 4KB) | 64 (uint64_t 每个 bit 表示一个 AU) | 1=空闲，0=已分配 |
| L1 | 64 * AU (512 个 AU ≈ 2MB @ 4KB) | 32 (每 2 bit 描述一个 L0 slotset) | 00=全分配，01=部分分配，11=全空闲 |
| L2 | 32 * L1 粒度 (≈ 64MB @ 4KB) | 64 (每 1 bit 描述一个 L1 slotset) | 0=全分配，1=有可用空间 |

**对齐优化**:

- `slots_per_slotset = 8` 个 `uint64_t` = 64 字节 = x86\_64 cache line 大小
- 每一层的分配/标记以 slotset 为单位操作，最大化 cache 利用率

#### 2.3.2 分配算法

```plaintext
_allocate_l2(want, min_length, max_length, hint, allocated, extents):
  1. 若 hint != 0 → last_pos = align(hint / l2_granularity, L1_ENTRIES_PER_SLOT)
  2. 两轮扫描:
     第一轮: [last_pos → end]
     第二轮: [0 → last_pos) （绕回）
  3. 外层槽扫描 (以 l2 slot 为单位):
     - slot_val == all_slot_clear → 跳过（全分配）
     - slot_val == all_slot_set → 全空闲，分配 l1 全部
     - 否则 → 找空闲 bit，分配单个 l1 slotset
  4. 内层 _allocate_l1() → _allocate_l0():
     - 遍历 l0 数组的 slot（uint64_t）
     - slot_val == all_slot_set → 取整个 slot
     - 否则逐 bit 扫描，找连续空闲位
     - 使用 __builtin_ffsll / __builtin_popcountll 等硬件加速指令
     - _fragment_and_emplace(): 合并相邻 extent，按 max_length 切片
```

#### 2.3.3 释放算法

```plaintext
_free_l2(release_set):
  for each (offset, length) in release_set:
    1. l1._free_l1(offset, length) → 标记 L0/L1 位图
    2. _mark_l2_free(l2_pos, l2_pos_end) → 更新 L2 位图
    3. available += released_length
```

### 2.4 HybridAllocator 设计

HybridAllocator 继承 AvlAllocator，内嵌一个 BitmapAllocator 作为 fallback：

```plaintext
                 AvlAllocator (primary)
                /              \
         AVL tree          BitmapAllocator (fallback)
        (大块连续)         (小块零散)
```

#### 2.4.1 分配策略

```plaintext
allocate(want, unit, max_alloc_size, hint, extents):
  1. 尝试从 AVL 分配
  2. 若不够 → bitmap 补足
  3. 若 AVL 完全失败 → 释放已分配，回退到 bitmap
```

当前实现简化了 Ceph 原版的策略——始终优先尝试 AVL 树，AVL 不能满足时回退到 bitmap，
不再检查 bitmap 是否有空间或 want 与 AVL 最小节点的关系。
`_add_to_tree()` 认领回收 (详见 §2.4.3) 是主要的 bitmap → AVL 回流通道。

#### 2.4.2 Spillover 机制

```cpp
// AvlAllocator::_try_insert_range():
// 当 range_size_tree.size() >= range_count_cap 且新区间 < 树中最小节点时
_spillover_range(start, end):
  if (!bmap_alloc)
    bmap_alloc = new BitmapAllocator(cct, capacity, block_size, name + ".fallback")
  bmap_alloc->init_add_free(start, size)  // 溢出到 bitmap
```

#### 2.4.3 合并时的回收机制

`HybridAllocator` 重写（override）了 `_add_to_tree()`，在插入 AVL 树之前尝试从 bitmap
child 中"认领"相邻的空闲区间，以增加合并成大块连续区间的概率：

```cpp
void HybridAllocator::_add_to_tree(uint64_t start, uint64_t size) {
    if (child_) {
        uint64_t head = child_->claim_free_to_left(start);
        uint64_t tail = child_->claim_free_to_right(start + size);
        start -= head;
        size += head + tail;
    }
    AvlAllocator::_add_to_tree(start, size);
}
```

### 2.5 hint 参数的使用

`hint` 参数的行为因分配器实现而异：

| 分配器 | hint 处理 | 说明 |
| --- | --- | --- |
| AvlAllocator | 忽略 | 注释 `// unused, for now!`，使用 lbas cursor 替代 |
| BitmapAllocator | 使用 | `last_pos = align(hint / l2_granularity, d)`，设置搜索起始 L2 slot；两轮扫描 [hint→end] + [0→hint) |
| HybridAllocator | 透传 | 传递给内部 BitmapAllocator，AVL 路径忽略 |
| BlueStore 写路径 | 始终传 0 | `_do_alloc_write()` 中 `hint = 0` |
| BlueFS | 传末 extent 结尾 | 鼓励连续分配 |

**clab 决策**: 由于 BlueStore 写路径始终传 0，AvlAllocator 也忽略 hint，移植时 BitmapAllocator 可以保留 hint 逻辑但非必须。

## 3. 接口设计

### 3.1 Allocator 基类

所有模块均位于 `TOPNSPC`（即 `clab`）命名空间下。

```cpp
class Allocator {
public:
    Allocator(std::string_view name, int64_t capacity, int64_t block_size);
    virtual ~Allocator();

    virtual const char* get_type() const = 0;

    /// 分配 want 字节，返回零或多个 extent。
    /// want: 期望分配总大小
    /// block_size: 对齐单位（通常 = alloc unit）
    /// max_alloc_size: 单个 extent 上限（0=不限制）
    /// hint: 期望起始偏移（0 表示不指定）
    /// extents: 输出参数
    /// 返回实际分配的字节数，负值表示错误
    virtual int64_t allocate(uint64_t want, uint64_t block_size,
                             uint64_t max_alloc_size, int64_t hint,
                             PExtentVector *extents) = 0;

    int64_t allocate(uint64_t want, uint64_t block_size,
                     int64_t hint, PExtentVector *extents);

    virtual void release(const interval_set<uint64_t>& release_set) = 0;
    void release(const PExtentVector& release_set);

    virtual void dump() = 0;
    virtual void foreach(
        std::function<void(uint64_t offset, uint64_t length)> notify) = 0;

    virtual void init_add_free(uint64_t offset, uint64_t length) = 0;
    virtual void init_rm_free(uint64_t offset, uint64_t length) = 0;

    virtual uint64_t get_free() = 0;
    virtual double get_fragmentation() { return 0.0; }
    virtual double get_fragmentation_score();

    virtual void shutdown() = 0;

    static Allocator* create(
        const std::string& type,
        int64_t size,
        int64_t block_size,
        std::string_view name = "");

    const std::string& get_name() const;
    int64_t get_capacity() const { return device_size; }
    int64_t get_block_size() const { return block_size_; }

protected:
    const int64_t device_size;
    const int64_t block_size_;
};
```

### 3.2 PExtentVector（物理 extent 容器）

沿用已有 `bluestore_types.h` 中的定义（或 clab 的等价类型）：

```cpp
struct bluestore_pextent_t {
    uint64_t offset = 0;
    uint32_t length = 0;
};

using PExtentVector = std::vector<bluestore_pextent_t>;
```

### 3.3 interval_set（区间容器）

用于 release 接口。Ceph 使用 `interval_set<uint64_t>`，clab 可复用已有的 `interval_set` 或 `std::map<uint64_t, uint64_t>`。

## 4. 关键流程

### 4.1 启动恢复流程

```plaintext
BlueStore::_open_db_and_fm()
  │
  ├── db->open()                          // 打开 KV store
  ├── fm->init(kvdb, ...)                 // 加载 FM 配置
  │
  ├── alloc = Allocator::create(type, bdev->get_size(), min_alloc_size)
  │
  ├── if (!fm->is_null_manager()):
  │     fm->enumerate_reset()
  │     while (fm->enumerate_next(db, &offset, &length))
  │         alloc->init_add_free(offset, length)  // 重建空闲映射
  │     fm->enumerate_reset()
  │
  └── [null_manager 模式]:
        restore_allocator(alloc)           // 从 BlueFS 文件恢复
```

### 4.2 写入 IO 路径

```plaintext
BlueStore::_do_alloc_write()
  │
  ├── alloc->allocate(need, min_alloc_size, need, 0, &prealloc)
  │     └── 内存标记已分配
  │
  ├── for each write item:
  │     ├── 从 prealloc 中取 extent
  │     ├── txc->allocated.insert(off, len)
  │     └── bdev->write(off, data)
  │
  ├── _txc_finalize_kv(txc, txn):
  │     ├── fm->allocate(off, len, txn)     // 持久化
  │     └── fm->release(off, len, txn)
  │
  └── _txc_release_alloc(txc):
        └── alloc->release(txc->released)   // 延迟回收
```

### 4.3 null_manager 模式

当 FM 启用 null_manager 时，allocate/release 不写 KV store。但 Allocator 仍正常在内存中标记分配/释放。关闭时将 allocator 的完整状态写入 BlueFS 文件：

```plaintext
close():
  store_allocator(alloc) → BlueFS file "allocator_ncb"
```

## 5. 移植决策

### 5.1 实现策略

| 分配器 | 优先级 | 复杂度 | 说明 |
| --- | --- | --- | --- |
| `AvlAllocator` | **P0 - 必选** | 中等 | 默认分配器，性能均衡，适合通用场景 |
| `BitmapAllocator` | P1 | 高 | 三级位图 + 硬件加速，性能最优但复杂度高 |
| `HybridAllocator` | P2 | 高 | 依赖 Avl + Bitmap，可在两者完成后组合 |

建议移植顺序：**AvlAllocator → BitmapAllocator → HybridAllocator**

### 5.2 简化项

| Ceph 实现 | clab 简化策略 |
| --- | --- |
| `AdminSocketHook` debug 接口 | 移除（ASok 调试钩子，非核心） |
| `mempool` 内存监控 | 移除（仅统计用途） |
| `cct->_conf` 配置读取 | 改用构造函数参数或全局配置结构体 |
| `bluestore_types.h` 中 `bluestore_pextent_t` | clab 中定义等价类型 |
| ZonedAllocator / StupidAllocator | 不做移植 |
| `_fragment_and_emplace` 中的 max_length 切片 | 保留（避免单 extent 过大） |
| `get_fragmentation_score` 算法 | 保留基础实现，移除 clz 等平台相关问题（可用 `__builtin_clzll`） |

### 5.3 平台相关注意事项

| 特性 | x86\_64 | AArch64 |
| --- | --- | --- |
| `__builtin_ffsll` / `__builtin_popcountll` | 支持 | 支持 |
| cache line size | 64 bytes | 64 bytes (大多数) |
| `__builtin_clz` / `__builtin_ctz` | 支持 | 支持 |

三级位图（BitmapAllocator）中使用的 GCC 内建函数在 x86\_64 和 AArch64 上均有良好支持，无需额外抽象层。

### 5.4 依赖关系

Allocator 依赖：

- `common` 库: `bufferlist`、`clab_assert`、`interval_set`、`intarith` 工具函数
- 无 `kv` / `rocksdb` 依赖（只操作内存）
- 无 `blk` 依赖

### 5.5 线程安全

所有分配器实现自包含 `std::mutex`，外部不额外加锁。关键路径：

| 操作 | 加锁方式 |
| --- | --- |
| `allocate` | 内部 lock，全路径持锁 |
| `release` | 内部 lock |
| `get_free` | 内部 lock（仅读 num_free） |
| `init_add_free` / `init_rm_free` | 内部 lock |
| `foreach` / `dump` | 内部 lock |

### 5.6 与 FreelistManager 的关系

```plaintext
mkfs 时：
  alloc → 全空闲状态
  fm->create() → 初始化 KV 位图

挂载时：
  fm->enumerate_next() → alloc->init_add_free()  // 重建

运行时：
  IO 前：alloc->allocate()    // 内存决策
  IO 后：fm->allocate()       // 持久化

回收时：
  IO 完成：alloc->release()   // 内存归还（延迟）
           fm->release()     // 持久化
```

## 6. 参考

- Ceph source: `src/os/bluestore/Allocator.h` / `.cc`
- Ceph source: `src/os/bluestore/AvlAllocator.h` / `.cc`
- Ceph source: `src/os/bluestore/BitmapAllocator.h` / `.cc`
- Ceph source: `src/os/bluestore/HybridAllocator.h` / `.cc`
- Ceph source: `src/os/bluestore/fastbmap_allocator_impl.h`（三级位图核心实现）
- Ceph source: `src/os/bluestore/BlueStore.cc`（`_create_alloc`, `_init_alloc`, `_do_alloc_write`, `_txc_finalize_kv`, `_txc_release_alloc`）
- 本项目的 `docs/design/freelist-manager.md`: FreelistManager 设计
- 本项目的 `kv/key_value_db.h`: KV 抽象层
