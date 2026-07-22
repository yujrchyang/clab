# BlueFS — 用户态文件系统

> **实现状态**: 已实现（Phase 1.1–1.11，55 tests）

## 1. 概述

BlueFS 是一个为 RocksDB 设计的轻量级用户态文件系统，运行在裸块设备之上，不依赖内核 VFS。它替代了传统文件系统（如 XFS/ext4）作为 RocksDB 的存储后端，消除了双文件系统（RocksDB on XFS on BlueStore）的路径开销。

### 1.1 为什么要 BlueFS

```plaintext
传统方案：                        BlueStore 方案：
  Application                      Application
       │                                │
       ▼                                ▼
  LevelDB/RocksDB                  LevelDB/RocksDB
       │                                │
       ▼                                ▼
  XFS / ext4                     BlueFS (用户态 FS)
       │                                │
       ▼                                ▼
  Linux Block Layer              BlueStore Block Device
       │                                │
       ▼                                ▼
  块设备 (NVMe/SSD)                块设备 (NVMe/SSD)
```

BlueFS 消除了 **XFS → Block Layer → BlueStore** 之间的上下文切换和缓存层叠（double caching），同时支持与 BlueStore **共享同一块设备上的 Allocator**，避免空间分区浪费。

### 1.2 功能范围

| 功能 | 说明 |
| --- | --- |
| 目录操作 | create/remove directory, link/unlink file |
| 文件操作 | open/create/read/write/rename/unlink |
| 顺序写 | append-only 写入模式，支持 buffer + flush |
| 随机读 | 通过 prefetch buffer 或直接随机读 |
| 元数据日志 | 写前日志 (journal)，保证 crash consistency |
| 日志压缩 | **异步压缩**：边写入边压缩，不影响前台 IO |
| 多设备 | WAL / DB / Slow 三级设备 |
| 智能卷选择 | **RocksDBBlueFSVolumeSelector**：按 RocksDB 级别和设备容量智能放置 |
| 空间共享 | 与 BlueStore 共享 Allocator |

### 1.3 约束条件

| 维度 | 说明 |
| --- | --- |
| 操作系统 | 仅 Linux (x86\_64 + AArch64) |
| 块设备 | 通过 `blk::BlockDevice` 抽象层访问 |
| 定制化顺序 | 不支持任意位置随机写（仅 append + truncate） |
| 无 POSIX 兼容 | 不实现 `open/read/write` 系统调用接口 |
| 配置方式 | 通过 `BlueFSConfig` 结构体传入，提供默认值 + 文件加载 |

### 1.4 与 BlueStore 的关系

BlueFS 通常内嵌在 BlueStore 中，RocksDB 将其 WAL 和 SST 文件存放在 BlueFS 上。两者共享同一块设备时，通过 `bluefs_shared_alloc_context_t` 共用 Allocator：

```plaintext
         BlueStore                    BlueFS
            │                           │
  ┌─────────┴──────────┐      ┌─────────┴──────────┐
  │  Allocator         │◄────►│  Allocator         │
  │  (primary, shared) │      │  (primary, shared) │
  └────────────────────┘      └────────────────────┘
            │                           │
         BlockDevice                BlockDevice
         (主设备)                    (WAL/DB/主设备)
```

---

## 2. 架构总览

### 2.1 组件架构

```plaintext
┌──────────────────────────────────────────────────────────────────┐
│                           BlueFS                                 │
│                                                                  │
│  ┌────────────────────────────────────────────────────────┐      │
│  │                  In-Memory State                       │      │
│  │  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐ │      │
│  │  │ nodes.dir_map│  │nodes.file_map│  │ dirty.files   │ │      │
│  │  │ (dirname→Dir)│  │ (ino→File)   │  │ (seq→files)   │ │      │
│  │  └──────────────┘  └──────────────┘  └───────────────┘ │      │
│  └────────────────────────────────────────────────────────┘      │
│                                                                  │
│  ┌──────────────────────┐  ┌────────────────────────────────┐    │
│  │   Log/Journal        │  │  RocksDBBlueFSVolumeSelector   │    │
│  │   - bluefs_fnode_t   │  │  - Place by RocksDB level      │    │
│  │   - seq, transaction │  │  - Alloc by dev capacity ratio │    │
│  │   - dirty tracking   │  │  - Spill to Slow when DB full  │    │
│  │   - async compact    │  │                                │    │
│  └──────────────────────┘  └────────────────────────────────┘    │
│                                                                  │
│  ┌──────────────────────── Per-Device Layer ──────────────────┐  │
│  │                                                            │  │
│  │  BDEV_WAL (0)    BDEV_DB (1)    BDEV_SLOW (2)              │  │
│  │  ┌──────────┐   ┌──────────┐   ┌──────────┐                │  │
│  │  │Allocator │   │Allocator │   │Allocator │                │  │
│  │  │BlockDev  │   │BlockDev  │   │BlockDev  │                │  │
│  │  └──────────┘   └──────────┘   └──────────┘                │  │
│  └────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 磁盘数据结构

#### 2.2.1 超级块 (`bluefs_super_t`)

存储在 DB 设备偏移 `4096`（第 2 个 4KB 块），第一个 4KB 块保留给块设备标签。超级块使用 CRC-32C 校验，`_write_super` / `_read_super` 在写入和读取时分别计算和验证校验和：

```cpp
struct bluefs_super_t {
    uuid_d uuid;                     // 文件系统 UUID
    uuid_d osd_uuid;                 // 所属 OSD UUID
    uint64_t version;                // 版本计数器（每次写入递增）
    uint32_t block_size = 4096;      // 块大小
    bluefs_fnode_t log_fnode;        // 日志文件的 fnode (ino = 1)
};
```

#### 2.2.2 物理 extent (`bluefs_extent_t`)

```cpp
struct bluefs_extent_t {
    uint64_t offset = 0;    // 物理磁盘偏移
    uint32_t length = 0;    // 长度（字节）
    uint8_t  bdev;          // 所属设备索引 (BDEV_WAL/BDEV_DB/BDEV_SLOW)
};
```

文件中相邻的同设备 extent 自动合并。

#### 2.2.3 文件 inode (`bluefs_fnode_t`)

```cpp
struct bluefs_fnode_t {
    uint64_t ino;                                    // inode 编号 (1 = 日志文件)
    uint64_t size;                                   // 逻辑文件大小
    utime_t  mtime;                                  // 修改时间
    std::vector<bluefs_extent_t> extents;            // 物理 extent 列表
    std::vector<uint64_t> extents_index;             // extent 的逻辑偏移索引
    uint64_t allocated;                              // 总分配字节数
    uint64_t allocated_committed;                    // 上次提交时的 allocated

    // 增量版本（用于日志）
    struct bluefs_fnode_delta_t {
        uint64_t ino;
        uint64_t size;
        utime_t  mtime;
        uint64_t offset;      // 新增 extent 的起始逻辑偏移
        std::vector<bluefs_extent_t> extents;
    };

    bluefs_fnode_delta_t make_delta() const;
    void recalc_allocated();
    void reset_delta();
    void append_extent(const bluefs_extent_t &e);
};
```

- `ino = 1` 保留给日志文件
- `extents_index` 是预计算的逻辑偏移索引，用于 `seek(offset, &x_off)` 的二分查找
- `allocated_committed` 跟踪已提交到日志的大小，`make_delta()` 仅返回增量部分的 extent

#### 2.2.4 日志事务 (`bluefs_transaction_t`)

```cpp
struct bluefs_transaction_t {
    uuid_d uuid;               // 文件系统 UUID
    uint64_t seq;              // 单调递增序列号
    bufferlist op_bl;          // 编码后的操作列表

    enum op_t : uint8_t {
        OP_NONE          = 0,
        OP_INIT          = 2,  // 初始（空文件系统）标记
        OP_DIR_LINK      = 5,  // 设置目录条目
        OP_DIR_UNLINK    = 6,  // 删除目录条目
        OP_DIR_CREATE    = 7,  // 创建目录
        OP_DIR_REMOVE    = 8,  // 删除目录
        OP_FILE_UPDATE   = 9,  // 完整 fnode 更新
        OP_FILE_REMOVE   = 10, // 删除文件
        OP_JUMP          = 11, // 跳转 seq + offset（日志压缩用）
        OP_JUMP_SEQ      = 12, // 仅跳转 seq（异步压缩用）
        OP_FILE_UPDATE_INC = 13, // 增量 fnode 更新
    };
};
```

> **cxxlab 简化**: 移除 `OP_ALLOC_ADD`/`OP_ALLOC_RM`（已废弃的历史操作）。日志写入路径使用 CRC-32C 校验和块边界对齐，确保 crash consistency。

### 2.3 设备管理

BlueFS 最多管理 3 个设备：

| 设备 | 索引 | 用途 | 可选性 |
| --- | --- | --- | --- |
| `BDEV_WAL` | 0 | RocksDB WAL（高速设备如 NVRAM/Optane） | 可选 |
| `BDEV_DB` | 1 | RocksDB SST + 元数据（NVMe/SSD） | 必选 |
| `BDEV_SLOW` | 2 | 冷数据 spillover（HDD/SATA SSD） | 可选 |

设备可以独占或共享。当只有主块设备时，`BDEV_DB` 独占主设备上的部分预留空间。

---

## 3. 核心数据结构

### 3.1 BlueFS 类

```cpp
class BlueFS {
public:
    BlueFS(const BlueFSConfig &cfg);
    ~BlueFS();

    // 生命周期
    int mkfs(uuid_d osd_uuid);
    int mount();
    void umount(bool avoid_compact = false);
    bool is_mounted() const;

    // 设备管理
    int add_block_device(unsigned id, const std::string &path,
                         bool trim = false, uint64_t reserved = 0);
    int add_shared_device(unsigned id, const std::string &path,
                          bluefs_shared_alloc_context_t *shared_alloc,
                          bool trim = false, uint64_t reserved = 0);

    // 目录操作
    int mkdir(const std::string &dirname);
    int rmdir(const std::string &dirname);
    int lookup(const std::string &dirname, const std::string &filename);

    // 文件操作
    int open_for_write(const std::string &dirname,
                       const std::string &filename,
                       FileWriter **h, bool overwrite = false);
    int open_for_read(const std::string &dirname,
                      const std::string &filename,
                      FileReader **h, bool random = false);
    void close_writer(FileWriter *h);
    void close_reader(FileReader *h);

    // 读写
    int read(FileReader *h, uint64_t offset, uint64_t length,
             bufferlist *outbl);
    int read_random(FileReader *h, uint64_t offset, uint64_t length,
                    char *out);
    int write(FileWriter *h, bufferlist &bl, uint32_t len);
    int append_try_flush(FileWriter *h, const char *buf, uint32_t len);
    int flush(FileWriter *h);
    int fsync(FileWriter *h);

    // 文件管理
    int truncate(const std::string &dirname,
                 const std::string &filename,
                 uint64_t size);
    int unlink(const std::string &dirname,
               const std::string &filename);
    int rename(const std::string &old_dirname,
               const std::string &old_filename,
               const std::string &new_dirname,
               const std::string &new_filename);
    int stat(const std::string &dirname,
             const std::string &filename,
             struct stat *st);
    uint64_t get_total(const std::string &dirname);
    uint64_t get_free(const std::string &dirname);

    // 元数据同步
    int sync_metadata(bool avoid_compact = false);

    // 卷选择器
    void set_volume_selector(BlueFSVolumeSelector *vs);

private:
    struct File;
    struct Dir;
    struct FileWriter;
    struct FileReader;

    BlueFSConfig cfg_;

    // 设备
    std::vector<BlockDevice*> bdev_;
    std::vector<Allocator*> alloc_;
    std::vector<uint64_t> alloc_size_;
    std::vector<uint64_t> block_reserved_;

    // 共享分配（与 BlueStore）
    bluefs_shared_alloc_context_t *shared_alloc_ = nullptr;
    unsigned shared_alloc_id_ = unsigned(-1);

    // 内存状态
    struct {
        std::mutex lock;
        std::map<std::string, DirRef> dir_map;
        std::unordered_map<uint64_t, FileRef> file_map;
    } nodes_;

    // 日志
    struct {
        std::mutex lock;
        uint64_t seq_live_ = 1;
        FileWriter *writer_ = nullptr;
        bluefs_transaction_t t_;
    } log_;

    // 脏文件追踪
    struct {
        std::mutex lock;
        uint64_t seq_stable_ = 0;
        uint64_t seq_live_ = 1;
        std::map<uint64_t, dirty_file_list_t> files_;
        std::vector<interval_set<uint64_t>> pending_release_;
    } dirty_;

    // 卷选择器
    std::unique_ptr<BlueFSVolumeSelector> vselector_;

    // 异步压缩状态
    std::atomic<bool> log_is_compacting_{false};
    std::atomic<bool> log_forbidden_to_expand_{false};
    std::mutex compact_lock_;
    std::condition_variable compact_cond_;
};
```

### 3.2 BlueFSConfig

```cpp
struct BlueFSConfig {
    // 设备参数
    std::string wal_device_path;                // WAL 设备路径（可选）
    std::string db_device_path;                 // DB 设备路径（必选）
    std::string slow_device_path;               // Slow 设备路径（可选）

    // 分配
    uint64_t alloc_size = 1048576;              // 1MB，独占设备分配单元
    uint64_t shared_alloc_size = 1048576;       // 1MB，共享设备分配单元
    uint64_t max_log_runway = 4194304;          // 4MB，日志扩展量
    uint64_t min_log_runway = 1048576;          // 1MB，最小日志余量

    // 写入
    uint64_t min_flush_size = 524288;           // 512KB，触发刷写阈值
    bool buffered_io = false;                   // 使用缓冲 I/O
    bool sync_write = false;                    // 使用同步写入（非 AIO）

    // 日志压缩
    uint64_t log_compact_min_size = 16777216;   // 16MB，触发压缩的最小日志大小
    double log_compact_min_ratio = 2.0;         // 触发压缩的日志比

    // 读取
    uint64_t max_prefetch = 1048576;            // 1MB，读取预取大小

    static BlueFSConfig load_from_file(const std::string &path);
};
```

### 3.3 内部类型

#### File

```cpp
struct File {
    bluefs_fnode_t fnode;                    // inode + extents
    uint64_t dirty_seq = 0;                  // 脏时的 seq（用于日志追踪）
    int refs = 0;                            // 目录链接数
    bool locked = false;                     // 建议锁
    bool deleted = false;                    // 标记删除
    bool is_dirty = false;                   // 元数据变更
};
```

#### Dir

```cpp
struct Dir {
    std::map<std::string, FileRef> file_map;  // filename → File
};
```

#### FileWriter

```cpp
struct FileWriter {
    FileRef file;
    uint64_t pos = 0;                        // buffer 起始偏移（已写入位置）
    bufferlist buffer;                        // 新数据缓冲（文件末尾）
    bufferlist tail_block;                    // 文件末尾的未对齐部分块
    uint8_t writer_type = 0;                 // WRITER_WAL / WRITER_SST
    uint8_t write_hint = 0;                  // WRITE_LIFE_* hint

    std::array<IOContext*, 3> iocv;          // 每设备 IOContext
    std::array<bool, 3> dirty_devs;          // 每设备脏标记（用于 flush）
};
```

#### FileReader

```cpp
struct FileReader {
    FileRef file;
    struct FileReaderBuffer {
        uint64_t bl_off = 0;                 // prefectch buffer 逻辑偏移
        bufferlist bl;                       // 预取缓冲数据
        uint64_t pos = 0;                    // 当前逻辑偏移
        uint64_t max_prefetch;               // 最大预取大小
    };
    FileReaderBuffer buf;
    bool random;                             // 是否为随机读模式
    bool ignore_eof;                         // 读日志文件时使用
};
```

### 3.4 bluefs_shared_alloc_context_t

```cpp
struct bluefs_shared_alloc_context_t {
    Allocator *a = nullptr;                  // 共享的 Allocator 实例
    uint64_t alloc_unit = 0;                 // 共享设备的分配单元
    std::atomic<uint64_t> bluefs_used = 0;   // BlueFS 在共享设备上的消耗
};
```

### 3.5 BlueFSVolumeSelector（抽象基类）

```cpp
class BlueFSVolumeSelector {
public:
    virtual ~BlueFSVolumeSelector() = default;

    // 返回日志文件应放置的设备 hint
    virtual void *get_hint_for_log() const = 0;

    // 根据目录名返回放置 hint
    virtual void *get_hint_by_dir(const std::string &dirname) = 0;

    // 从 hint 中选出实际物理设备
    virtual uint8_t select_prefer_bdev(void *hint) = 0;

    // 追踪 extent 放置
    virtual void add_usage(void *hint, const bluefs_extent_t &ext) = 0;
    virtual void sub_usage(void *hint, const bluefs_extent_t &ext) = 0;

    // 获取 RocksDB db_paths 配置
    virtual void get_paths(const std::string &base,
                           std::vector<std::string> *paths) = 0;

    // 获取设备剩余空间（字节）
    virtual uint64_t get_avail_by_bdev(uint8_t bdev) const = 0;
};
```

### 3.6 RocksDBBlueFSVolumeSelector

智能卷选择器，按 RocksDB 数据级别和设备容量来决定文件放置位置：

```cpp
enum Level {
    LEVEL_LOG = 0,    // RocksDB WAL（日志）
    LEVEL_WAL = 1,    // RocksDB MANIFEST / CURRENT 等
    LEVEL_DB  = 2,    // SST 文件（db 级别）
    LEVEL_SLOW = 3,   // SST 文件（slow 级别）
    LEVEL_MAX = 4,
};

class RocksDBBlueFSVolumeSelector : public BlueFSVolumeSelector {
    // 每级别 × 每设备的字节计数矩阵
    std::array<std::array<std::atomic<uint64_t>, LEVEL_MAX>, MAX_BDEV + 1> level_usage_;

    // 每设备容量
    uint64_t db_total_ = 0;     // DB 设备总容量
    uint64_t wal_total_ = 0;    // WAL 设备总容量
    uint64_t slow_total_ = 0;   // Slow 设备总容量

    // 每设备预留空间（留给 BlueStore）
    uint64_t slow_reserved_ = 0;
    uint64_t db_reserved_ = 0;

    // 慢设备可用额度（DB 可以借给 Slow 的额度）
    uint64_t db_avail4slow_ = 0;

public:
    // 初始化：计算 db_avail4slow_ = max(0, db_total_ - usage - reserved)
    void init(uint64_t db_total, uint64_t wal_total, uint64_t slow_total);

    void *get_hint_for_log() const override;
    // 返回 LEVEL_LOG → BDEV_WAL

    void *get_hint_by_dir(const std::string &dirname) override;
    // "block.wal" → LEVEL_WAL → BDEV_WAL
    // "db.slow" → LEVEL_SLOW → BDEV_SLOW
    // 其他 → LEVEL_DB → BDEV_DB

    uint8_t select_prefer_bdev(void *hint) override;
    // WAL: 始终 BDEV_WAL
    // DB:  优先 BDEV_DB，DB 满时 (usage+新数据 > db_total_-reserved) 
    //      且 WAL 有空间 → spill 到 WAL；
    //      还不行且 slow 有空间 → spill 到 slow
    // SLOW: 优先 BDEV_SLOW，也可以借用 DB 剩余空间
    //       (慢设备数据可以放在 DB 设备上)

    void add_usage(void *hint, const bluefs_extent_t &ext) override;
    void sub_usage(void *hint, const bluefs_extent_t &ext) override;

    void get_paths(const std::string &base,
                   std::vector<std::string> *paths) override;
    // 返回 {base, db_total} 和 {base+".slow", slow_total}
    // RocksDB 据此设置 db_paths

    uint64_t get_avail_by_bdev(uint8_t bdev) const override;
};
```

**放置策略**：

| 数据级别 | 首选设备 | 溢出路径 |
| --- | --- | --- |
| LEVEL_LOG (RocksDB WAL) | BDEV_WAL | 不溢出 |
| LEVEL_WAL (MANIFEST 等) | BDEV_WAL | 不溢出 |
| LEVEL_DB (SST) | BDEV_DB | BDEV_DB 满 → BDEV_SLOW，若 Slow 也无空间 → BDEV_WAL |
| LEVEL_SLOW (冷 SST) | BDEV_SLOW | 可借用 BDEV_DB 剩余空间 (`db_avail4slow_`) |

---

## 4. IO 生命周期

### 4.1 mkfs 路径

```plaintext
mkfs(osd_uuid)
  │
  ├── 1. 打开块设备
  │     遍历已注册设备，打开 BlockDevice
  │
  ├── 2. 初始化分配器
  │     for each device:
  │     if is_shared: reuse shared_alloc->allocator
  │     else:         alloc[dev] = Allocator::create("avl", size, alloc_size)
  │
  ├── 3. 创建日志文件
  │     log_file = new File(ino = 1)
  │     _allocate(BDEV_DB, bluefs_max_log_runway, &log_file->fnode)
  │
  ├── 4. 写入初始事务
  │     txn = { seq = 1, OP_INIT }
  │     write to log_file via bdev
  │
  ├── 5. 写入超级块
  │     super = { uuid, osd_uuid, block_size, log_fnode }
  │     write to BDEV_DB offset 4096
  │
  └── 6. 清理
        close devices
```

### 4.2 mount 路径

```plaintext
mount()
  │
  ├── 1. 打开块设备
  │     遍历已注册设备，打开 BlockDevice
  │
  ├── 2. 读取超级块
  │     read(BDEV_DB, 4096, 4096 → super_bl)
  │     decode super
  │     verify CRC-32C
  │
  ├── 3. 初始化分配器
  │     for each device:
  │     if is_shared: use shared_alloc->allocator
  │     else:         create Allocator("avl", size, alloc_size)
  │
  ├── 4. 设置卷选择器
  │     初始化 RocksDBBlueFSVolumeSelector
  │     传入各设备容量，计算 db_avail4slow_
  │
  ├── 5. 重放日志
  │     log_fnode = super.log_fnode
  │     for each extent in log_fnode.extents:
  │       for each transaction block:
  │         read block → decode OPs
  │         verify CRC-32C
  │         switch op:
  │           OP_DIR_CREATE:  nodes_.dir_map[name] = new Dir
  │           OP_DIR_LINK:    dir->file_map[name] = file
  │           OP_DIR_UNLINK:  dir->file_map.erase(name)
  │           OP_FILE_UPDATE: nodes_.file_map[ino] = decode fnode
  │           OP_FILE_UPDATE_INC: apply delta to existing fnode
  │           OP_FILE_REMOVE: nodes_.file_map.erase(ino)
  │           OP_JUMP:        skip to new offset in log
  │           OP_INIT:        no-op
  │     set log_.seq_live, dirty_.seq_stable, dirty_.seq_live
  │
  ├── 6. 从分配器中移除已占用的空间
  │     for each file in nodes_.file_map:
  │       update volume selector usage
  │       for each extent in file.fnode.extents:
  │         alloc[id]->init_rm_free(extent.offset, extent.length)
  │
  └── 7. 创建日志 writer
        log_.writer = new FileWriter(log_file)
```

### 4.3 写入路径

```plaintext
open_for_write(dirname, filename, &h, overwrite=false)
  │
  ├── 1. 查找或创建目录
  ├── 2. 查找或创建文件
  │     if new: assign ino = ++ino_last
  ├── 3. 写入日志事务
  │     append OP_FILE_UPDATE + OP_DIR_LINK to log.t
  ├── 4. 创建 FileWriter
  │     h = new FileWriter(file)
  └── 返回

write(h, buf, len)
  │
  └── append_try_flush(h, buf, len)
        │
        ├── 1. h->buffer.append(buf, len)
        │
        ├── 2. if h->buffer.length() >= cfg_.min_flush_size:
        │       _flush_F(h, false)
        │       │
        │       ├── 获取 h->file->lock
        │       ├── 若已分配空间不足 → _allocate() 扩容
        │       ├── h->flush_buffer()
        │       │     // 合并 tail_block + buffer，按块对齐
        │       │     // 返回连续的 bufferlist 用于写入
        │       ├── for each extent:
        │       │     bdev[extent.bdev]->aio_write(extent.offset, data, h->iocv[dev])
        │       └── bdev->aio_submit(ioc)
        │
        ├── 3. 检查是否需要同步到日志
        │     file->is_dirty = true
        │
        └── 4. _maybe_compact_log()
              根据日志大小和比率判断是否触发异步压缩

fsync(h)
  │
  ├── 1. _flush_F(h, true)           // 刷写所有缓冲数据
  ├── 2. _flush_bdev(h)              // 等待 AIO + bdev->flush()
  ├── 3. _signal_dirty_to_log(h)     // 标记文件元数据为脏
  │     file->dirty_seq = dirty_.seq_live
  │     add to dirty_.files[dirty_.seq_live]
  │
  ├── 4. _flush_and_sync_log()       // 提交日志事务
  │     │
  │     ├── 获取 log.lock + dirty.lock
  │     ├── seq = ++log_.seq_live
  │     ├── _consume_dirty(seq)
  │     │     // 收集所有 dirty_.files[seq] 的 file
  │     │     // 每个 file 生成 OP_FILE_UPDATE_INC
  │     │     // 加入 log_.t
  │     ├── _maybe_extend_log()
  │     │     // 若日志余量不足，追加分配新 extent
  │     │     // 写入 OP_FILE_UPDATE_INC（log file 自身）
  │     ├── _flush_and_sync_log_core()
  │     │     // 编码 log_.t → buffer
  │     │     // log_.writer->append(buffer)
  │     │     // _flush_special(log_.writer) → bdev aio + wait
  │     │     // bdev->flush()
  │     ├── _clear_dirty_set_stable(seq)
  │     │     // 从 dirty_.files 中移除已清理的 file
  │     └── _release_pending_allocations()
  │           // 将 pending_release 归还 Allocator
  │
  └── 5. _maybe_compact_log()

close_writer(h)
  │
  ├── 1. fsync(h)                      // 确保所有数据已刷写
  ├── 2. 等待所有 AIO 完成
  └── 3. delete h
```

### 4.4 读取路径

```plaintext
open_for_read(dirname, filename, &h, random=false)
  │
  ├── 1. 查找目录 + 文件
  └── 2. h = new FileReader(file, random ? 4K : cfg_.max_prefetch)

read(h, offset, length, outbl)
  │
  ├── 1. if no cache → 按块对齐从磁盘读取
  │     fnode.seek(offset) → 查找包含 offset 的 extent
  │     bdev[dev]->read(physical_offset, aligned_len, &buf)
  │     缓存到 FileReaderBuffer
  │
  ├── 2. 从缓存中复制数据到 outbl
  └── 返回实际读取字节数

read_random(h, offset, length, out)
  │
  └── bdev[dev]->read_random(physical_offset, length, out)
        // 直接读取，不经过缓存
```

### 4.5 日志压缩（异步）

异步压缩是 BlueFS 的关键机制：允许在压缩期间继续写入日志，避免前台 IO 停顿。

```plaintext
_maybe_compact_log()
  │
  ├── 检查触发条件
  │     if log_is_compacting_ → 返回（避免并发）
  │     if 正在 umount  → 返回
  │     current_size = log_file 当前总大小
  │     estimated = 估算紧凑大小（所有 fnode + dir 之和）
  │     if current_size < cfg_.log_compact_min_size → 返回
  │     if current_size / estimated < cfg_.log_compact_min_ratio → 返回
  │
  └── _compact_log_async() // 异步压缩

_compact_log_async()
  │
  │  ─── Step 0: 准备 ───
  │
  ├── 获取 compact_lock_
  │     log_is_compacting_ = true
  │     log_forbidden_to_expand_ = true  // 压缩期间日志不能追加新 extent
  │
  ├── 获取 log.lock
  │
  │  ─── Step 1: 分配 tail 空间，让写入可以继续 ───
  │
  ├── 分配 fnode_tail 空间
  │     // 在日志文件末尾追加并分配一段新空间
  │     // 当前日志写入完成后，后续操作可以跳到 tail
  │     _allocate(BDEV_DB, max_log_runway, &log_fnode_tail)
  │
  ├── 写入 OP_JUMP + OP_FILE_UPDATE_INC
  │     // log.t 添加两个 op:
  │     //   OP_FILE_UPDATE_INC(log_fnode_tail) — 记录日志自身新增 extent
  │     //   OP_JUMP(new_seq, tail_offset)      — 指向 tail 空间
  │     _flush_and_sync_log_core()  // 刷写入磁盘
  │
  ├── 释放 log.lock
  │     // log 锁释放后，新写入直接写到 tail 空间
  │
  │  ─── Step 2: 收集元数据快照（不持有 log 锁）───
  │
  ├── _compact_log_dump_metadata()
  │     // 遍历 nodes_.dir_map + nodes_.file_map
  │     // 收集：
  │     //   - 所有目录的 OP_DIR_CREATE
  │     //   - 所有文件条目的 OP_DIR_LINK
  │     //   - 所有文件的 OP_FILE_UPDATE（完整 fnode，不是增量）
  │     // 生成 compacted_meta_bl
  │
  │  ─── Step 3: 分配压缩后日志空间 ───
  │
  ├── 计算 compacted_meta_need = compacted_meta_bl.length()
  ├── starter 大小 = log_fnode 一次增量 + OP_JUMP
  ├── 分配用于 starter + compacted_meta 的空间
  │     // 如果可用空间不足，做同步分配（可能触发 BlueStore GC）
  │     _allocate(BDEV_DB, compacted_meta_need + starter_size, &new_fnode)
  │
  │  ─── Step 4: 构建新日志内容 ───
  │
  ├── starter_bl:
  │     OP_INIT
  │     OP_FILE_UPDATE_INC(log_fnode_delta)  // 日志自身的 fnode
  │     OP_JUMP → compacted_meta 起始偏移
  │
  ├── compacted_meta_bl:
  │     OP_DIR_CREATE × N
  │     OP_DIR_LINK × N
  │     OP_FILE_UPDATE × N  // 所有文件的完整 fnode
  │     OP_JUMP → tail 起始偏移
  │
  │  ─── Step 5: 写入新日志内容 ───
  │
  ├── bdev->write(BDEV_DB, new_fnode 起始偏移，starter_bl + compacted_meta_bl)
  │
  │  ─── Step 6: 更新超级块 ───
  │
  ├── 获取 log.lock
  ├── super.log_fnode = new_fnode
  ├── _write_super(BDEV_DB)     // 持久化超级块
  │
  │  ─── Step 7: 切换内存状态 ───
  │
  ├── log_file->fnode = new_fnode
  ├── log_.writer->pos 调整为指向 tail 的写入点
  ├── log_forbidden_to_expand_ = false
  ├── log_is_compacting_ = false
  ├── 释放 log.lock
  │
  │  ─── Step 8: 释放旧日志空间 ───
  │
  └── for each old extent (不在 new_fnode 中):
        alloc[id]->release(off, len)
        volume_selector->sub_usage(hint, extent)
```

**关键设计点**：

1. **三段跳跃式日志**：`starter → compacted_meta → tail`，通过 `OP_JUMP` 串联。新日志文件在物理上可以不连续。
2. **双 log.lock 区间**：Step 0-1 持锁准备 tail，Step 2-5 释放锁让前台 IO 继续写入 tail，Step 6-7 重新持锁切换。
3. **元数据快照一致性**：`_compact_log_dump_metadata()` 在无 log 锁时执行，捕获的是 Step 2 时刻的内存快照。Step 0-1 中 `OP_JUMP` 之前的写入全部已刷盘，因此快照不会丢失已提交的变更。

### 4.6 空间分配 (`_allocate()`)

```plaintext
_allocate(uint8_t dev_id, uint64_t len, bluefs_fnode_t *node)
  │
  ├── 1. len = round_up(len, alloc_size_[dev_id])
  │
  ├── 2. hint = node 最后一个 extent 的结束偏移
  │
  ├── 3. alloc = is_shared(dev_id) ? shared_alloc_->a : alloc_[dev_id]
  │
  ├── 4. alloc->allocate(len, alloc_size, hint, &extents)
  │
  ├── 5. if 分配失败 && 允许设备降级：
  │     // WAL → DB → Slow 逐级回退
  │     dev_id++ → retry
  │
  ├── 6. if 共享设备 && 分配失败且有 cooldown 机制：
  │     设置 cooldown_deadline 避免重试过密
  │     // 共享设备分配的 cooldown：若连续失败，
  │     // 等待 bluefs_failed_shared_alloc_cooldown 秒后再试
  │
  ├── 7. for each extent in extents:
  │     node->append_extent({offset, length, dev_id})
  │     if vselector_:
  │       vselector_->add_usage(dev_hint, extent)
  │
  └── 8. if is_shared: shared_alloc_->bluefs_used += len
```

---

## 5. 线程模型

### 5.1 线程概览

| 线程 | 职责 |
| --- | --- |
| 调用者线程 | 文件读写操作 (`write`/`read`/`fsync`) |
| AIO 回调 | 异步 IO 完成通知 |
| 异步压缩线程 | 在 `_compact_log_async()` 中后台执行日志压缩 |

异步压缩的执行流：`_maybe_compact_log()` 在 `flush()` / `fsync()` 路径上被调用，发现需要压缩时直接在调用者线程上执行 `_compact_log_async()`。

> **注意**: `_compact_log_async()` 虽然在调用者线程上执行，但其 **Step 2-5（元数据快照 + 写入）** 释放了 `log.lock`，允许其他调用者线程在此期间继续写入日志。因此它本质上是异步的——不阻塞前台日志提交。

### 5.2 锁层级

```plaintext
         │ W  │ L  │ N  │ D  │ F
    ─────┼────┼────┼────┼────┼────
    W   │    │ >  │ >  │ >  │ >
    L   │    │    │ >  │ >  │ >
    N   │    │    │    │ >  │ >
    D   │    │    │    │    │ >
    F   │    │    │    │    │
```

- **W** = `FileWriter::lock`
- **L** = `log_.lock`
- **N** = `nodes_.lock`
- **D** = `dirty_.lock`
- **F** = `File::lock`

这是一个 DAG，按顺序加锁可避免死锁。
额外独立的 `compact_lock_` 仅用于保护 `log_is_compacting_` 状态，不与上述锁混用。

---

## 6. RocksDB 集成 (BlueRocksEnv)

BlueRocksEnv 是连接 BlueFS 和 RocksDB 的桥梁——它实现 `rocksdb::Env` 接口，将 RocksDB 的所有文件操作路由到 BlueFS，而将线程管理、定时等非文件操作委托给 POSIX `Env::Default()`。

详细设计见独立文档：[blue-rocks-env.md](blue-rocks-env.md)。

---

## 7. 简化与决策

### 7.1 与 Ceph 的差异汇总

| 特性 | Ceph BlueFS | cxxlab |
| --- | --- | --- |
| 日志压缩 | 异步（默认） | 异步（保留） |
| VolumeSelector | RocksDBBlueFSVolumeSelector（默认） | RocksDBBlueFSVolumeSelector（保留） |
| 设备迁移 | BDEV_NEWWAL / BDEV_NEWDB | 不实现 |
| 废弃操作 | OP_ALLOC_ADD / OP_ALLOC_RM | 移除 |
| 超级块布局 | 可选 memorized_layout | 不实现 |
| mempool | 精细内存统计 | 移除 |
| PerfCounters | 详细性能计数器 | 保留基础统计 |

### 7.2 关键决策

| 决策 | 原因 |
| --- | --- |
| 异步压缩 | 前台 IO 不因压缩而停顿，对 RocksDB 写入延迟影响最小 |
| RocksDBBlueFSVolumeSelector | 按 RocksDB 数据级别和设备容量智能放置，避免 DB 设备过早写满 |
| BlueRocksEnv + EnvWrapper | 只覆盖文件操作，其余委托 POSIX，最小化实现量 |
| 不移除 OP_JUMP | 异步压缩的核心依赖，不可或缺 |
| 与 BlueStore 共享 Allocator | 避免空间分区浪费，是本设计的核心优势 |
| 每设备独立 Allocator | 独占设备（WAL）使用独立分配器，共享设备使用 BlueStore 的分配器 |
| 使用 AvlAllocator 而非 BitmapAllocator | BitmapAllocator 的 L2 粒度为 512MB，小测试设备（8MB）上 `init_rm_free` 任意范围都会清空首个 L2 bit，导致全设备误标记为已分配。AvlAllocator 无最小粒度限制，适合所有设备尺寸 |

### 7.3 已知待办

- [ ] 设备热迁移
- [ ] BlueFS 健康检查和自我修复
- [ ] 异步压缩线程池（当前在调用者线程执行，后续可专用线程）

---

## 8. 参考

- Ceph source: `src/os/bluestore/BlueFS.h` / `.cc`
- Ceph source: `src/os/bluestore/bluefs_types.h` / `.cc`
- Ceph source: `src/os/bluestore/BlueStore.h` / `.cc` (BlueFS 集成部分)
- 本项目 [docs/design/overview.md](overview.md): 架构总览
- 本项目 [docs/design/block-device.md](block-device.md): 块设备抽象层设计
- 本项目 [docs/design/allocator.md](allocator.md): Allocator 设计
- 本项目 [docs/design/blue-rocks-env.md](blue-rocks-env.md): BlueRocksEnv 设计
- 本项目 [docs/design/bluestore.md](bluestore.md): BlueStore 设计
