#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "btier/btier_types.h"
#include "common/buffer_fwd.h"
#include "common/common_fwd.h"

namespace TOPNSPC {

class BlockDevice;

namespace btier {

enum JournalOp : uint8_t {
    OP_TXN_BEGIN = 1,
    OP_KEY_PUT = 2,
    OP_KEY_DEL = 3,
    OP_MARK_DEAD = 4,
    OP_EXTENT_NEW = 5,
    OP_EXTENT_FREE = 6,
    OP_TXN_COMMIT = 7,
    OP_CHECKPOINT = 8,
};

struct JournalRecord {
    JournalOp op;
    uint64_t txn_id = 0;
    std::string key;
    KeyLocation key_loc;
    uint64_t extent_id = 0;
    DiskLocation extent_loc;
    uint32_t dead_length = 0;
    uint32_t crc = 0;
};

class Journal {
public:
    Journal(BlockDevice *dev, uint64_t dev_size);
    ~Journal();

    uint64_t begin_txn();
    int append(uint64_t txn_id, const JournalRecord &rec);
    int commit_txn(uint64_t txn_id);

    int checkpoint(const std::vector<JournalRecord> &full_state);

    std::vector<JournalRecord> recover();

    void sync();
    void trim();
    void close();

    // ── Space management ─────────────────────────────────────────
    // Returns bytes used between checkpoint and write head.
    uint64_t get_used_bytes() const;

    // Returns usage ratio [0.0, 1.0].
    double get_usage() const;

    // Returns true if journal usage >= 80% (should trigger async checkpoint).
    bool needs_checkpoint() const;

    // Returns true if journal usage >= 95% (put() should block/backpressure).
    bool is_near_full() const;

    static constexpr uint64_t kJournalSize = 64 * 1024 * 1024;
    static constexpr uint64_t kSuperblockSize = 4096;
    static constexpr uint64_t kDataStart = kSuperblockSize;
    static constexpr uint64_t kTxnAlignment = 4096;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace btier
}  // namespace TOPNSPC
