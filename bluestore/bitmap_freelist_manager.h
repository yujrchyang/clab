#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "bluestore/freelist_manager.h"
#include "common/buffer.h"
#include "kv/key_value_db.h"

namespace TOPNSPC {

class BitmapFreelistManager : public FreelistManager {
    std::string meta_prefix_;
    std::string bitmap_prefix_;

    uint64_t size_ = 0;
    uint64_t bytes_per_block_ = 0;
    uint64_t blocks_per_key_ = 128;
    uint64_t bytes_per_key_ = 0;
    uint64_t blocks_ = 0;

    uint64_t block_mask_ = 0;
    uint64_t key_mask_ = 0;

    bufferlist all_set_bl_;

    Iterator enumerate_p_;
    uint64_t enumerate_offset_ = 0;
    bufferlist enumerate_bl_;
    int enumerate_bl_pos_ = 0;

    std::mutex lock_;

    uint64_t _get_offset(uint64_t key_off, int bit) const {
        return key_off + bit * bytes_per_block_;
    }

    void _init_misc();
    void _xor(uint64_t offset, uint64_t length,
              Transaction txn);
    int _read_cfg(std::function<int(const std::string &, std::string *)> cfg_reader);
    void _load_from_db(KeyValueDB *kvdb);
    uint64_t _size_2_block_count(uint64_t target_size) const;

public:
    BitmapFreelistManager(std::string meta_prefix,
                          std::string bitmap_prefix);

    int create(uint64_t size, uint64_t granularity,
               Transaction txn) override;

    int init(KeyValueDB *kvdb, bool db_in_read_only,
             std::function<int(const std::string &, std::string *)> cfg_reader) override;

    void shutdown() override;

    void enumerate_reset() override;
    bool enumerate_next(KeyValueDB *kvdb,
                        uint64_t *offset, uint64_t *length) override;

    void allocate(uint64_t offset, uint64_t length,
                  Transaction txn) override;
    void release(uint64_t offset, uint64_t length,
                 Transaction txn) override;

    uint64_t get_size() const override { return size_; }
    uint64_t get_alloc_units() const override { return blocks_; }
    uint64_t get_alloc_size() const override { return bytes_per_block_; }

    void get_meta(uint64_t target_size,
                  std::vector<std::pair<std::string, std::string>> *) const override;
};

}  // namespace TOPNSPC
