#include "btier/key_map.h"

#include <shared_mutex>

namespace TOPNSPC::btier {

namespace {

constexpr uint64_t kSequentialThreshold = 64 * 1024;
constexpr uint32_t kMaxSeqAward = 63;

struct KeyEntry {
    KeyLocation loc;
    uint64_t last_lba = 0;
    uint32_t consecutive_sequential = 0;
};

}  // anonymous namespace

struct KeyMap::Impl {
    mutable std::shared_mutex lock_;

    std::unordered_map<std::string, KeyEntry> map_;
    std::unordered_map<uint64_t, std::unordered_set<std::string>> reverse_index_;
};

KeyMap::KeyMap() : impl_(std::make_unique<Impl>()) {}
KeyMap::~KeyMap() = default;

bool KeyMap::lookup(const std::string &key, KeyLocation *loc) const {
    std::shared_lock lock(impl_->lock_);
    auto it = impl_->map_.find(key);
    if (it == impl_->map_.end()) return false;
    if (loc) *loc = it->second.loc;
    return true;
}

void KeyMap::put(const std::string &key, const KeyLocation &loc, uint64_t lba) {
    std::unique_lock lock(impl_->lock_);

    bool existing = impl_->map_.count(key) > 0;
    auto &entry = impl_->map_[key];

    if (existing) {
        // Existing key — check if old extent needs reverse index cleanup
        if (entry.loc.extent_id != loc.extent_id) {
            auto it = impl_->reverse_index_.find(entry.loc.extent_id);
            if (it != impl_->reverse_index_.end()) {
                it->second.erase(key);
                if (it->second.empty())
                    impl_->reverse_index_.erase(it);
            }
        }
    }

    // Stride tracking
    uint64_t prev_lba = entry.last_lba;
    uint64_t delta = (lba > prev_lba) ? lba - prev_lba : prev_lba - lba;
    entry.last_lba = lba;

    if (delta <= kSequentialThreshold) {
        if (entry.consecutive_sequential < kMaxSeqAward)
            entry.consecutive_sequential++;
    } else {
        entry.consecutive_sequential = 0;
    }

    // Update location
    entry.loc = loc;

    // Update reverse index
    impl_->reverse_index_[loc.extent_id].insert(key);
}

void KeyMap::erase(const std::string &key) {
    std::unique_lock lock(impl_->lock_);
    auto it = impl_->map_.find(key);
    if (it == impl_->map_.end()) return;

    uint64_t old_extent = it->second.loc.extent_id;
    impl_->map_.erase(it);

    auto rit = impl_->reverse_index_.find(old_extent);
    if (rit != impl_->reverse_index_.end()) {
        rit->second.erase(key);
        if (rit->second.empty())
            impl_->reverse_index_.erase(rit);
    }
}

std::unordered_set<std::string>
KeyMap::keys_in_extent(uint64_t extent_id) const {
    std::shared_lock lock(impl_->lock_);
    auto it = impl_->reverse_index_.find(extent_id);
    if (it == impl_->reverse_index_.end())
        return {};
    return it->second;
}

void KeyMap::batch_update(
    const std::vector<std::pair<std::string, KeyLocation>> &updates) {
    std::unique_lock lock(impl_->lock_);
    for (const auto &[key, new_loc] : updates) {
        auto it = impl_->map_.find(key);
        if (it != impl_->map_.end()) {
            uint64_t old_extent = it->second.loc.extent_id;
            if (old_extent != new_loc.extent_id) {
                auto rit = impl_->reverse_index_.find(old_extent);
                if (rit != impl_->reverse_index_.end()) {
                    rit->second.erase(key);
                    if (rit->second.empty())
                        impl_->reverse_index_.erase(rit);
                }
            }
            it->second.loc = new_loc;
        } else {
            KeyEntry entry;
            entry.loc = new_loc;
            impl_->map_[key] = entry;
        }
        impl_->reverse_index_[new_loc.extent_id].insert(key);
    }
}

uint32_t
KeyMap::get_consecutive_sequential(const std::string &key) const {
    std::shared_lock lock(impl_->lock_);
    auto it = impl_->map_.find(key);
    if (it == impl_->map_.end()) return 0;
    return it->second.consecutive_sequential;
}

void KeyMap::persist(Journal *journal) {
    // Stub — actual journal integration in A7
}

void KeyMap::recover(Journal *journal) {
    // Stub — actual journal integration in A7
}

size_t KeyMap::size() const {
    std::shared_lock lock(impl_->lock_);
    return impl_->map_.size();
}

size_t KeyMap::keys_in_extent_count(uint64_t extent_id) const {
    std::shared_lock lock(impl_->lock_);
    auto it = impl_->reverse_index_.find(extent_id);
    if (it == impl_->reverse_index_.end()) return 0;
    return it->second.size();
}

}  // namespace TOPNSPC::btier
