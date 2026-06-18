#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "common/common_fwd.h"
#include "common/intarith.h"

namespace TOPNSPC {

struct bluestore_pextent_t {
    uint64_t offset = 0;
    uint32_t length = 0;

    bluestore_pextent_t() = default;
    bluestore_pextent_t(uint64_t o, uint32_t l) : offset(o), length(l) {}
};

using PExtentVector = std::vector<bluestore_pextent_t>;

template <typename T>
class interval_set {
public:
    using Map = std::map<T, T>;

    class const_iterator {
        friend class interval_set;
        typename Map::const_iterator it_;
        explicit const_iterator(typename Map::const_iterator it) : it_(it) {}

    public:
        const auto &operator*() const { return *it_; }
        const auto *operator->() const { return &*it_; }
        const_iterator &operator++() {
            ++it_;
            return *this;
        }
        bool operator!=(const const_iterator &o) const { return it_ != o.it_; }
        T get_start() const { return it_->first; }
        T get_len() const { return it_->second; }
    };

    const_iterator begin() const { return const_iterator(m_.begin()); }
    const_iterator end() const { return const_iterator(m_.end()); }
    bool empty() const { return m_.empty(); }
    size_t size() const { return m_.size(); }

    void insert(T off, T len) {
        if (len == 0) return;
        auto it = m_.lower_bound(off);
        // merge with following
        while (it != m_.end() && it->first <= off + len) {
            len = std::max(len, it->first + it->second - off);
            it = m_.erase(it);
        }
        // merge with preceding
        if (it != m_.begin()) {
            auto prev = it;
            --prev;
            if (prev->first + prev->second >= off) {
                len = std::max(len, off + len - prev->first);
                off = prev->first;
                m_.erase(prev);
            }
        }
        m_[off] = len;
    }

    void erase(T off, T len) {
        if (len == 0) return;
        auto it = m_.lower_bound(off);
        if (it != m_.begin()) {
            auto prev = it;
            --prev;
            if (prev->first + prev->second > off) {
                auto left_end = off;
                if (prev->first < off) {
                    auto old_end = prev->second;
                    prev->second = off - prev->first;
                    if (off + len < prev->first + old_end) {
                        m_[off + len] = (prev->first + old_end) - (off + len);
                    }
                }
            }
        }
        T end = off + len;
        while (it != m_.end() && it->first < end) {
            T it_end = it->first + it->second;
            if (it_end > end) {
                m_[end] = it_end - end;
            }
            it = m_.erase(it);
        }
    }

    void clear() { m_.clear(); }

    void swap(interval_set &o) { m_.swap(o.m_); }

    T range_start() const {
        return m_.empty() ? T(0) : m_.begin()->first;
    }
    T range_end() const {
        return m_.empty() ? T(0) : m_.rbegin()->first + m_.rbegin()->second;
    }

    void insert(const interval_set &other) {
        for (auto &[off, len] : other.m_)
            insert(off, len);
    }

private:
    Map m_;
};

}  // namespace TOPNSPC
