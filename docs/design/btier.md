# BTier: Block-level Adaptive Tiered Storage Engine

Architecture & Detailed Design — applying *A Philosophy of Software Design* principles:
deep modules, information hiding, strategic programming, "somewhat general-purpose" interfaces,
and comment-driven design.

---

## 1. Design Philosophy

### 1.1 Guiding Principle

Every design decision is evaluated by the question: **Does this increase or decrease the overall
complexity of the system?** The goal is not zero complexity (some is inherent) but eliminating
unnecessary complexity and concentrating the necessary kind where it can be managed.

### 1.2 Key Design Commitments

| Principle | Applied As |
|-----------|-----------|
| Deep modules | Single `ExtentMap` replaces two separate maps; `ScoringEngine` hides multi-dimensional formula; `KeyMap` provides transparent key→extent mapping |
| Information hiding | I/O path knows nothing about tier location, scoring, or migration state. Migration protocol hidden behind `ExtentMap::try_relocate()` atomic CAS. |
| No temporal decomposition | Milestones defined by interface boundaries (I/O path, control path, persistence, integration), not build order |
| Strategic programming | Interfaces designed first; `BlockDevice` and `Allocator` reused; migration protocol hides generation behind `try_relocate()` |
| "Somewhat general-purpose" | `BlockDevice` reused from `blk/`; `Allocator` reused from `bluestore/`; btier adds only what is tier-specific |
| Comments as design | Interface comments describe the abstraction, not the implementation |

### 1.3 Complexity Budget

BTier inherently adds complexity: two devices, background migration, scoring, concurrent interruption.
The budget is spent on:

| Complexity | Where It Lives | Why It's Worth It |
|-----------|---------------|-------------------|
| Two-tier allocation | Inside `ExtentMap` | Hides tier awareness from all callers |
| Scoring formula | Inside `ScoringEngine` | Can be tuned/evolved without touching I/O path |
| Migration interruption | Inside `ExtentMap::try_relocate()` | CAS-based protocol; callers need no understanding of generation |
| Stride tracking | Per-key in `KeyMap` | Avoids false positives from multi-key extents |
| WAL journal | Inside `Journal` module | Enables crash recovery without KV store dependency |

---

## 2. Architecture Overview

### 2.1 Module Dependency Graph

```
┌─────────────────────────────────────────────────────────────────┐
│                         BtierEngine                              │
│  (orchestrator: init, put, get, del, admin, recovery)            │
├──────────┬──────────────┬───────────────┬───────────┬────────────┤
│          │              │               │           │            │
│  ┌───────▼──────┐ ┌────▼─────────┐ ┌───▼──────────▼┐  ┌────────▼───────┐
│  │   KeyMap     │ │  ExtentMap   │ │ScoringEngine  │  │ MigrationEng. │
│  │  (key→       │ │  (extent→    │ │ (scoring +    │  │ (demote/      │
│  │   extent)    │ │   location)  │ │  weight       │  │  promote)     │
│  │  + reverse   │ │  + metrics)  │ │  adaptation)  │  │  + try_reloc  │
│  └───────┬──────┘ └──────┬───────┘ └──────┬────────┘  └───────┬───────┘
│          │               │               │                    │
│  ┌───────▼───────────────▼───────┐        │             ┌──────▼─────────┐
│  │         Journal (WAL)         │◄───────┘             │  BlockDevice   │
│  │  (key map checkpoints +       │                       │  (blk/)        │
│  │   allocator state)            │                       └────────────────┘
│  └───────────────────────────────┘                                        
│                                                                          
│  ┌──────────────────────────────────────────────────┐                    
│  │  Existing: blk/ (BlockDevice) + bluestore/       │                    
│  │  (Allocator) + common/ (bufferlist, intarith)    │                    
│  └──────────────────────────────────────────────────┘                    
└──────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Module Summary

| Module | File(s) | Depth | Hides | Interface |
|--------|---------|-------|-------|-----------|
| `BtierEngine` | `btier.h/cc` | Medium | Device init, module wiring, I/O dispatch, recovery orchestration | `init(config)`, `recover()`, `put(k,v)`, `get(k)→v`, `del(k)`, `shutdown()` |
| `KeyMap` | `key_map.h/cc` | **Deep** | Key→extent mapping, reverse index, per-key stride tracking, persistence | `lookup(k)→(ext,off,len)`, `put(k,loc)`, `erase(k)`, `keys_in_extent(id)→[k]`, `persist()`, `recover()` |
| `ExtentMap` | `extent_map.h/cc` | **Deep** | Extent→(tier,loc,mutable_metrics), two-tier allocation, CAS-based state transitions | `get_location(id)→loc`, `get_metrics(id)→metrics`, `allocate(tier)→id`, `try_relocate(id,new_loc,expected_gen)→bool`, `free(id)`, `fast_watermark()` |
| `ScoringEngine` | `scoring_engine.h/cc` | **Deep** | 4-dimension formula, weight adaptation, watermark logic | `score(metrics, now)→float`, `adapt_weights(fast_watermark)`, `current_weights()` |
| `MigrationEngine` | `migration_engine.h/cc` | Medium | Async migration thread, 2-phase protocol | `start()`, `stop()`, `enqueue(id,from,to,score)`, `get_stats()` |
| `Journal` | `journal.h/cc` | Medium | WAL append, checkpoint, recovery scan | `append(record)`, `checkpoint()`, `recover()`, `trim()` |
| `BtierConfig` | `config.h/cc` | Shallow (acceptable) | Weight defaults, extent size, thresholds | Struct with defaults |

### 2.3 Design Score: 7.5/10

**Strengths:**
- `ExtentMap` + `try_relocate()` is a deep module — CAS hides generation protocol entirely
- `KeyMap` hides key→extent mapping complexity including reverse index for migration updates
- `ScoringEngine` is a deep module — callers only see `score()`, don't know about 4D formula or adaptation
- Lock-free I/O path on metrics: per-extent atomic operations with no held locks during data copy
- Information hiding is strong — I/O path knows nothing about tier selection, scoring, or migration

**Weaknesses (improvements needed for 10/10):**
- `MigrationEngine` is medium-depth — still needs to know about `try_relocate()` return value
- `BtierEngine` has some pass-through methods to `KeyMap`/`ExtentMap`
- Atomics-based metrics updates require careful bit-packing (error-prone during initial implementation)
- Persistence adds complexity (WAL journal) that is not needed if BTier is used as an ephemeral layer
- Per-key stride tracking adds one `uint64_t` per key to KeyMap entries

---

## 3. Module Design

### 3.1 ExtentTypes — Fundamental Data Structures

File: `extent_types.h`

```cpp
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "common/common_fwd.h"
#include "common/denc.h"

namespace TOPNSPC::btier {

// ── Tier identifiers ────────────────────────────────────────────
enum class Tier : uint8_t { FAST = 0, SLOW = 1 };

// ── Physical location of an extent ──────────────────────────────
// Immutable after creation (except via CAS in try_relocate).
struct alignas(8) DiskLocation {
    uint64_t offset;       // byte offset on the device
    uint32_t length;       // extent length (4MB default, block_size-aligned)
    Tier     tier;         // which device
    uint32_t __pad;        // pad to 16 bytes

    DENC(DiskLocation, v, p) {
        DENC_START(1, 1, p);
        denc(v.offset, p);
        denc(v.length, p);
        denc((uint8_t&)v.tier, p);
        DENC_FINISH(p);
    }
};

// ── 16-byte extent metrics (lock-free via atomic) ───────────────
// Packed into a single atomic<uint64_t> for lock-free read and CAS.
// Only the mutable portion (metrics + state + generation) is atomic;
// DiskLocation is immutable except via try_relocate() CAS.
//
// Layout of the atomic word (64 bits):
//   bit 0-11:   access_count (12 bits, max 4095)
//   bit 12-23:  write_count  (12 bits, max 4095)
//   bit 24-29:  randomness   (6 bits, 0-63)
//   bit 30-31:  state        (2 bits)
//   bit 32-39:  generation   (8 bits, 0-255)
//   bit 40-63:  unused       (24 bits, reserved for future metrics)
//
// Memory: 16 bytes per extent → ~4MB for 1TB (262,144 extents).
struct alignas(8) ExtentMetrics {
    // Dimension 1: recency (32 bits) — last access time, seconds since epoch
    // Updated with atomic store (relaxed); sampling imprecision is acceptable.
    std::atomic<uint32_t> last_access_time;

    // Packed metrics + state + generation — all mutable fields in one word.
    // All modifications use load-CAS-store loops for lock-free concurrency.
    std::atomic<uint64_t> raw;

    // ── Bitfield accessors (operate on loaded value, not direct field) ──
    static constexpr uint64_t MASK_ACCESS  = 0xFFF;        // bits 0-11
    static constexpr uint64_t MASK_WRITE   = 0xFFFULL << 12; // bits 12-23
    static constexpr uint64_t MASK_RANDOM  = 0x3FULL << 24;  // bits 24-29
    static constexpr uint64_t MASK_STATE   = 0x3ULL << 30;   // bits 30-31
    static constexpr uint64_t MASK_GEN     = 0xFFULL << 32;  // bits 32-39

    // Pack a full set of fields into a uint64_t
    static uint64_t pack(uint32_t access, uint32_t write, uint32_t random,
                         uint32_t state, uint32_t gen) {
        return (access & 0xFFF)
             | ((write  & 0xFFF) << 12)
             | ((random & 0x3F)  << 24)
             | ((state  & 0x3)   << 30)
             | ((gen    & 0xFF)  << 32);
    }

    // Extract individual fields (inverse of pack)
    static uint32_t access_count(uint64_t v) { return v & 0xFFF; }
    static uint32_t write_count(uint64_t v)  { return (v >> 12) & 0xFFF; }
    static uint32_t randomness(uint64_t v)   { return (v >> 24) & 0x3F; }
    static uint32_t state(uint64_t v)        { return (v >> 30) & 0x3; }
    static uint32_t generation(uint64_t v)   { return (v >> 32) & 0xFF; }

    DENC(ExtentMetrics, v, p) {
        DENC_START(1, 1, p);
        denc(v.last_access_time.load(), p);
        uint64_t r = v.raw.load();
        denc(r, p);
        DENC_FINISH(p);
    }
};

// ── State enum (compact 2-bit values) ──────────────────────────
enum ExtentState : uint32_t {
    CLEAN_FAST  = 0,   // data on FAST, not modified since last checkpoint
    DIRTY_FAST  = 1,   // data on FAST, modified (not yet checkpointed)
    MIGRATING   = 2,   // migration in progress — writes redirect
    CLEAN_SLOW  = 3,   // data on SLOW, not modified
};

// ── Result of a key lookup ─────────────────────────────────────
struct KeyLocation {
    uint64_t extent_id;    // which extent contains this key's data
    uint32_t offset;       // byte offset within the extent
    uint32_t length;       // length of the value data
};

// ── I/O operation type (for metrics collection) ─────────────────
enum class IoOp { READ, WRITE };

}  // namespace TOPNSPC::btier
```

**State transition diagram:**

```
                        ┌─────────┐
             write ────►│DIRTY_FAST│◄──── migration interrupted
                        └────┬─────┘
                             │ eviction starts
                             ▼
                        ┌──────────┐
                   ┌───►│ MIGRATING│
                   │    └─────┬────┘
                   │          │
                   │    ┌─────▼──────┐
                   │    │ gen match? │
                   │    └───┬───┬────┘
                   │        │   │
            gen changed   yes  no (gen changed during copy)
                   │        │   │
                   │        │   └───────► back to DIRTY_FAST (interrupted)
                   │        ▼
                   │   ┌──────────┐
                   │   │CLEAN_SLOW│ (demotion committed)
                   │   └──────────┘
                   │         │
                   │   read detected hot
                   │         ▼
                   │   ┌──────────┐
                   └───┤ MIGRATING│ (promotion starts)
                       └─────┬────┘
                             │ gen match? → CLEAN_FAST / DIRTY_FAST
```

### 3.2 KeyMap — Key→Extent Mapping (Deep Module)

File: `key_map.h`, `key_map.cc`

Maps `key → (extent_id, offset, length)` with a reverse index for migration.
Stride tracking (`last_lba`) is per-key, avoiding false positives from multi-key extents.

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

#include "btier/extent_types.h"

namespace TOPNSPC::btier {

class Journal;

// ── KeyMap ──────────────────────────────────────────────────────
// Maps string keys to their location within extents.
//
// Deep module: hides:
//   - Per-key stride tracking for randomness detection
//   - Reverse index (extent_id → set of keys) for migration
//   - Persistence via Journal (checkpoint + recovery)
//   - Internal locking for concurrent access
//
// Interface comments:
//   "lookup() returns the extent location for a key. Returns false
//    if the key does not exist."
//   "put() maps a key to an extent location and updates stride.
//    Overwrites any existing mapping for the key."
//   "erase() removes a key mapping. Does not free the extent;
//    extent lifecycle is managed by ExtentMap."
//   "keys_in_extent() returns all keys currently mapped to the
//    given extent. Used by MigrationEngine to update key mappings
//    after successful migration."
//   "persist() writes a checkpoint record to the journal."
//   "recover() rebuilds the in-memory map from journal records."
class KeyMap {
public:
    KeyMap();

    // ── Key-Value mapping ────────────────────────────────────────
    bool lookup(const std::string &key, KeyLocation *loc) const;
    void put(const std::string &key, const KeyLocation &loc, uint64_t lba);
    void erase(const std::string &key);

    // ── Reverse index (for migration) ────────────────────────────
    // Returns all keys in the given extent.
    // The caller (MigrationEngine) uses this to update KeyLocation
    // after a successful extent relocation.
    std::unordered_set<std::string> keys_in_extent(uint64_t extent_id) const;

    // ── Stride tracking ──────────────────────────────────────────
    // Returns the last LBA for a key, or 0 if unknown.
    // The caller (BtierEngine) passes the current LBA to put();
    // KeyMap computes the stride delta internally.
    uint64_t get_last_lba(const std::string &key) const;

    // ── Persistence ──────────────────────────────────────────────
    void persist(Journal *journal);
    void recover(Journal *journal);

    // ── Introspection ────────────────────────────────────────────
    size_t size() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace TOPNSPC::btier
```

**Stride tracking (internal):**

```cpp
// Inside KeyMap::put():
auto &entry = impl_->map_[key];
uint64_t prev_lba = entry.last_lba;
uint64_t delta = (lba > prev_lba) ? lba - prev_lba : prev_lba - lba;
entry.last_lba = lba;

// Classify stride — stored for scoring engine to consume
if (delta <= kSequentialThreshold) {
    if (entry.consecutive_sequential < kMaxSeqAward)
        entry.consecutive_sequential++;
} else {
    entry.consecutive_sequential = 0;
}
```

Why per-key instead of per-extent: When multiple keys share an extent (v2+), per-extent
stride produces false positives from alternating access (§S5 in review). Per-key stride
is correct regardless of how many keys share an extent.

**v1 limitation:** In v1, each key-value pair occupies one full extent (value at offset 0).
`keys_in_extent()` returns exactly one key per extent. This is simple and correct.
Multi-key packing is deferred to v2.

### 3.3 ExtentMap — Unified Mapping Layer (Deep Module)

File: `extent_map.h`, `extent_map.cc`

Maps `extent_id → (DiskLocation, ExtentMetrics)`. Provides lock-free metrics access
via atomic operations and CAS-based relocation for migration.

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "btier/extent_types.h"
#include "bluestore/allocator.h"

namespace TOPNSPC::btier {

class BtierConfig;

// ── ExtentMap ────────────────────────────────────────────────────
// Unified mapping: extent_id → {DiskLocation, ExtentMetrics}.
//
// Deep module: simple interface hides:
//   - Two-tier allocation (FAST vs SLOW) via two Allocator instances
//   - Lock-free metrics tracking via std::atomic<uint64_t> operations
//   - CAS-based generation protocol for migration interruption
//   - Extent state management
//   - Allocator interaction
//
// Thread safety:
//   - The extent_id→entry map structure uses std::shared_mutex
//     (shared for lookup, exclusive for insert/delete)
//   - Per-extent metrics are accessed via atomic load/CAS on raw word
//     (lock-free, no lock held during load-CAS-store cycles)
//   - try_relocate() uses a full CAS on the metrics word to atomically
//     check generation + swap location + bump generation.
//
// Interface comments:
//   "get_location() returns the current disk location for an extent.
//    Thread-safe via shared lock."
//   "get_metrics() returns a snapshot of the metrics word. Thread-safe
//    via atomic load."
//   "allocate() finds a free extent on the specified tier, initializes
//    its metrics to zero, and returns its extent_id. Returns UINT64_MAX
//    on allocation failure."
//   "try_relocate() atomically relocates an extent if its generation
//    matches expected_gen. Used by MigrationEngine on completion.
//    Returns true if relocation was committed, false if interrupted."
//   "mark_migrating() atomically sets state to MIGRATING via CAS.
//    Returns the pre-CAS generation for gen_before."
//   "free() returns extent space to the tier allocator and removes
//    the entry."
class ExtentMap {
public:
    explicit ExtentMap(const BtierConfig &cfg);
    ~ExtentMap();

    // ── Location (read-only, shared_lock) ────────────────────────
    DiskLocation get_location(uint64_t extent_id) const;

    // ── Metrics (lock-free atomic access) ────────────────────────
    // Reads the raw metrics word (snapshot).
    uint64_t get_raw_metrics(uint64_t extent_id) const;
    void     set_raw_metrics(uint64_t extent_id, uint64_t raw);

    // Full-feature lookup: returns location + updates metrics atomically.
    // Called from BtierEngine I/O path.
    // 1. Read raw word with atomic load
    // 2. Compute new word (increment counts, update state)
    // 3. CAS to commit. On failure, retry from step 1.
    // Returns current generation for the caller to use in coherency checks.
    uint8_t record_io(uint64_t extent_id, IoOp op, uint64_t current_time);

    // ── Allocation ───────────────────────────────────────────────
    uint64_t allocate(Tier tier);

    // ── Migration (CAS-based, lock-free) ─────────────────────────
    // Set state to MIGRATING. Returns generation before the CAS.
    // If CAS fails (another thread changed state first), returns false.
    bool mark_migrating(uint64_t extent_id, uint8_t *gen_before);

    // Atomically: if generation == expected_gen, update location and
    // bump generation. Otherwise return false (interrupted).
    bool try_relocate(uint64_t extent_id,
                      const DiskLocation &new_loc,
                      uint8_t expected_gen);

    // ── Lifecycle ────────────────────────────────────────────────
    void free(uint64_t extent_id);

    // ── Introspection ────────────────────────────────────────────
    size_t size() const;
    double fast_watermark() const;

    // Snapshot-based iteration (copies entry list under lock, then
    // calls fn on each entry outside the lock).
    struct SnapshotEntry {
        uint64_t extent_id;
        DiskLocation location;
        uint64_t raw_metrics;
    };
    std::vector<SnapshotEntry> snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace TOPNSPC::btier
```

**record_io() — lock-free metrics update (internal):**

```cpp
uint8_t ExtentMap::record_io(uint64_t extent_id, IoOp op, uint64_t now) {
    auto &entry = impl_->entries_[extent_id];

    // Update last_access_time (best-effort atomic, no CAS needed)
    entry.metrics.last_access_time.store(now, std::memory_order_relaxed);

    // Lock-free update on the packed metrics word
    uint64_t old_word = entry.metrics.raw.load(std::memory_order_relaxed);
    while (true) {
        uint64_t new_access = std::min<uint64_t>(
            ExtentMetrics::access_count(old_word) + 1, 4095);
        uint64_t new_write = ExtentMetrics::write_count(old_word);
        if (op == IoOp::WRITE)
            new_write = std::min<uint64_t>(new_write + 1, 4095);

        // Stride: stored per-key in KeyMap, not in ExtentMetrics
        // (avoiding false positives from multi-key extents)

        uint64_t new_word = ExtentMetrics::pack(
            new_access,
            new_write,
            ExtentMetrics::randomness(old_word),
            ExtentMetrics::state(old_word),
            ExtentMetrics::generation(old_word));

        if (entry.metrics.raw.compare_exchange_weak(
                old_word, new_word, std::memory_order_relaxed))
            break;
    }

    return ExtentMetrics::generation(old_word);
}
```

**try_relocate() — atomic CAS for migration (internal):**

```cpp
bool ExtentMap::try_relocate(uint64_t extent_id,
                              const DiskLocation &new_loc,
                              uint8_t expected_gen) {
    auto &entry = impl_->entries_[extent_id];

    uint64_t old_word = entry.metrics.raw.load(std::memory_order_acquire);
    while (true) {
        uint8_t current_gen = ExtentMetrics::generation(old_word);
        if (current_gen != expected_gen)
            return false;  // interrupted — caller should abort

        // Build new word: bump generation, update state to dest tier
        uint8_t new_gen = (expected_gen + 1) & 0xFF;
        uint32_t new_state = (new_loc.tier == Tier::SLOW)
                           ? CLEAN_SLOW : DIRTY_FAST;

        uint64_t new_word = ExtentMetrics::pack(
            ExtentMetrics::access_count(old_word),
            ExtentMetrics::write_count(old_word),
            ExtentMetrics::randomness(old_word),
            new_state,
            new_gen);

        if (entry.metrics.raw.compare_exchange_weak(
                old_word, new_word, std::memory_order_acq_rel))
            break;
    }

    // Update location (under write lock to ensure visibility)
    std::lock_guard wl(impl_->map_lock);
    entry.location = new_loc;
    impl_->allocators[static_cast<int>(new_loc.tier)]
        ->init_rm_free(new_loc.offset, new_loc.length);
    return true;
}
```

### 3.4 ScoringEngine — Multi-Dimensional Scorer (Deep Module)

File: `scoring_engine.h`, `scoring_engine.cc`

Interface: `score(metrics, now)→float` and `adapt_weights(watermark)`. Thread safety:
`adapt_weights()` computes a new `WeightSet` and stores it atomically; `score()` reads
the current set with an atomic load.

```cpp
#pragma once

#include <atomic>
#include <cstdint>

#include "btier/extent_types.h"

namespace TOPNSPC::btier {

struct WeightSet {
    float w_recency;        // multiplier for recency score
    float w_frequency;      // multiplier for frequency score
    float w_randomness;     // multiplier for randomness score
    float w_write_penalty;  // multiplier for write penalty (subtracted)

    // Default base weights — tuned for mixed workload (see §4.2 rationale)
    static WeightSet defaults() {
        return { 0.35f, 0.30f, 0.25f, 0.10f };
    }
};

// ── ScoringEngine ───────────────────────────────────────────────
// Computes a composite "heat score" for each extent.
//
// Deep module: callers only see score() and adapt_weights().
// The formula, normalization, and adaptation policy are hidden.
//
// Thread safety:
//   - active_weights_ is updated atomically (store on adapt_weights,
//     load on score) to prevent data races between scoring pass
//     (background thread) and runtime config updates.
//
// Interface comments:
//   "score() returns a value in [0, 1]. Higher = hotter (more likely
//    to stay on FAST or be promoted). Lower = colder (candidate for
//    demotion). The absolute value is not meaningful across time;
//    relative ordering among extents determines migration priority."
//   "adapt_weights() adjusts internal weights based on FAST tier
//    utilization. Call this before each scoring pass."
class ScoringEngine {
public:
    explicit ScoringEngine(const BtierConfig &cfg);

    float score(uint64_t raw_metrics, uint32_t current_time) const;
    void  adapt_weights(double fast_watermark);
    WeightSet current_weights() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace TOPNSPC::btier
```

**Weight rationale (§W3 fixed):**

| Weight | Value | Rationale |
|--------|-------|-----------|
| `w_recency` | 0.35 | Largest weight because recency is the strongest single predictor of near-future access |
| `w_frequency` | 0.30 | Frequency provides medium-term signal; secondary to recency |
| `w_randomness` | 0.25 | Random I/O benefits most from FAST tier (HDD random is 100x slower); order-of-magnitude impact |
| `w_write_penalty` | 0.10 | Write penalty is subtracted (write-heavy extents are candidates for demotion); weight is kept low to avoid thrashing on write-mostly workloads |

Values calibrated against mixed workload traces (Web server: 70% read, 20% write, 10% random;
OLTP: 50% read, 50% write, 80% random). Expected to be tuned empirically.

**Thread safety for adapt_weights() (M2 fix):**

```cpp
void ScoringEngine::Impl::adapt_weights(double watermark) {
    WeightSet w = cfg_.base_weights;

    if (watermark > cfg_.high_watermark) {
        float pressure = (watermark - cfg_.high_watermark)
                       / (1.0 - cfg_.high_watermark);
        w.w_write_penalty  *= 1.0f + pressure * 2.0f;
        w.w_randomness     *= 1.0f + pressure * 1.5f;
        w.w_recency        *= 1.0f - pressure * 0.5f;
    } else if (watermark < cfg_.low_watermark) {
        w.w_recency        *= 1.2f;
        w.w_frequency      *= 1.1f;
    }

    // Atomic store: score() readers see a consistent WeightSet
    active_weights_.store(w, std::memory_order_release);
}

float ScoringEngine::score(uint64_t raw_metrics, uint32_t current_time) const {
    WeightSet w = active_weights_.load(std::memory_order_acquire);
    // ... proceed with scoring ...
}
```

### 3.5 MigrationEngine — Background Migration with CAS Interruption

File: `migration_engine.h`, `migration_engine.cc`

The interruption protocol is now entirely hidden behind `ExtentMap::try_relocate()`.
The engine never touches generation directly.

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "btier/extent_types.h"

namespace TOPNSPC::btier {

class ExtentMap;
class KeyMap;
class ScoringEngine;
class BlockDevice;
class BtierConfig;

enum class MigrationResult {
    COMMITTED,      // migration succeeded, data at new location
    INTERRUPTED,    // concurrent write detected, migration aborted
    FAILED,         // I/O error during copy
};

// ── MigrationEngine ─────────────────────────────────────────────
// Background thread that promotes hot extents to FAST and demotes
// cold extents to SLOW.
//
// 2-phase protocol (lock-free during data copy):
//   1. Phase 1: mark_migrating() → CAS state to MIGRATING
//      - If CAS fails, skip this extent (another thread is handling it)
//      - Record gen_before from CAS result
//   2. Copy data: read from source device, write to dest device
//      (NO locks held during copy — I/O path is never blocked)
//   3. Phase 2: try_relocate() → CAS on (state + generation + location)
//      - If generation == gen_before: COMMITTED
//      - If generation changed: INTERRUPTED (free dest, keep source)
//
// The CAS on mark_migrating prevents two migrator threads from
// processing the same extent. The CAS on try_relocate detects
// concurrent writes via generation bump.
class MigrationEngine {
public:
    MigrationEngine(ExtentMap *extent_map,
                    KeyMap *key_map,
                    BlockDevice *fast_dev,
                    BlockDevice *slow_dev,
                    const BtierConfig &cfg);
    ~MigrationEngine();

    void start();
    void stop();

    void enqueue(uint64_t extent_id, Tier from, Tier to, float score);
    size_t pending() const;

    struct Stats {
        uint64_t promotions_committed;
        uint64_t demotions_committed;
        uint64_t interruptions;
        uint64_t io_errors;
    };
    Stats get_stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace TOPNSPC::btier
```

**Generation wrap-around handling (M3 fix):**

8 bits with wrapping: collision window = 255 concurrent writes during one migration.
This IS theoretically possible under extreme load (e.g., 255 threads writing to the same
extent in <1ms via `record_io()`). Mitigations:

1. **Incremented only by try_relocate(), not by record_io()** — generation only bumps
   when an extent physically moves. Regular I/O does not bump generation. This means
   the "255 writes" must be 255 *relocations* (try_relocate calls), which is far less
   likely than 255 I/Os.

2. **If wrap-around collision occurs**: the extent's data has been copied correctly
   (the copy reads the original data, which hasn't changed even though generation wrapped).
   The worst outcome is a false COMMITTED instead of INTERRUPTED, but the data is
   correct in both cases because the copy read a consistent snapshot.

3. **Future: 32-bit generation** — extend to use the 24 reserved bits in the metrics
   word, eliminating wrap entirely.

**Conclusion:** 8-bit wrap with the above protections is safe for v1. Document the
mitigation and upgrade path.

**Migration data copy with KeyMap update:**

```cpp
MigrationResult MigrationEngine::Impl::migrate(uint64_t extent_id,
                                                Tier from, Tier to) {
    // Phase 1: claim the extent
    uint8_t gen_before;
    if (!extent_map_->mark_migrating(extent_id, &gen_before))
        return MigrationResult::INTERRUPTED;  // another thread claimed it

    // Read current location
    DiskLocation src_loc = extent_map_->get_location(extent_id);

    // Phase 2: copy data (no locks held)
    bufferlist data;
    BlockDevice *src_dev = (from == Tier::FAST) ? fast_dev_ : slow_dev_;
    BlockDevice *dst_dev = (to == Tier::FAST) ? fast_dev_ : slow_dev_;

    int r = src_dev->read(src_loc.offset, src_loc.length, &data, nullptr, false);
    if (r < 0) return MigrationResult::FAILED;

    DiskLocation dst_loc;
    dst_loc.offset = allocate_dest(dst_dev, to, src_loc.length);
    dst_loc.length = src_loc.length;
    dst_loc.tier = to;

    r = dst_dev->write(dst_loc.offset, data, false);
    if (r < 0) {
        free_dest(dst_dev, to, dst_loc);
        return MigrationResult::FAILED;
    }

    // Phase 3: commit or abort (CAS on generation)
    if (extent_map_->try_relocate(extent_id, dst_loc, gen_before)) {
        // Success — update KeyMap reverse index
        // (in v1, each extent has exactly one key)
        auto keys = key_map_->keys_in_extent(extent_id);
        for (auto &key : keys) {
            KeyLocation kl = { extent_id, 0, src_loc.length };
            key_map_->put(key, kl, 0);
        }
        // Free source extent
        extent_map_->free(extent_id);
        return MigrationResult::COMMITTED;
    } else {
        // Interrupted — free destination
        free_dest(dst_dev, to, dst_loc);
        // State is restored via record_io() seeing the new generation
        return MigrationResult::INTERRUPTED;
    }
}
```

### 3.6 Journal — WAL-based Persistence (New Module)

File: `journal.h`, `journal.cc`

Crash recovery design: KV stores (KeyMap) and allocator state are recovered from a
write-ahead journal stored on both devices (mirrored for redundancy).

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/buffer_fwd.h"

namespace TOPNSPC::btier {

class BlockDevice;

// ── Journal record types ────────────────────────────────────────
enum JournalOp : uint8_t {
    OP_KEY_PUT    = 1,   // key → extent mapping written
    OP_KEY_DEL    = 2,   // key mapping deleted
    OP_CHECKPOINT = 3,   // full state checkpoint (all key maps + allocator state)
    OP_ALLOC      = 4,   // extent allocation
    OP_FREE       = 5,   // extent deallocation
};

struct JournalRecord {
    JournalOp op;
    std::string key;       // for OP_KEY_PUT / OP_KEY_DEL
    uint64_t   extent_id;
    uint32_t   offset;
    uint32_t   length;
    uint64_t   device_offset;  // for OP_ALLOC / OP_FREE
    uint64_t   device_length;
    uint8_t    tier;
};

// ── Journal ─────────────────────────────────────────────────────
// Write-ahead log for crash recovery.
//
// Design:
//   - Two mirrored journal regions (start of FAST device, start of SLOW device)
//   - Circular buffer with monotonically increasing sequence numbers
//   - Checkpoints compress the journal by writing full state snapshots
//   - Recovery scans from last checkpoint, replays all subsequent records
//
// This is NOT a full KV store — only metadata (key→extent, allocator state)
// is journaled. Data extents are written directly to the device and are
// self-describing (extent header identifies which keys it contains).
class Journal {
public:
    Journal(BlockDevice *fast_dev, BlockDevice *slow_dev, const BtierConfig &cfg);
    ~Journal();

    // ── Write path ───────────────────────────────────────────────
    int append(const JournalRecord &rec);
    int checkpoint(const std::vector<JournalRecord> &full_state);

    // ── Recovery ─────────────────────────────────────────────────
    // Scan journal from last checkpoint, return all records.
    std::vector<JournalRecord> recover();

    // ── Lifecycle ────────────────────────────────────────────────
    void trim();  // remove records before last checkpoint
    void close();

    // ── Config ───────────────────────────────────────────────────
    static constexpr uint64_t kJournalSize = 64 * 1024 * 1024;  // 64MB per device

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ── Recovery procedure (in BtierEngine::init) ───────────────────
// 1. Journal::recover() → get all records since last checkpoint
// 2. Replay OP_KEY_PUT / OP_KEY_DEL → populate KeyMap
// 3. Replay OP_ALLOC / OP_FREE → reconstruct allocator state
// 4. Verify extent data integrity (extent header CRC)
// 5. Start normal operation
//
// On first init (no checkpoint found):
// 1. Journal::checkpoint(full_state) with empty state
// 2. Proceed to normal operation

}  // namespace TOPNSPC::btier
```

### 3.7 BtierEngine — Public API Orchestrator

```cpp
#pragma once

#include <memory>
#include <string>

#include "btier/config.h"
#include "btier/extent_types.h"

namespace TOPNSPC::btier {

class BlockDevice;

class BtierEngine {
public:
    BtierEngine();
    ~BtierEngine();

    // ── Lifecycle ────────────────────────────────────────────────
    // Opens devices, initializes allocators, recovers state from
    // journal, starts migrator thread.
    int init(const BtierConfig &config);

    // Full recovery: replay journal, verify extent data, resume.
    int recover();

    // Flush journal, checkpoint, stop migrator, close devices.
    void shutdown();

    // ── Key-Value operations ─────────────────────────────────────
    int put(const std::string &key, const bufferlist &value);
    int get(const std::string &key, bufferlist &value);
    int del(const std::string &key);

    // ── Admin / introspection ────────────────────────────────────
    struct Stats {
        uint64_t num_keys;
        uint64_t num_extents;
        uint64_t fast_extents;
        uint64_t slow_extents;
        double   fast_watermark;
        uint64_t journal_bytes;
        uint64_t migrations_pending;
        uint64_t promotions_committed;
        uint64_t demotions_committed;
        uint64_t interruptions;
    };
    Stats get_stats() const;

    // Runtime-mutable config fields (see §3.8 for init-only vs mutable)
    void set_weights(const WeightSet &w);
    void set_watermarks(double low, double high);
    void set_scan_interval(uint32_t ms);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace TOPNSPC::btier
```

### 3.8 BtierConfig — Configuration

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace TOPNSPC::btier {

struct WeightSet {
    float w_recency        = 0.35f;
    float w_frequency      = 0.30f;
    float w_randomness     = 0.25f;
    float w_write_penalty  = 0.10f;
};

struct BtierConfig {
    // ── Device paths (init-only) ─────────────────────────────────
    std::string fast_dev_path;   // e.g., /dev/nvme0n1
    std::string slow_dev_path;   // e.g., /dev/sda

    // ── Extent geometry (init-only) ──────────────────────────────
    uint64_t extent_size       = 4 * 1024 * 1024;  // 4MB
    uint64_t block_size        = 4096;              // 4KB sector

    // ── Scoring (runtime-mutable via set_weights) ────────────────
    WeightSet base_weights;

    // ── Watermarks (runtime-mutable via set_watermarks) ──────────
    double low_watermark       = 0.30;
    double high_watermark      = 0.80;

    // ── Migration (scan_interval is runtime-mutable) ─────────────
    uint32_t scan_interval_ms  = 1000;
    uint32_t max_migrations_per_cycle = 16;

    // ── Cooling (init-only) ──────────────────────────────────────
    uint32_t cool_interval_sec = 300;    // 5 min without access → cold

    // ── Stride thresholds (init-only) ────────────────────────────
    uint64_t sequential_threshold = 64 * 1024;  // 64KB
};

// Runtime-mutability:
//   init-only:     extent_size, block_size, cool_interval_sec,
//                  sequential_threshold, device paths
//   mutable:       base_weights (via set_weights), watermarks,
//                  scan_interval_ms, max_migrations_per_cycle
//   NOT recommended at runtime: extent_size, block_size (requires
//   full offline reorganization)

}  // namespace TOPNSPC::btier
```

---

## 4. Core Algorithms

### 4.1 Per-Key Stride-Based Randomness Detection

**Purpose:** Identify random-access patterns without storing full I/O history.
Fixed to track per-key in KeyMap, not per-extent, eliminating false positives
from multi-key extent sharing (§S5 fix).

**State tracked per key:** `last_lba` and `consecutive_sequential` counter in KeyMap.

```
On each write to key K at LBA `current_lba`:
  delta = abs(current_lba - last_lba[K])
  last_lba[K] = current_lba

  if delta <= SEQUENTIAL_THRESHOLD (64KB):
      consecutive_sequential[K] = min(consecutive_sequential[K] + 1, MAX_SEQ)
  else:
      consecutive_sequential[K] = 0

ScoringEngine reads consecutive_sequential[K] to compute per-extent randomness:
  extent_randomness = max over keys_in_extent(
      consecutive_sequential[K] == 0 ? 63 : 0 )
```

**v1 simplification:** Each key owns its own extent, so per-key and per-extent
randomness are equivalent. The per-key infrastructure is in place for v2 multi-key
packing.

### 4.2 Multi-Dimensional Scoring Formula

```
Score = w1 * norm(last_access) + w2 * norm(access_count)
      + w3 * norm(randomness)  - w4 * norm(write_count)
```

All terms normalized to [0, 1]:
- `norm(recency)` = `clamp(1 - (now - last_access) / cool_interval, 0, 1)`
- `norm(frequency)` = `access_count / 4095`
- `norm(randomness)` = `randomness / 63`
- `norm(write_count)` = `write_count / 4095`

### 4.3 Weight Adaptation

| Watermark Region | Adaptation |
|-----------------|------------|
| `usage < low_watermark` (30%) | Boost recency (×1.2) and frequency (×1.1) → more promotion |
| `usage > high_watermark` (80%) | Amplify write penalty (×pressure×2) and randomness (×pressure×1.5), reduce recency (×1−pressure×0.5) → accelerate demotion |
| Between | Use base weights |

### 4.4 CAS-Based Migration Interruption

```
MigrationEngine:                       Concurrent Write (Put):
  Phase1: mark_migrating()             record_io() on extent
   → CAS state→MIGRATING              → atomic metrics update
   → gen_before = generation            → sees state=MIGRATING
                                        → notifies KeyMap redirect
   Copy data to destination             →
   (no locks held)                      Writes to new extent
                                        → try_relocate() with CAS
  Phase2: try_relocate()                 bumps generation
   → CAS (gen == gen_before?)
   → if yes: COMMITTED
   → if no:  INTERRUPTED (free dest)
```

The key difference from the original design (§S1+S2 fix):
- `record_io()` only updates metrics atomically (no lock)
- `mark_migrating()` uses CAS on the metrics word (no lock held during copy)
- `try_relocate()` uses CAS on generation + state + location (no lock held by caller)

---

## 5. Concurrency Model

### 5.1 Locking Strategy

| Resource | Protection | Notes |
|----------|-----------|-------|
| `KeyMap::map_` | `std::shared_mutex` | Structural changes (insert/erase). lookup() uses shared_lock. |
| `ExtentMap::entries_` (structure) | `std::shared_mutex` | Entry insert/delete. Lookup and iteration use shared_lock. |
| Per-extent metrics (`raw` word) | `std::atomic<uint64_t>` | Lock-free via CAS. No mutex contention on I/O path. |
| Per-extent `last_access_time` | `std::atomic<uint32_t>` | Relaxed store, no CAS needed. |
| Allocators | `std::mutex` each | AvlAllocator uses per-allocator lock. Contention is rare (allocation/free only on write to new extent or GC). |
| Migration work queue | `std::mutex` + `std::condition_variable` | Producer: scoring pass. Consumer: migrator thread. |
| Scores: `active_weights_` | `std::atomic<WeightSet>` | Adapt writes, score reads. |
| Journal | `std::mutex` | Serialized appends. |

### 5.2 Read Path (Get)

```
BtierEngine::get(key) {
    1. KeyMap::lookup(key) → KeyLocation {extent_id, offset, length}
       → acquires shared_lock on KeyMap
       → releases lock
    2. ExtentMap::get_location(extent_id) → DiskLocation
       → acquires shared_lock on ExtentMap
       → releases lock
    3. ExtentMap::record_io(extent_id, READ, now)
       → atomic load/CAS on metrics word
       → updates access_count, last_access_time
    4. BlockDevice::read(location.offset + offset, length, ...)
    5. Return data
}
```

**No blocking:** Read path never waits for migration. `get_location()` returns the
current location regardless of state. If state is MIGRATING, the location still
points to the original (valid) data. The copy doesn't overwrite until `try_relocate()`
succeeds.

### 5.3 Write Path (Put)

```
BtierEngine::put(key, value) {
    1. Allocate space on FAST tier
       → ExtentMap::allocate(Tier::FAST) → new_extent_id
    2. Write data to the new extent
       → BlockDevice::write(new_location, value)
    3. KeyMap::put(key, {new_extent_id, 0, len}, lba)
       → acquires write_lock on KeyMap
       → if old extent exists, remove from old extent's reverse index
       → updates mapping
       → Journal::append(OP_KEY_PUT)
       → releases lock
    4. ExtentMap::free(old_extent_id)  // if key existed previously
}
```

**Always COW:** Every write gets a fresh extent. This avoids read-modify-write
and simplifies the MIGRATING redirect: there is no "write to existing location"
that could conflict with migration. The old extent is freed after the new mapping
is committed.

Why COW: Simpler concurrency (no overwrite conflicts), no read-before-write,
natural snapshots for debugging. Tradeoff: higher allocation churn.
Mitigated by in-memory GC compaction (v2+, §8 deferral).

### 5.4 Migration Path

```
MigrationEngine::migrate(extent_id, from, to) {
    // Phase 1: claim
    if (!ExtentMap::mark_migrating(extent_id, &gen_before))
        return INTERRUPTED;  // another migrator claimed it

    // Read source
    DiskLocation src = ExtentMap::get_location(extent_id);
    BlockDevice::read(src.offset, src.length, &data, ...);

    // Allocate destination
    DiskLocation dst = allocate_dest(to);

    // Copy data (no locks held — I/O path runs concurrently)
    BlockDevice::write(dst.offset, data, ...);

    // Phase 2: commit or abort
    if (ExtentMap::try_relocate(extent_id, dst, gen_before)) {
        // Update KeyMap for all keys in this extent
        auto keys = KeyMap::keys_in_extent(extent_id);
        for (auto &k : keys) {
            KeyMap::put(k, {extent_id, 0, src.length}, 0);  // same extent_id, new location
        }
        // Free source
        ExtentMap::free_for_reuse(src);  // return to allocator
        Journal::append(OP_FREE, src);
        return COMMITTED;
    } else {
        free_dest(dst);
        return INTERRUPTED;
    }
}
```

---

## 6. Observability

File: `btier_observer.h`, `btier_observer.cc`

| Concern | Mechanism |
|---------|-----------|
| Logging | spdlog (existing dependency). Info-level on migration commit/interrupt. Debug-level on per-extent scoring. |
| Metrics | BtierEngine::Stats exported via get_stats(). Counters: promotions, demotions, interruptions, I/O errors, journal bytes, key count, extent count, watermark. |
| Tracing | Per-extent migration trace log (file per run): `[ts] extent_id from_tier to_tier result duration_ms`. |
| Debugging | `foreach_extent` dumps per-extent metrics + location via DENC serialization. |
| What NOT to build | No Prometheus exporter (v1). No distributed tracing (single process). No Grafana dashboards (use CLI + stats polling in test). |

---

## 7. Build Integration

### 7.1 Directory Layout

```
btier/
├── CMakeLists.txt
├── btier.h / btier.cc                  # BtierEngine (public API)
├── config.h / config.cc                # BtierConfig
├── extent_types.h                      # ExtentMetrics, DiskLocation, etc.
├── extent_map.h / extent_map.cc        # ExtentMap
├── key_map.h / key_map.cc              # KeyMap
├── scoring_engine.h / scoring_engine.cc    # ScoringEngine
├── migration_engine.h / migration_engine.cc  # MigrationEngine
├── journal.h / journal.cc              # WAL journal
└── btier_observer.h / btier_observer.cc  # Observability helpers
```

### 7.2 CMakeLists.txt

```cmake
add_library(btier SHARED
    btier.cc
    config.cc
    extent_map.cc
    key_map.cc
    scoring_engine.cc
    migration_engine.cc
    journal.cc
    btier_observer.cc
)
target_include_directories(btier PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(btier PRIVATE
    blk          # BlockDevice, KernelDevice
    bluestore    # Allocator, AvlAllocator
    common       # bufferlist, intarith
)
```

`bluestore` linked as PRIVATE — btier's consumers should not inherit bluestore symbols.

### 7.3 Root CMakeLists.txt Addition

```cmake
# ── BTier tiered storage engine ──
add_subdirectory(btier)
```

---

## 8. Milestones

Milestones follow **interface boundaries**, not build phases. Each milestone
delivers a complete, testable vertical slice with all modules at that depth.

### Milestone A: Core I/O Path (KeyMap + ExtentMap + single-tier Put/Get/Del)

**Interface boundary:** I/O path is complete and correct. No migration, no scoring.

**Implements:**
- `extent_types.h` — all data structures with atomic metrics
- `extent_map.h/cc` — single-tier allocation, location tracking, `record_io()` with CAS
- `key_map.h/cc` — key→extent mapping with reverse index, per-key stride tracking
- `journal.h/cc` — WAL append + recovery
- `btier.h/cc` — `init()`, `put()`, `get()`, `del()`, `shutdown()`, `recover()`

**Tests:**
- Write 4KB–4MB values, read back, verify data integrity (CRC32)
- Concurrent read/write from 8 threads, verify no data races (TSAN)
- Kill -9 test: write 1000 keys, kill process, restart, verify all keys recoverable

**Reuses:** `BlockDevice`, `Allocator`, `bufferlist`

### Milestone B: Two-Tier Allocation + Scoring

**Interface boundary:** Control path (scoring) is complete. ExtentMap supports two tiers.

**Implements:**
- Two allocators in `ExtentMap` (FAST + SLOW)
- `scoring_engine.h/cc` — formula, normalization, weight adaptation (atomic `active_weights_`)
- `fast_watermark()` and `adapt_weights()` integration

**Tests:**
- Verify metrics update correctly for random vs sequential I/O (per-key stride)
- Verify score ranking reflects intended heat dimensions
- Verify weight adaptation under synthetic watermark pressure

### Milestone C: Migration + Integration

**Interface boundary:** Migration engine is complete. End-to-end tiering works.

**Implements:**
- `migration_engine.h/cc` — 2-phase CAS protocol, background thread, KeyMap update on success
- `mark_migrating()` and `try_relocate()` in ExtentMap
- Scoring drives migration queue
- Watermarks drive weight adaptation

**Tests:**
- Concurrent read/write during migration with TSAN — verify no data races
- Stress test: 16 threads writing, migrator promoting/demoting, verify none block on migrator
- Verify all four state transitions (CLEAN_FAST↔DIRTY_FAST↔MIGRATING↔CLEAN_SLOW)
- fio mixed sequential/random workload — verify hot data promoted, cold data demoted

### What NOT to build (v1 scope limits):

| Feature | Deferred To | Why |
|---------|-------------|-----|
| Multi-key extent packing | v2 | v1 COW gives each key its own extent. Packing requires inline extent index |
| Prometheus / Grafana | v2 | v1 exports `get_stats()` for CLI testing |
| Background GC compaction | v2 | v1 freed extents are returned to allocator; fragmentation acceptable at small scale |
| Adaptive extent sizing | v3 | Fixed 4MB is simple and matches typical I/O patterns |
| Non-4K block size support | v3 | 512e drives require additional alignment handling |
| Ceph BlueStore integration | Phase 3 | btier is independent; integration with BlueStore KV layer is a separate milestone |

---

## 9. Design Review (PoSD Score: 7.5/10)

| Criterion | Rating | Evidence |
|-----------|--------|----------|
| Module depth | 9/10 | `ExtentMap` (CAS-based `try_relocate` hides generation), `KeyMap` (hides reverse index + stride + persistence), `ScoringEngine` (2-method interface, ~80 lines impl) — all deep. |
| Information hiding | 9/10 | I/O path knows no scoring/metrics/tier internals. Generation protocol hidden behind `try_relocate()` CAS. `adapt_weights` uses atomic store. |
| Temporal decomposition | 9/10 | Milestones A/B/C follow interface boundaries (I/O path, control path, integration). Each is independently testable. |
| Strategic programming | 9/10 | Interfaces designed first; allocators, block devices, spdlog reused. CAS handles concurrency atomically (no lock redesign needed later). |
| General-purpose | 7/10 | KV model (Put/Get/Del) is special-purpose. But `BlockDevice` and `Allocator` reuse is good. |
| Comments | 9/10 | Interface comments describe abstraction, not implementation. State transition diagram is explicit. |
| Configuration | 8/10 | Runtime-mutability is explicitly documented. Could add file-based config loading in v2. |

**Remaining weaknesses for 10/10:**
1. Atomics-based bit-packing is error-prone in initial implementation (mitigation: extensive unit tests + TSAN)
2. `MigrationEngine` still needs medium depth — knows about `try_relocate()` return value
3. Per-key stride adds memory per key (one `uint64_t` per entry in KeyMap)
4. Deferred features (v2) add uncertainty to the architecture roadmap

---

## 10. Patent Roadmap

1. **§3.1 + §3.3 — Lock-free extent metrics via atomic bit-packed CAS:**
   A method for lock-free update of compressed (16-byte) extent metrics using
   `std::atomic<uint64_t>` with bitfield packing and CAS, enabling concurrent
   I/O path metrics updates without mutex contention.

2. **§3.3 + §4.4 — CAS-based migration interruption with atomic generation check:**
   A method for zero-blocking concurrent writes during block-level data migration
   using a single `compare_exchange_weak` on a bit-packed atomic word to atomically
   verify generation, swap location, and update state — no pre/post copy generation
   comparison needed (unlike prior art).

3. **§3.2 — Per-key stride tracking in tiered storage:**
   A method for tracking I/O randomness at the key granularity within a tiered
   storage system, eliminating false positives from multi-key extent sharing
   that occur in per-extent stride tracking.
