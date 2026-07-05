#include "btier/journal.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "blk/block_device.h"
#include "common/buffer.h"
#include "common/crc32.h"
#include "common/intarith.h"

namespace TOPNSPC::btier {

namespace {

// ── Serialization helpers ─────────────────────────────────────

void put_u8(bufferlist &bl, uint8_t v) {
    bl.append(reinterpret_cast<const char *>(&v), sizeof(v));
}

void put_u32(bufferlist &bl, uint32_t v) {
    bl.append(reinterpret_cast<const char *>(&v), sizeof(v));
}

void put_u64(bufferlist &bl, uint64_t v) {
    bl.append(reinterpret_cast<const char *>(&v), sizeof(v));
}

uint8_t get_u8(bufferlist::const_iterator &p) {
    uint8_t v = 0;
    p.copy(sizeof(v), reinterpret_cast<char *>(&v));
    return v;
}

uint32_t get_u32(bufferlist::const_iterator &p) {
    uint32_t v = 0;
    p.copy(sizeof(v), reinterpret_cast<char *>(&v));
    return v;
}

uint64_t get_u64(bufferlist::const_iterator &p) {
    uint64_t v = 0;
    p.copy(sizeof(v), reinterpret_cast<char *>(&v));
    return v;
}

void put_string(bufferlist &bl, const std::string &s) {
    put_u32(bl, (uint32_t)s.size());
    bl.append(s.data(), s.size());
}

std::string get_string(bufferlist::const_iterator &p) {
    uint32_t len = get_u32(p);
    std::string s;
    s.resize(len);
    p.copy(len, s.data());
    return s;
}

void put_key_location(bufferlist &bl, const KeyLocation &kl) {
    put_u64(bl, kl.extent_id);
    put_u32(bl, kl.offset);
    put_u32(bl, kl.length);
}

KeyLocation get_key_location(bufferlist::const_iterator &p) {
    KeyLocation kl;
    kl.extent_id = get_u64(p);
    kl.offset = get_u32(p);
    kl.length = get_u32(p);
    return kl;
}

void put_disk_location(bufferlist &bl, const DiskLocation &dl) {
    put_u64(bl, dl.offset);
    put_u32(bl, dl.length);
    put_u8(bl, (uint8_t)dl.tier);
}

DiskLocation get_disk_location(bufferlist::const_iterator &p) {
    DiskLocation dl;
    dl.offset = get_u64(p);
    dl.length = get_u32(p);
    dl.tier = (Tier)get_u8(p);
    return dl;
}

void serialize_record(bufferlist &bl, const JournalRecord &rec) {
    put_u8(bl, (uint8_t)rec.op);
    put_u64(bl, rec.txn_id);
    switch (rec.op) {
    case OP_KEY_PUT:
        put_string(bl, rec.key);
        put_key_location(bl, rec.key_loc);
        break;
    case OP_KEY_DEL:
        put_string(bl, rec.key);
        break;
    case OP_MARK_DEAD:
        put_u64(bl, rec.extent_id);
        put_u32(bl, rec.dead_length);
        break;
    case OP_EXTENT_NEW:
        put_u64(bl, rec.extent_id);
        put_disk_location(bl, rec.extent_loc);
        break;
    case OP_EXTENT_FREE:
        put_u64(bl, rec.extent_id);
        break;
    case OP_TXN_COMMIT:
        put_u32(bl, rec.crc);
        break;
    default:
        break;
    }
}

JournalRecord deserialize_record(bufferlist::const_iterator &p) {
    JournalRecord rec;
    rec.op = (JournalOp)get_u8(p);
    rec.txn_id = get_u64(p);
    switch (rec.op) {
    case OP_KEY_PUT:
        rec.key = get_string(p);
        rec.key_loc = get_key_location(p);
        break;
    case OP_KEY_DEL:
        rec.key = get_string(p);
        break;
    case OP_MARK_DEAD:
        rec.extent_id = get_u64(p);
        rec.dead_length = get_u32(p);
        break;
    case OP_EXTENT_NEW:
        rec.extent_id = get_u64(p);
        rec.extent_loc = get_disk_location(p);
        break;
    case OP_EXTENT_FREE:
        rec.extent_id = get_u64(p);
        break;
    case OP_TXN_COMMIT:
        rec.crc = get_u32(p);
        break;
    default:
        break;
    }
    return rec;
}

// ── Journal superblock (4KB at offset 0) ────────────────────────
struct JournalSuperBlock {
    static constexpr uint64_t MAGIC = 0x42544A5350423031ULL;  // "BTJSPB01" in big-endian bytes
    uint64_t magic = 0;
    uint64_t checkpoint_offset = 0;  // offset of last checkpoint
    uint64_t checkpoint_seqno = 0;   // seqno at checkpoint
    uint64_t write_offset = 0;       // current write head
    uint64_t pad[508];               // pad to 4KB: 4096 - 32 = 4064 = 508 * 8
};
static_assert(sizeof(JournalSuperBlock) == 4096,
              "JournalSuperBlock must be 4KB");

}  // anonymous namespace

// ── In-memory transaction buffer ────────────────────────────────
struct TxnBuffer {
    uint64_t txn_id;
    std::vector<JournalRecord> records;
    bufferlist serialized;  // accumulated serialized records (without BEGIN/COMMIT)
};

struct Journal::Impl {
    BlockDevice *dev;
    uint64_t dev_size;
    uint64_t data_size;  // dev_size - kSuperblockSize

    std::mutex lock_;
    uint64_t next_txn_id = 1;
    std::unordered_map<uint64_t, TxnBuffer> active_txns_;

    JournalSuperBlock superblock_;
    uint64_t write_offset = 0;  // relative to kDataStart
    bool initialized = false;

    Impl(BlockDevice *d, uint64_t ds)
        : dev(d), dev_size(ds), data_size(ds > kSuperblockSize ? ds - kSuperblockSize : 0) {}

    int read_superblock() {
        bufferlist bl;
        int r = dev->read(0, sizeof(JournalSuperBlock), &bl, nullptr, false);
        if (r < 0) return r;
        if (bl.length() < sizeof(JournalSuperBlock)) return -EIO;
        std::memcpy(&superblock_, bl.c_str(),
                    sizeof(JournalSuperBlock));
        if (superblock_.magic != JournalSuperBlock::MAGIC) {
            // First init
            superblock_ = {};
            superblock_.magic = JournalSuperBlock::MAGIC;
            superblock_.checkpoint_offset = 0;
            superblock_.checkpoint_seqno = 0;
            superblock_.write_offset = 0;
            write_offset = 0;
            return write_superblock();
        }
        write_offset = superblock_.write_offset;
        return 0;
    }

    int write_superblock() {
        bufferlist bl;
        bl.append(reinterpret_cast<const char *>(&superblock_),
                  sizeof(JournalSuperBlock));
        int r = dev->write(0, bl, false);
        if (r < 0) return r;
        return dev->flush();
    }

    int pad_and_write(bufferlist &bl) {
        // Pad to kTxnAlignment
        size_t len = bl.length();
        size_t padded = round_up_to(len, kTxnAlignment);
        if (padded == 0) padded = kTxnAlignment;
        if (padded > len) {
            std::string zeros(padded - len, '\0');
            bl.append(zeros);
        }
        // Handle wrap-around
        if (write_offset + padded > data_size) {
            // Wrap to beginning
            write_offset = 0;
        }
        uint64_t abs_offset = kDataStart + write_offset;
        int r = dev->write(abs_offset, bl, false);
        if (r < 0) return r;
        write_offset += padded;
        if (write_offset >= data_size) write_offset = 0;
        return dev->flush();
    }
};

Journal::Journal(BlockDevice *dev, uint64_t dev_size)
    : impl_(std::make_unique<Impl>(dev, dev_size)) {
    // Read or initialize superblock on construction
    impl_->read_superblock();
}

Journal::~Journal() = default;

uint64_t Journal::begin_txn() {
    std::lock_guard lock(impl_->lock_);
    uint64_t txn_id = impl_->next_txn_id++;
    TxnBuffer &tb = impl_->active_txns_[txn_id];
    tb.txn_id = txn_id;
    return txn_id;
}

int Journal::append(uint64_t txn_id, const JournalRecord &rec) {
    std::lock_guard lock(impl_->lock_);
    auto it = impl_->active_txns_.find(txn_id);
    if (it == impl_->active_txns_.end()) return -ENOENT;

    JournalRecord r = rec;
    r.txn_id = txn_id;
    serialize_record(it->second.serialized, r);
    it->second.records.push_back(r);
    return 0;
}

int Journal::commit_txn(uint64_t txn_id) {
    bufferlist txn_bl;

    {
        std::lock_guard lock(impl_->lock_);
        auto it = impl_->active_txns_.find(txn_id);
        if (it == impl_->active_txns_.end()) return -ENOENT;

        auto &tb = it->second;

        // Build the full transaction: BEGIN + records + COMMIT
        // Compute CRC over the serialized records (without BEGIN/COMMIT)
        uint32_t crc = 0;
        if (tb.serialized.length() > 0) {
            crc = calc_crc32(
                reinterpret_cast<const uint8_t *>(tb.serialized.c_str()),
                tb.serialized.length(), 0);
        }

        // BEGIN record
        JournalRecord begin_rec;
        begin_rec.op = OP_TXN_BEGIN;
        begin_rec.txn_id = txn_id;
        serialize_record(txn_bl, begin_rec);

        // Records
        txn_bl.append(tb.serialized);

        // COMMIT record
        JournalRecord commit_rec;
        commit_rec.op = OP_TXN_COMMIT;
        commit_rec.txn_id = txn_id;
        commit_rec.crc = crc;
        serialize_record(txn_bl, commit_rec);

        impl_->active_txns_.erase(it);

        // Write + fsync + superblock update — all under lock to prevent
        // concurrent commit_txn/checkpoint from corrupting write_offset.
        int r = impl_->pad_and_write(txn_bl);
        if (r < 0) return r;

        impl_->superblock_.write_offset = impl_->write_offset;
        impl_->write_superblock();
    }

    return 0;
}

int Journal::checkpoint(const std::vector<JournalRecord> &full_state) {
    // Build the checkpoint as a committed transaction so recover() can replay it.
    // Format: BEGIN | CHECKPOINT | state_records... | COMMIT(crc)

    // Serialize state records separately for CRC computation
    bufferlist records_bl;
    for (const auto &rec : full_state) {
        serialize_record(records_bl, rec);
    }

    uint32_t crc = 0;
    if (records_bl.length() > 0) {
        crc = calc_crc32(
            reinterpret_cast<const uint8_t *>(records_bl.c_str()),
            records_bl.length(), 0);
    }

    // Build full transaction (done outside lock — pure computation)
    bufferlist bl;

    JournalRecord begin_rec;
    begin_rec.op = OP_TXN_BEGIN;
    begin_rec.txn_id = 0;
    serialize_record(bl, begin_rec);

    JournalRecord ckpt;
    ckpt.op = OP_CHECKPOINT;
    ckpt.txn_id = 0;
    serialize_record(bl, ckpt);

    bl.append(records_bl);

    JournalRecord commit_rec;
    commit_rec.op = OP_TXN_COMMIT;
    commit_rec.txn_id = 0;
    commit_rec.crc = crc;
    serialize_record(bl, commit_rec);

    // Write + superblock update — all under lock
    std::lock_guard lock(impl_->lock_);
    uint64_t ckpt_start = impl_->write_offset;

    int r = impl_->pad_and_write(bl);
    if (r < 0) return r;

    // Set checkpoint_offset to the START of the checkpoint data
    impl_->superblock_.checkpoint_offset = ckpt_start;
    impl_->superblock_.write_offset = impl_->write_offset;
    impl_->superblock_.checkpoint_seqno++;
    impl_->write_superblock();

    return 0;
}

std::vector<JournalRecord> Journal::recover() {
    std::vector<JournalRecord> result;

    // Read superblock
    int r = impl_->read_superblock();
    if (r < 0) return result;

    // If checkpoint_offset == write_offset, no valid data to recover
    if (impl_->superblock_.checkpoint_offset == impl_->write_offset) {
        // Unless both are 0 and there's unwritten data (first init case)
        if (impl_->write_offset == 0) {
            // Check if there's any data at all
            bufferlist first;
            r = impl_->dev->read(kDataStart, kTxnAlignment, &first, nullptr, true);
            if (r < 0 || first.length() == 0) return result;
            bool all_zeros = true;
            for (size_t i = 0; i < first.length(); i++) {
                if (first[i] != 0) {
                    all_zeros = false;
                    break;
                }
            }
            if (all_zeros) return result;
        } else {
            return result;
        }
    }

    // Read the entire scan range into one buffer to handle records
    // that span 4K block boundaries.
    uint64_t scan_start = impl_->superblock_.checkpoint_offset;
    uint64_t scan_end = impl_->write_offset;

    // Calculate total bytes to read (handling wrap-around)
    uint64_t scan_len;
    if (scan_end >= scan_start) {
        scan_len = scan_end - scan_start;
    } else {
        scan_len = impl_->data_size - scan_start + scan_end;
    }

    bufferlist full_bl;
    uint64_t remaining_to_read = scan_len;
    uint64_t read_pos = scan_start;
    while (remaining_to_read > 0) {
        uint64_t to_read = std::min(kTxnAlignment, remaining_to_read);
        if (read_pos + to_read > impl_->data_size) {
            to_read = impl_->data_size - read_pos;
        }
        bufferlist chunk;
        r = impl_->dev->read(kDataStart + read_pos, to_read, &chunk, nullptr, true);
        if (r < 0 || chunk.length() == 0) break;
        full_bl.append(chunk);
        remaining_to_read -= to_read;
        read_pos += to_read;
        if (read_pos >= impl_->data_size) read_pos = 0;
    }

    // Parse records from the full buffer
    std::vector<JournalRecord> current_txn;
    bool in_txn = false;
    uint64_t current_txn_id = 0;

    auto p = full_bl.cbegin();
    while (!p.end()) {
        size_t remaining = full_bl.length() - p.get_off();
        if (remaining < 9) break;

        size_t before_off = p.get_off();
        JournalRecord rec = deserialize_record(p);

        // Skip zero padding between transactions (op=0 is not a valid JournalOp).
        // Advance to the next kTxnAlignment boundary for efficiency.
        if (rec.op == 0) {
            size_t next_boundary = (before_off / kTxnAlignment + 1) * kTxnAlignment;
            if (next_boundary >= full_bl.length()) break;
            auto temp = full_bl.cbegin();
            temp += next_boundary;
            p = std::move(temp);
            continue;
        }

        if (rec.op == OP_TXN_BEGIN) {
            in_txn = true;
            current_txn_id = rec.txn_id;
            current_txn.clear();
        } else if (rec.op == OP_TXN_COMMIT) {
            if (in_txn && rec.txn_id == current_txn_id) {
                bufferlist crc_bl;
                for (const auto &r2 : current_txn) {
                    serialize_record(crc_bl, r2);
                }
                uint32_t expected_crc = 0;
                if (crc_bl.length() > 0) {
                    expected_crc = calc_crc32(
                        reinterpret_cast<const uint8_t *>(crc_bl.c_str()),
                        crc_bl.length(), 0);
                }
                if (expected_crc == rec.crc) {
                    for (auto &r2 : current_txn) {
                        result.push_back(r2);
                    }
                }
            }
            in_txn = false;
            current_txn.clear();
        } else if (in_txn && rec.op != OP_CHECKPOINT) {
            current_txn.push_back(rec);
        }
    }

    return result;
}

void Journal::sync() {
    impl_->dev->flush();
}

void Journal::trim() {
    // Reclaim journal space before checkpoint_offset by advancing
    // checkpoint_offset to write_offset. This does NOT reset write_offset
    // (which would overwrite the checkpoint) — it just means recovery
    // will scan from the new checkpoint_offset.
    std::lock_guard lock(impl_->lock_);
    impl_->superblock_.checkpoint_offset = impl_->write_offset;
    impl_->superblock_.write_offset = impl_->write_offset;
    impl_->write_superblock();
}

void Journal::close() {
    impl_->dev->flush();
}

uint64_t Journal::get_used_bytes() const {
    uint64_t cp = impl_->superblock_.checkpoint_offset;
    uint64_t wp = impl_->write_offset;
    if (wp >= cp) {
        return wp - cp;
    }
    // Wrapped
    return impl_->data_size - cp + wp;
}

double Journal::get_usage() const {
    if (impl_->data_size == 0) return 1.0;
    return static_cast<double>(get_used_bytes()) /
        static_cast<double>(impl_->data_size);
}

bool Journal::needs_checkpoint() const {
    return get_usage() >= 0.80;
}

bool Journal::is_near_full() const {
    return get_usage() >= 0.95;
}

}  // namespace TOPNSPC::btier
