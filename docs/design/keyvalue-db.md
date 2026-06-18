# kv — Key-Value Storage Abstraction Layer

## 1. 需求分析

### 1.1 背景

clab 需要在 block device 层之上实现一个类 BlueStore 的对象存储引擎。Ceph BlueStore 依赖一个 KV 存储作为元数据与状态持久化引擎，承担以下职责：

| 数据类别 | KV Prefix | 内容 |
| --- | --- | --- |
| 超级块 | `PREFIX_SUPER` ("S") | nid/blobid 计数器、分配参数 |
| 统计信息 | `PREFIX_STAT` ("T") | 全局/每池 statfs 数据 |
| 集合 (Collection) | `PREFIX_COLL` ("C") | 集合名 → cnode_t |
| 对象元数据 (Onode) | `PREFIX_OBJ` ("O") | 对象名 → onode_t（含 extent map） |
| Omap (旧) | `PREFIX_OMAP` ("M") | nid + key → value |
| Omap (按 PG) | `PREFIX_PERPG_OMAP` ("p") | pool + hash + nid + key → value |
| Omap (按 pool) | `PREFIX_PERPOOL_OMAP` ("m") | pool + nid + key → value |
| Omap (meta PG) | `PREFIX_PGMETA_OMAP` ("P") | meta PG 的 omap |
| Deferred WAL | `PREFIX_DEFERRED` ("L") | seq → deferred_transaction_t |
| Freelist (extent) | `PREFIX_ALLOC` ("B") | offset → length |
| Freelist (bitmap) | `PREFIX_ALLOC_BITMAP` ("b") | 位图分配元数据 |
| 共享 Blob | `PREFIX_SHARED_BLOB` ("X") | sb_id → shared_blob_t |

### 1.2 约束条件

- **操作系统**: 仅 Linux（x86\_64 + AArch64）
- **后端实现**:
  - **RocksDB** — 生产级持久化引擎
  - **MemDB** — 纯内存实现，仅用于调试/演示/单元测试，无持久化保障
- **接口要求**: 必须支持 MergeOperator，这是 BlueStore 原子分配和统计更新的基础

### 1.3 功能需求

| 需求 | 说明 | 优先级 |
| --- | --- | --- |
| Point Read | `get(prefix, key) → value` | P0 |
| Batch Read | `get(prefix, keys) → map<key, value>` | P0 |
| Point Write | `set(prefix, key, value)` | P0 |
| Point Delete | `rmkey(prefix, key)` | P0 |
| Single-Key Delete (LSM opt) | `rm_single_key(prefix, key)` — 仅删除最新版本 | P0 |
| Prefix Delete | `rmkeys_by_prefix(prefix)` | P0 |
| Range Delete | `rm_range_keys(prefix, start, end)` | P0 |
| Merge | `merge(prefix, key, delta)` — 原子增量操作 | P0 |
| 前缀迭代 | `get_iterator(prefix)` — 正反向遍历 | P0 |
| 全空间迭代 | `get_wholespace_iterator()` — 遍历全部 prefix | P0 |
| 迭代器边界 | `IteratorBounds` — lower\_bound / upper\_bound 过滤 | P0 |
| NOCACHE 扫描 | `ITERATOR_NOCACHE` — 不污染缓存的大规模扫描 | P0 |
| 事务提交 | `submit_transaction(t)` — 异步 | P0 |
| 同步提交 | `submit_transaction_sync(t)` — 用于 bootstap / batch | P0 |
| Compact | `compact()` / `compact_async()` — 全量 SST 合并 | P1 |
| Merge Op 注册 | `set_merge_operator(prefix, op)` — open 前调用 | P0 |
| 空间估算 | `get_estimated_size()` — 磁盘占用统计 | P1 |

## 2. 架构设计

### 2.1 整体架构

```plaintext
┌────────────────────────────────────────────────────────────┐
│                      KeyValueDB (abstract)                 │
│  Base: factory / get / iterator_bounds / merge_op_reg      │
│  Inner: PrefixIteratorImpl (WholeSpaceIterator prefix      │
│         filter wrapper)                                    │
└─────────────┬───────────────────────────────┬──────────────┘
              │ inherit                       │ inherit       
┌─────────────▼──────────────┐  ┌─────────────▼──────────────┐
│       RocksDBStore         │  │          MemDB             │
│  rocksdb::DB wrapper       │  │  std::map + std::mutex     │
│  key: prefix+'\0'+key      │  │  pure memory, no persist   │
│  supports MergeOperator    │  │  supports MergeOperator    │
│  supports IteratorBounds   │  │  iterator invalidation(smp)│
└────────────────────────────┘  └────────────────────────────┘
```

### 2.2 构建组织

```plaintext
kv/
├── CMakeLists.txt            ← 构建 libkv.so
├── key_value_db.h            ← 抽象基类 (接口 + 内部 PrefixIteratorImpl)
├── key_value_db.cc           ← create() 工厂方法
├── mem/
│   ├── mem_db.h
│   └── mem_db.cc
├── rocksdb/
│   ├── rocksdb_store.h
│   └── rocksdb_store.cc
└── merge_op/
    ├── int64_array_merge_op.h  ← 用于 PREFIX_STAT
    ├── xor_merge_op.h          ← 用于 PREFIX_ALLOC_BITMAP
    └── merge_op.h              ← 抽象基类
```

### 2.3 依赖关系

```plaintext
libkv.so
  ├── libcommon.so (bufferlist, cassert, error)
  ├── librocksdb (仅 RocksDBStore 链接)
  └── Boost (仅 RocksDBStore: intrusive list for cache)
```

## 3. 接口设计

### 3.1 MergeOperator 基类

```cpp
class MergeOperator {
public:
  virtual ~MergeOperator() = default;
  /// Merge into non-existent key (existing_value is empty)
  virtual void merge_nonexistent(
    const char *rdata, size_t rlen, std::string *new_value) = 0;
  /// Merge into existing key
  virtual void merge(
    const char *ldata, size_t llen,
    const char *rdata, size_t rlen,
    std::string *new_value) = 0;
  virtual const char *name() const = 0;
};
```

### 3.2 KeyValueDB 抽象基类

```cpp
class KeyValueDB {
public:
  // ── Transaction ─────────────────────────────────────────
  struct TransactionImpl {
    virtual void set(const std::string &prefix,
                     const std::string &k,
                     const bufferlist &bl) = 0;
    virtual void set(const std::string &prefix,
                     const char *k, size_t klen,
                     const bufferlist &bl);
    virtual void rmkey(const std::string &prefix,
                       const std::string &k) = 0;
    virtual void rmkey(const std::string &prefix,
                       const char *k, size_t klen);
    /// Optimized single-key delete (LSM-tree stores).
    /// Only removes the latest version; behavior undefined if key
    /// has been overwritten.
    virtual void rm_single_key(const std::string &prefix,
                                const std::string &k) {
      rmkey(prefix, k);
    }
    virtual void rmkeys_by_prefix(const std::string &prefix) = 0;
    virtual void rm_range_keys(const std::string &prefix,
                                const std::string &start,
                                const std::string &end) = 0;
    virtual void merge(const std::string &prefix,
                       const std::string &k,
                       const bufferlist &value) = 0;
    virtual ~TransactionImpl() = default;
  };
  using Transaction = std::shared_ptr<TransactionImpl>;

  // ── Iterator ────────────────────────────────────────────
  struct IteratorBounds {
    std::optional<std::string> lower_bound;
    std::optional<std::string> upper_bound;
  };

  class IteratorImpl {
  public:
    virtual int seek_to_first() = 0;
    virtual int seek_to_last() = 0;
    virtual int lower_bound(const std::string &to) = 0;
    virtual int upper_bound(const std::string &after) = 0;
    virtual bool valid() const = 0;
    virtual int next() = 0;
    virtual int prev() = 0;
    virtual std::string key() const = 0;
    virtual bufferlist value() const = 0;
    virtual int status() const = 0;
    virtual ~IteratorImpl() = default;
  };

  class WholeSpaceIteratorImpl : public IteratorImpl {
  public:
    virtual std::pair<std::string, std::string> raw_key() = 0;
    virtual bool raw_key_is_prefixed(const std::string &prefix) = 0;
    virtual size_t key_size() { return 0; }
    virtual size_t value_size() { return 0; }
    /// Set persistent iterator bounds (pointers must remain valid
    /// for the iterator's lifetime). Converted to
    /// rocksdb::ReadOptions::iterate_{lower,upper}_bound.
    void set_iterate_lower_bound(const std::string *b) {
      iterate_lower_bound_ = b;
    }
    void set_iterate_upper_bound(const std::string *b) {
      iterate_upper_bound_ = b;
    }
  protected:
    const std::string *iterate_lower_bound_ = nullptr;
    const std::string *iterate_upper_bound_ = nullptr;
  };

  using Iterator = std::shared_ptr<IteratorImpl>;
  using WholeSpaceIterator = std::shared_ptr<WholeSpaceIteratorImpl>;
  using IteratorOpts = uint32_t;
  static constexpr IteratorOpts ITERATOR_NOCACHE = 1;

  // ── Factory ─────────────────────────────────────────────
  static std::unique_ptr<KeyValueDB> create(
      const std::string &type,
      const std::string &dir,
      std::map<std::string, std::string> options = {});

  // ── Lifecycle ───────────────────────────────────────────
  virtual int init(const std::string &options_str = "") = 0;
  virtual int open(std::ostream &out) = 0;
  virtual int create_and_open(std::ostream &out) = 0;
  virtual int open_read_only(std::ostream &out) { return -ENOTSUP; }
  virtual int repair(std::ostream &out) { return 0; }
  virtual void close() = 0;

  // ── Transaction ─────────────────────────────────────────
  virtual Transaction get_transaction() = 0;
  virtual int submit_transaction(Transaction t) = 0;
  virtual int submit_transaction_sync(Transaction t) {
    return submit_transaction(t);
  }

  // ── Point Read ──────────────────────────────────────────
  virtual int get(const std::string &prefix,
                  const std::set<std::string> &keys,
                  std::map<std::string, bufferlist> *out) = 0;
  virtual int get(const std::string &prefix,
                  const std::string &key,
                  bufferlist *out);

  // ── Iterator ────────────────────────────────────────────
  virtual WholeSpaceIterator get_wholespace_iterator(
      IteratorOpts opts = 0) = 0;
  virtual Iterator get_iterator(
      const std::string &prefix,
      IteratorOpts opts = 0,
      IteratorBounds bounds = IteratorBounds());

  // ── Merge Operator ──────────────────────────────────────
  virtual int set_merge_operator(
      const std::string &prefix,
      std::shared_ptr<MergeOperator> mop) {
    merge_ops_.emplace_back(prefix, std::move(mop));
    return 0;
  }

  // ── Maintenance ─────────────────────────────────────────
  virtual void compact() = 0;
  virtual void compact_async() { compact(); }
  virtual void compact_prefix(const std::string &prefix) { compact(); }
  virtual void compact_prefix_async(const std::string &prefix) {
    compact_async();
  }
  virtual void compact_range(const std::string &prefix,
                              const std::string &start,
                              const std::string &end) {
    (void)prefix; (void)start; (void)end; compact();
  }
  virtual void compact_range_async(const std::string &prefix,
                                    const std::string &start,
                                    const std::string &end) {
    compact_range(prefix, start, end);
  }
  virtual uint64_t get_estimated_size(
      std::map<std::string, uint64_t> &extra) = 0;

  virtual ~KeyValueDB() = default;

protected:
  /// Wrap a WholeSpaceIterator with a prefix filter
  Iterator make_iterator(const std::string &prefix,
                         WholeSpaceIterator w_iter,
                         IteratorBounds bounds = IteratorBounds{});

  const std::vector<std::pair<std::string,
      std::shared_ptr<MergeOperator>>> &
  get_merge_ops() const { return merge_ops_; }

private:
  std::vector<std::pair<std::string,
      std::shared_ptr<MergeOperator>>> merge_ops_;
};
```

### 3.3 PrefixIteratorImpl（基类内部实现）

`PrefixIteratorImpl` 继承 `IteratorImpl`，包裹一个 `WholeSpaceIterator`，
在 `valid()` 中调用 `raw_key_is_prefixed(prefix)` 确保没有越界到其他 prefix。
构造时将 bounds 信息（`lower_bound` / `upper_bound`）合并到编码 key 中，
并通过 `set_iterate_lower_bound` / `set_iterate_upper_bound` 传递给底层 RocksDB 迭代器。
`make_iterator()` 是基类提供的 protected 工厂方法，子类只需实现
`get_wholespace_iterator()` 即可获得 prefix 过滤迭代器。

## 4. 组件详情

### 4.1 MemDB

- **存储**: `std::map<std::string, std::string>`
- **Key 编码**: `prefix + '\0' + key`（与 Ceph 原始 MemDB 一致）
- **锁**: `std::mutex` 保护所有读写操作
- **事务**: `MDBTransactionImpl` 将操作记录到 `vector<Op>`，提交时加锁依次回放
- **迭代器**: 创建时获取 `std::map` 的 snapshot（排序后的 `vector<pair>`），并通过 `uint64_t seqno_` 检测写操作是否发生过。检测到变更后重新 snapshot 并重定位到之前的 key 位置。支持 `set_iterate_lower_bound` / `set_iterate_upper_bound`（自 `WholeSpaceIteratorImpl` 基类继承）。
- **Merge**: 实现完整的 merge 语义，查找已注册的 MergeOperator 并调用 `merge_nonexistent` / `merge`。未注册的 prefix 返回 `-ENOENT`。
- **持久化**: 无。重启后数据丢失
- **remove**: Ceph MemDB 的 `_save()`/`_load()` 文件持久化、`PerfCounters`、`btree_map` 支持均移除

### 4.2 RocksDBStore

- **编译**: 需要 `-Wno-unused-parameter`（RocksDB 接口回调中大量未使用参数）
- **存储**: 单个 `rocksdb::DB` 实例，不启用 ColumnFamily 分片
- **Key 编码**: `prefix + '\0' + key`（与 MemDB 一致，确保两者 Key 空间可互相转换）
- **ColumnFamily**: 仅使用默认 CF（`rocksdb::DB::Open` 的单 CF 模式）。不实现 Ceph 的 `parse_sharding_def` / hash 分片逻辑。
- **Merge**: 通过 `RocksDBMergeAdapter`（继承 `rocksdb::MergeOperator`）将 `KeyValueDB::MergeOperator` 分发到各 prefix。`set_merge_operator()` 必须在 `open()` / `create_and_open()` 之前调用，否则返回 `-EROFS`。
- **IteratorBounds**: 两层分离：
  - `WholeSpaceIteratorImpl` 基类持有 `const std::string* iterate_lower_bound_` / `iterate_upper_bound_`
  - `PrefixIteratorImpl` 构造时通过 `set_iterate_lower_bound` / `set_iterate_upper_bound` 将 bounds 注入基类
  - `RDBWholeSpaceIteratorImpl` 在每次 `seek` / `lower_bound` / `upper_bound` 时调用 `apply_bounds()`，将 `std::string*` 转为 `rocksdb::Slice*` 写入 `ReadOptions`
- **open_read_only**: 通过 `rocksdb::DB::OpenForReadOnly()` 实现
- **repair**: 通过 `rocksdb::RepairDB()` 实现
- **compact_prefix / compact_range**: 编码 key 后调用 `CompactRange()` 限定 `[start, end)` Slice
- **DeleteRange 阈值**: 通过构造函数 options `delete_range_threshold` 配置，预留用于小范围逐条删除以减少宽墓碑开销，当前初始版本始终使用 `DeleteRange`
- **Init 选项解析**: `init(options_str)` 解析 `key=val;key=val` 格式字符串，支持设置 `write_buffer_size`、`max_write_buffer_number`、`max_bytes_for_level_base`、`target_file_size_base` 等 RocksDB 参数
- **ITERATOR_NOCACHE**: 通过 `rocksdb::ReadOptions::fill_cache = false` 实现
- **Compact**: 调用 `rocksdb::DB::CompactRange()`，同步版本
- **submit_transaction_sync**: 设置 `rocksdb::WriteOptions::sync = true` 实现 WAL 同步
- **key_size / value_size**: 委托给 `rocksdb::Iterator::key().size()` / `value().size()`
- **移除**:
  - `CompactThread` 异步压缩
  - `BinnedLRUCache` / `PriorityCache` 集成
  - `Resharding` 相关逻辑
  - `CephContext` / `PerfCounters` / `Formatter` 依赖
  - ColumnFamily sharding 及多 CF 管理
  - 自定义 block cache 配置

### 4.3 MergeOperator 实现

以 `Int64ArrayMergeOperator` 为例：

```cpp
class Int64ArrayMergeOperator : public MergeOperator {
  const char *name() const override { return "int64_array"; }
  void merge_nonexistent(const char *rdata, size_t rlen,
                          std::string *new_value) override {
    *new_value = std::string(rdata, rlen);  // exist = delta
  }
  void merge(const char *ldata, size_t llen,
             const char *rdata, size_t rlen,
             std::string *new_value) override {
    // Element-wise int64 addition
    auto existing = reinterpret_cast<const int64_t*>(ldata);
    auto delta   = reinterpret_cast<const int64_t*>(rdata);
    size_t count = std::min(llen, rlen) / sizeof(int64_t);
    std::vector<int64_t> result(count);
    for (size_t i = 0; i < count; i++)
      result[i] = existing[i] + delta[i];
    new_value->assign(reinterpret_cast<char*>(result.data()),
                      count * sizeof(int64_t));
  }
};
```

`XorMergeOperator` 做位级别的异或操作用于 BitmapFreelistManager：

```cpp
class XorMergeOperator : public MergeOperator {
  const char *name() const override { return "xor"; }
  void merge_nonexistent(const char *rdata, size_t rlen,
                          std::string *new_value) override {
    *new_value = std::string(rdata, rlen);
  }
  void merge(const char *ldata, size_t llen,
             const char *rdata, size_t rlen,
             std::string *new_value) override {
    size_t len = std::min(llen, rlen);
    new_value->resize(len);
    for (size_t i = 0; i < len; i++)
      (*new_value)[i] = ldata[i] ^ rdata[i];
  }
};
```

## 5. IO 流程

### 5.1 写入流程

```plaintext
BlueStore::_txc_apply_kv()
        │
        ▼
txc->t->set(PREFIX_OBJ, onode_key, onode_bl)     ← onode 更新
txc->t->set(PREFIX_COLL, cid, cnode_bl)           ← collection 更新
txc->t->merge(PREFIX_STAT, stat_key, delta_bl)    ← statfs 增量
txc->t->set(PREFIX_OMAP, omap_key, omap_bl)       ← omap 更新
txc->t->rm_range_keys(PREFIX_OMAP, head, tail)    ← omap 范围删除
fm->allocate(release, t)  → t->merge("b", k, bl)  ← 位图分配
fm->release(alloc, t)     → t->merge("b", k, bl)  ← 位图释放
        │
        ▼
KeyValueDB::submit_transaction(t)
        │
  ┌─────┴──────┐
  │            │
  ▼            ▼
RocksDB      MemDB
WriteBatch   lock + replay ops
-> Write()   -> map insert/erase
```

**RocksDBStore 提交路径** (`submit_transaction`):

1. 将 `rocksdb::WriteBatch` 通过 `db->Write(woptions, &batch)` 提交
2. Merge 操作通过 RocksDB 的 `Merge(...)` API 写入 batch

**MemDB 提交路径** (`submit_transaction`):

1. 加锁 `m_lock`
2. 遍历 `MDBTransactionImpl.ops`，对每条操作调用 `_setkey` / `_rmkey` / `_merge`
3. 释放锁

### 5.2 读取流程

```plaintext
BlueStore::_onode_map_lookup(key)
        │
        ▼
KeyValueDB::get(PREFIX_OBJ, key, &bl)
        │
  ┌─────┴──────┐
  │            │
  ▼            ▼
RocksDB      MemDB
db->Get()    map.find()
```

### 5.3 扫描流程

```plaintext
BlueStore::generate_stats()
        │
        ▼
KeyValueDB::get_wholespace_iterator(opts = ITERATOR_NOCACHE)
        │
        ▼
it->seek_to_first()
while (it->valid()) {
    auto [prefix, key] = it->raw_key();   ← 获取 prefix + 内部 key
    auto value = it->value();
    it->next();
}
```

### 5.4 启动恢复流程

```plaintext
BlueStore::_open_db()
  db = KeyValueDB::create("rocksdb", path, opts)
  db->set_merge_operator("T", Int64ArrayMergeOperator)
  FreelistManager::setup_merge_operators(db, "bitmap")    ← 设置 "b" 的 XOR merge
  db->create_and_open(out)                                 ← KV 存储初始化
        │
        ▼
BlueStore::_open_fm()
  遍历 PREFIX_ALLOC 或 PREFIX_ALLOC_BITMAP 重建 freelist
        │
        ▼
BlueStore::_open_collections()
  遍历 PREFIX_COLL 重建所有 Collection
        │
        ▼
BlueStore::_replay()
  get_iterator(PREFIX_DEFERRED) 遍历重放未完成的 deferred 写
```

## 6. 使用示例

### 6.1 创建及打开

```cpp
auto db = KeyValueDB::create("rocksdb", "/var/lib/clab/store", {});
// 必须在 open 前注册 MergeOperator
db->set_merge_operator("T",
    std::make_shared<Int64ArrayMergeOperator>());

int r = db->create_and_open(std::cerr);
```

### 6.2 写入

```cpp
auto t = db->get_transaction();

// Point write
bufferlist val;
val.append("hello");
t->set("O", "obj_key", val);

// Range delete
t->rm_range_keys("M", "head_prefix", "tail_prefix");

// Merge (statfs delta)
bufferlist delta;
encode(int64_t(1), delta);
t->merge("T", "global", delta);

db->submit_transaction_sync(t);
```

### 6.3 读取

```cpp
bufferlist value;
int r = db->get("O", "obj_key", &value);
if (r == 0) {
    // value contains the data
}
```

### 6.4 扫描

```cpp
// Prefix-scoped iteration with bounds
auto it = db->get_iterator("O", 0,
    KeyValueDB::IteratorBounds{"start_key", "end_key"});
it->lower_bound("start_key");
while (it->valid()) {
    process(it->key(), it->value());
    it->next();
}

// Full-space scan (e.g. debug)
auto wit = db->get_wholespace_iterator(
    KeyValueDB::ITERATOR_NOCACHE);
wit->seek_to_first();
while (wit->valid()) {
    auto [prefix, key] = wit->raw_key();
    fmt::print("{}:{}\n", prefix, key);
    wit->next();
}
```

### 6.5 调试模式

```cpp
// 完全在内存中运行，不产生任何磁盘写入
auto db = KeyValueDB::create("memdb", "/tmp/unused", {});
db->create_and_open(std::cerr);
// 所有操作与 RocksDB 模式语义完全一致
```
