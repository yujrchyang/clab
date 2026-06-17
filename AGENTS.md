# clab

## Build & Toolchain

- **Build system:** CMake (in-source build dir at `build/`)
- **Formatter:** `.clang-format` (Google-based, 4-space indent, format-on-save via clangd)
- **LSP:** clangd (`--compile-commands-dir=${workspaceFolder}`)
- **Compiler artifacts** are gitignored (`.o`, `.so`, `.a`, `build/`, `compile_commands.json`, etc.)

## Headers

`IncludeBlocks: Preserve` — clang-format preserves user-defined groups
(separated by blank lines). Within each group, `IncludeCategories` define
the sort priority:

| Priority | Pattern | Example headers |
|----------|---------|-----------------|
| 1 | `<` + `.h` | `<fcntl.h>`, `<unistd.h>`, `<gtest/gtest.h>`, `<libaio.h>` |
| 2 | `<` + no `.h` or `.hpp` | `<vector>`, `<cstdlib>`, `<memory>` |
| 3 | `<` + `.hpp` | `<boost/container/small_vector.hpp>` |
| 4 | `""` | `"blk/aio.h"`, `"common/buffer.h"` |

Within each priority, headers are sorted alphabetically. To create or
remove a group, add or remove the blank line between blocks. Includes
never migrate across blank lines.

## Progress

### Goal
Implement the kv abstraction layer for clab (RocksDBStore backend, MemDB debug backend), modeled after Ceph's `src/kv/*`.

### Key Context
- `TOPNSPC` macro defined in `common/common_fwd.h` → expands to `clab`
- `bufferlist` = `clab::bufferlist`
- Key encoding: `prefix + '\0' + inner_key` (both backends, Ceph-compatible)
- RocksDB version: v7.10.2 (via submodule `third_party/rocksdb/`)
- Ceph reference: `/home/yujrchyang/opensrc/ceph/src/kv/*` and `src/os/bluestore/*`
- kv/ compiles as SHARED library (`libkv.so`), links `common` (PUBLIC) + `RocksDB::RocksDB` (PRIVATE)
- `kv/CMakeLists.txt` uses `-Wno-unused-parameter` due to RocksDB callback signatures

### Serialization: `common/denc.h` (DENC framework)

`common/denc.h` 是一个基于 traits 模板的编译期序列化框架，统一通过 `denc(o, p)` 入口调度 encode/decode。使用规则：

- **POD 定长类型**（uint64_t, int32_t 等用于 FreelistManager meta）：直接用 `bl.append((const char*)&v, sizeof(v))` / `p.copy(...)` 即可，不需要 `denc.h`
- **简单容器**（`vector<T>` 等）或 `string`：直接 `#include "common/denc.h"`，模板已内置支持
- **自定义复合类型**（onode_t、extent_map 等复杂的 BlueStore 结构化数据）：用 `WRITE_CLASS_DENC(T)` 宏或 `DENC(Type, v, p)` 宏实现成员级序列化，在 `topnspc` 命名空间内使用

用法示例：
```cpp
#include "common/denc.h"

// 方式 A: DENC 宏（推荐，结构体内联）
struct Extent {
    uint64_t offset;
    uint32_t length;
    DENC(Extent, v, p) {
        DENC_START(1, 1, p);
        denc(v.offset, p);
        denc(v.length, p);
        DENC_FINISH(p);
    }
};

// 方式 B: WRITE_CLASS_DENC（traits 特化）
struct Blob {
    uint64_t id;
    // 需定义 encode() / decode() / bound_encode() 成员
};
WRITE_CLASS_DENC(Blob);  // 在 topnspc 内

// 使用
bufferlist bl;
encode(extent, bl);       // denc(o, p) 顶层包装
Extent e;
decode(e, bl.cbegin());   // denc(o, p) 顶层包装
```

简单原则：**只有 uint64/int/string 等简单字段时直接手动序列化；出现嵌套结构体组合（onode_t 含多个成员 + map + vector）时上 DENC**。

### Done
- **RocksDBStore implementation:**
  - `RDBTransactionImpl`: builds `rocksdb::WriteBatch`, submitted via `db_->Write()` / `db_->Write({.sync=true})`
  - `RDBWholeSpaceIteratorImpl`: wraps `rocksdb::Iterator`, supports `ITERATOR_NOCACHE` via `fill_cache=false`, applies bounds from `WholeSpaceIteratorImpl::iterate_{lower,upper}_bound_` as `rocksdb::Slice*`
  - `RocksDBMergeAdapter`: `rocksdb::MergeOperator` adapter dispatching to `kv::MergeOperator` by key prefix
  - `rm_single_key`: uses `SingleDelete`
  - `rmkeys_by_prefix`: uses `WriteBatch::DeleteRange(prefix+'\0', prefix+'\xff')`
  - `open_read_only`: uses `rocksdb::DB::OpenForReadOnly`
  - `repair`: uses `rocksdb::RepairDB`
  - `compact_prefix` / `compact_range`: encode key and call `CompactRange` with Slice bounds
  - Factory: `create("rocksdb", dir, opts)` returns `RocksDBStore`
  - `set_merge_operator`: overridden to return `-EROFS` if `db_ != nullptr`
  - `init(options_str)`: parses `key=val;key=val` style string for RocksDB options
  - `delete_range_threshold`: configurable via options map, reserved for small-range optimization
- **MemDB implementation:**
  - `MDBTransactionImpl::Op`: refactored with explicit `end` field for `rm_range_keys` (removed reuse of `value`)
  - `_merge()`: uses `clab_assert(mop)` instead of silent return
  - Iterator invalidation: `uint64_t seqno_` incremented on every mutation; `MDBWholeSpaceIteratorImpl` detects stale seqno on each seek/lower_bound/upper_bound and rebuilds snapshot from `std::map` under lock
- **PrefixIteratorImpl:**
  - Removed `skip_to_next_valid()`/`skip_to_prev_valid()` direction-check bug
  - Constructs `seek_lower_bound_`/`seek_upper_bound_` from prefix + bounds and passes to underlying iterator via `set_iterate_lower_bound` / `set_iterate_upper_bound`
- **Base interface (`kv/key_value_db.h`):**
  - `open_read_only`, `repair`, `compact_prefix`, `compact_prefix_async`, `compact_range`, `compact_range_async` added as virtual-with-default
  - `WholeSpaceIteratorImpl`: added `set_iterate_lower_bound(const std::string*)` / `set_iterate_upper_bound(const std::string*)` with protected `iterate_{lower,upper}_bound_` pointers
  - `key_size()` / `value_size()` added to `WholeSpaceIteratorImpl`
  - `make_iterator` now takes `IteratorBounds` parameter
  - `set_merge_operator` is no longer pure virtual (has default storage in `merge_ops_`)
  - `get_merge_ops()` protected accessor for subclasses
- **Tests:** Split `test_librocksdb.cc` (23 raw RocksDB tests) from `test_rocksdb.cc` (21 RocksDBStore tests); 36 MemDB tests in `test_memdb.cc`; all 80 pass
- **Ceph evaluation:** Compared `src/kv/*` (KeyValueDB, RocksDBStore, MemDB) and `src/os/bluestore/*` (KV usage patterns), identified 12 improvement items in 8 categories — all resolved

### Key Decisions
- Single default ColumnFamily (no hash sharding, no `parse_sharding_def`)
- `set_merge_operator` must be called before `open()` / `create_and_open()` for RocksDBStore
- `close()` null-checks `db_` before delete, sets to `nullptr`, resets adapter
- DeleteRange threshold: configurable via `delete_range_threshold`, default 0 → always use DeleteRange
- MemDB iterator invalidation: seqno-based, automatically rebuilds snapshot on detection of concurrent writes
- Iterator bounds two-layer separation: `WholeSpaceIteratorImpl` stores `const std::string*`, backend converts to native type (`rocksdb::Slice*`) at seek time

### Next Steps
- BlueStore implementation using kv interface
- Potential additions when needed: `set_cache_size()`, `get_property()`, `get_approximate_size()` (Ceph-compatible helpers, not blocking)

### Relevant Files
- `kv/key_value_db.h`: Abstract base (TransactionImpl, IteratorImpl, WholeSpaceIteratorImpl, PrefixIteratorImpl, KeyValueDB)
- `kv/key_value_db.cc`: PrefixIteratorImpl, KeyValueDB factory/create
- `kv/mem/mem_db.h` / `kv/mem/mem_db.cc`: MemDB backend
- `kv/rocksdb/rocksdb_store.h` / `kv/rocksdb/rocksdb_store.cc`: RocksDBStore backend
- `kv/merge_op/`: MergeOperator abstract base, Int64ArrayMergeOperator, XorMergeOperator
- `kv/CMakeLists.txt`: builds libkv.so (SHARED), links common (PUBLIC) + RocksDB::RocksDB (PRIVATE), uses `-Wno-unused-parameter`
- `tests/kv/test_librocksdb.cc`: 23 raw RocksDB tests
- `tests/kv/test_rocksdb.cc`: 21 RocksDBStore tests
- `tests/kv/test_memdb.cc`: 36 MemDB tests
- `tests/kv/CMakeLists.txt`: test targets linking kv + RocksDB + clab_test_helpers + GTest
- `docs/kv-design.md`: full design specification
- `docs/freelist-manager-design.md`: FreelistManager/BitmapFreelistManager design analysis (Ceph reference: `src/os/bluestore/FreelistManager.*`, `BitmapFreelistManager.*`)

### Next Steps (updated)
- Implement FreelistManager / BitmapFreelistManager in `bluestore/` layer
- Implement Allocator (StupidAllocator / BitmapAllocator)
- BlueStore core engine with KV + FreelistManager + Allocator integration
