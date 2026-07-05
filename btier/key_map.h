#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "btier/btier_types.h"

namespace TOPNSPC::btier {

class Journal;

class KeyMap {
public:
    KeyMap();
    ~KeyMap();

    bool lookup(const std::string &key, KeyLocation *loc) const;
    void put(const std::string &key, const KeyLocation &loc, uint64_t lba);
    void erase(const std::string &key);

    std::unordered_set<std::string> keys_in_extent(uint64_t extent_id) const;

    void batch_update(
        const std::vector<std::pair<std::string, KeyLocation>> &updates);

    uint32_t get_consecutive_sequential(const std::string &key) const;

    void persist(Journal *journal);
    void recover(Journal *journal);

    size_t size() const;
    size_t keys_in_extent_count(uint64_t extent_id) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace TOPNSPC::btier
