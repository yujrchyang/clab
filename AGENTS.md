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
Implement the kv abstraction layer (RocksDBStore + MemDB) and bluestore layer (FreelistManager + Allocator) for clab, modeled after Ceph's `src/kv/*` and `src/os/bluestore/*`.

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
- **Allocator implementation:**
  - `Allocator` abstract base with `create()` factory, `init_add_free()`, `allocate()`, `release()`, `get_fragmentation()`, `get_alloc_stats()` interface
  - `AvlAllocator`: interval-tree (AVL) based via `range_seg_tree_t`, exact match allocation, `_spillover_range()` cap mechanism
  - `BitmapAllocator`: 2-level bitmap (`bdev_block_count` × `bitmap_granularity`), `ffs`/`ffz` scan, affinity hint via arena-weighted round-robin
  - `HybridAllocator`: wraps AvlAllocator + BitmapAllocator child, `_add_to_tree()` override to claim-free adjacent extents from bitmap before AVL insert
   - `Allocator::create()` factory with `"stupid"` (AvlAllocator), `"bitmap"`, `"hybrid"` type strings
- **Tests:** 21 AvlAllocator tests, 33 BitmapAllocator tests, 19 HybridAllocator tests — all pass

### Key Decisions
- Single default ColumnFamily (no hash sharding, no `parse_sharding_def`)
- `set_merge_operator` must be called before `open()` / `create_and_open()` for RocksDBStore
- `close()` null-checks `db_` before delete, sets to `nullptr`, resets adapter
- DeleteRange threshold: configurable via `delete_range_threshold`, default 0 → always use DeleteRange
- MemDB iterator invalidation: seqno-based, automatically rebuilds snapshot on detection of concurrent writes
- Iterator bounds two-layer separation: `WholeSpaceIteratorImpl` stores `const std::string*`, backend converts to native type (`rocksdb::Slice*`) at seek time
- BitmapFreelistManager: `create()` allocates block 0 at mkfs time (caller must not double-allocate)
- Bitmap key encoding: 8 bytes big-endian uint64_t (memcmp-compatible, matches Ceph `_key_encode_u64`)
- BitmapFreelistManager links as STATIC library `libbluestore.a`
- bluestore/ subdirectory added to root CMakeLists.txt
- Allocator::create() type string `"stupid"` maps to AvlAllocator (not the original Ceph StupidAllocator)
- HybridAllocator allocation strategy: always try AVL first, bitmap as fallback (simplified from Ceph's conditional strategy)
- `_add_to_tree()` claim-free optimization reclaims adjacent free extents from bitmap child before AVL insertion

### Next Steps
- BlueStore core engine with KV + FreelistManager + Allocator integration

### Relevant Files
- `kv/key_value_db.h`: Abstract base (TransactionImpl, IteratorImpl, WholeSpaceIteratorImpl, PrefixIteratorImpl, KeyValueDB)
- `kv/key_value_db.cc`: PrefixIteratorImpl, KeyValueDB factory/create
- `kv/mem/mem_db.h` / `kv/mem/mem_db.cc`: MemDB backend
- `kv/rocksdb/rocksdb_store.h` / `kv/rocksdb/rocksdb_store.cc`: RocksDBStore backend
- `kv/merge_op/`: MergeOperator abstract base, Int64ArrayMergeOperator, XorMergeOperator
- `kv/CMakeLists.txt`: builds libkv.so (SHARED), links common (PUBLIC) + RocksDB::RocksDB (PRIVATE), uses `-Wno-unused-parameter`
- `bluestore/freelist_manager.h`: FreelistManager abstract base
- `bluestore/bitmap_freelist_manager.h` / `bluestore/bitmap_freelist_manager.cc`: BitmapFreelistManager implementation
- `bluestore/CMakeLists.txt`: builds libbluestore.a (STATIC)
- `bluestore/allocator.h` / `bluestore/allocator.cc`: Allocator abstract base + factory
- `bluestore/avl_allocator.h` / `bluestore/avl_allocator.cc`: AvlAllocator (interval-tree)
- `bluestore/bitmap_allocator.h` / `bluestore/bitmap_allocator.cc`: BitmapAllocator (2-level bitmap)
- `bluestore/hybrid_allocator.h` / `bluestore/hybrid_allocator.cc`: HybridAllocator (AVL + bitmap)
- `tests/kv/test_librocksdb.cc`: 23 raw RocksDB tests
- `tests/kv/test_rocksdb.cc`: 21 RocksDBStore tests
- `tests/kv/test_memdb.cc`: 36 MemDB tests
- `tests/bluestore/test_bitmap_freelist_manager.cc`: 15 BitmapFreelistManager tests
- `tests/bluestore/test_avl_allocator.cc`: 21 AvlAllocator tests
- `tests/bluestore/test_bitmap_allocator.cc`: 33 BitmapAllocator tests
- `tests/bluestore/test_hybrid_allocator.cc`: 19 HybridAllocator tests
- `tests/kv/CMakeLists.txt`: test targets linking kv + RocksDB + clab_test_helpers + GTest
- `docs/design/keyvalue-db.md`: full design specification
- `docs/design/freelist-manager.md`: FreelistManager/BitmapFreelistManager design analysis (Ceph reference: `src/os/bluestore/FreelistManager.*`, `BitmapFreelistManager.*`)

### Next Steps (updated)
- ~~Implement FreelistManager / BitmapFreelistManager in `bluestore/` layer~~ ✓
- ~~Implement Allocator (AvlAllocator / BitmapAllocator / HybridAllocator)~~ ✓
- BlueStore core engine with KV + FreelistManager + Allocator integration
