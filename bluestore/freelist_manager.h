#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "kv/key_value_db.h"

namespace TOPNSPC {

class FreelistManager {
    bool null_manager_ = false;

public:
    virtual ~FreelistManager() = default;

    virtual int create(uint64_t size, uint64_t granularity,
                       Transaction txn) = 0;

    virtual int init(KeyValueDB *kvdb, bool db_in_read_only,
                     std::function<int(const std::string &, std::string *)> cfg_reader) = 0;

    virtual void shutdown() = 0;

    virtual void enumerate_reset() = 0;
    virtual bool enumerate_next(KeyValueDB *kvdb,
                                uint64_t *offset, uint64_t *length) = 0;

    virtual void allocate(uint64_t offset, uint64_t length,
                          Transaction txn) = 0;
    virtual void release(uint64_t offset, uint64_t length,
                         Transaction txn) = 0;

    virtual uint64_t get_size() const = 0;
    virtual uint64_t get_alloc_units() const = 0;
    virtual uint64_t get_alloc_size() const = 0;

    virtual void get_meta(uint64_t target_size,
                          std::vector<std::pair<std::string, std::string>> *) const = 0;

    bool is_null_manager() const { return null_manager_; }
    void set_null_manager() { null_manager_ = true; }
};

}  // namespace TOPNSPC
