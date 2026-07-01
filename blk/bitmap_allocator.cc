#include "blk/bitmap_allocator.h"

#include <cerrno>

#include "common/intarith.h"

namespace TOPNSPC {

template class AllocatorLevel02<AllocatorLevel01Loose>;

BitmapAllocator::BitmapAllocator(int64_t capacity, int64_t alloc_unit,
                                 std::string_view name)
    : Allocator(name, capacity, alloc_unit) {
    _init(capacity, alloc_unit, false);
}

int64_t BitmapAllocator::allocate(uint64_t want, uint64_t unit,
                                  uint64_t max_alloc_size, int64_t hint,
                                  PExtentVector *extents) {
    clab_assert(isp2(unit));
    clab_assert(want % unit == 0);
    clab_assert(unit >= l1.l0_granularity);
    uint64_t allocated = 0;
    int r = _allocate_l2(want, unit, max_alloc_size, hint,
                         &allocated, extents);
    return (r == 0 && allocated > 0) ? static_cast<int64_t>(allocated) : -ENOSPC;
}

void BitmapAllocator::release(const interval_set<uint64_t> &release_set) {
    _free_l2(release_set);
}

uint64_t BitmapAllocator::get_free() {
    std::lock_guard l(lock);
    return available;
}

double BitmapAllocator::get_fragmentation() {
    std::lock_guard l(lock);
    uint64_t total = l1.unalloc_l1_count + l1.partial_l1_count;
    if (total == 0)
        return 0.0;
    return static_cast<double>(l1.partial_l1_count) / total;
}

void BitmapAllocator::dump() {
    std::map<size_t, size_t> bins;
    collect_stats(bins);
    for ([[maybe_unused]] auto &[bin, count] : bins) {
    }
}

void BitmapAllocator::foreach (
    std::function<void(uint64_t offset, uint64_t length)> notify) {
    foreach_internal(notify);
}

void BitmapAllocator::init_add_free(uint64_t offset, uint64_t length) {
    if (!length) return;
    std::lock_guard l(lock);
    uint64_t off = round_up(offset, l1.l0_granularity);
    length = align_down(offset + length - off, l1.l0_granularity);
    if (length == 0) return;
    _mark_free(off, length);
}

void BitmapAllocator::init_rm_free(uint64_t offset, uint64_t length) {
    if (!length) return;
    std::lock_guard l(lock);
    uint64_t off = round_up(offset, l1.l0_granularity);
    length = align_down(offset + length - off, l1.l0_granularity);
    if (length == 0) return;
    _mark_allocated(off, length);
}

void BitmapAllocator::shutdown() {
    std::lock_guard l(lock);
    _shutdown();
}

// =====================================================================
// AllocatorLevel01Loose — L0 claim methods
// =====================================================================

uint64_t AllocatorLevel01Loose::_claim_free_to_left_l0(int64_t l0_pos_start) {
    auto d0 = bits_per_slot;
    int64_t pos = l0_pos_start - 1;
    slot_t bits = slot_t(1) << (pos % d0);
    int64_t idx = pos / d0;
    slot_t *val_s = l0.data() + idx;
    int64_t pos_e = p2align<int64_t>(pos, d0);

    while (pos >= pos_e) {
        if (0 == ((*val_s) & bits))
            return uint64_t(pos + 1);
        (*val_s) &= ~bits;
        bits >>= 1;
        --pos;
    }

    --idx;
    val_s = l0.data() + idx;
    while (idx >= 0 && (*val_s) == all_slot_set) {
        *val_s = all_slot_clear;
        --idx;
        pos -= d0;
        val_s = l0.data() + idx;
    }

    if (idx >= 0 && (*val_s) != all_slot_set && (*val_s) != all_slot_clear) {
        int64_t pe = p2align<int64_t>(pos, d0);
        bits = slot_t(1) << (pos % d0);
        while (pos >= pe) {
            if (0 == ((*val_s) & bits))
                return uint64_t(pos + 1);
            (*val_s) &= ~bits;
            bits >>= 1;
            --pos;
        }
    }
    return uint64_t(pos + 1);
}

uint64_t AllocatorLevel01Loose::_claim_free_to_right_l0(int64_t l0_pos_start) {
    auto d0 = bits_per_slot;
    int64_t pos = l0_pos_start;
    slot_t bits = slot_t(1) << (pos % d0);
    size_t idx = size_t(pos / d0);
    if (idx >= l0.size())
        return uint64_t(pos);
    slot_t *val_s = l0.data() + idx;
    int64_t pos_e = p2roundup<int64_t>(pos + 1, d0);

    while (pos < pos_e) {
        if (0 == ((*val_s) & bits))
            return uint64_t(pos);
        (*val_s) &= ~bits;
        bits <<= 1;
        ++pos;
    }

    ++idx;
    val_s = l0.data() + idx;
    while (idx < l0.size() && (*val_s) == all_slot_set) {
        *val_s = all_slot_clear;
        ++idx;
        pos += d0;
        val_s = l0.data() + idx;
    }

    if (idx < l0.size() && (*val_s) != all_slot_set && (*val_s) != all_slot_clear) {
        int64_t pe = p2roundup<int64_t>(pos + 1, d0);
        bits = slot_t(1) << (pos % d0);
        while (pos < pe) {
            if (0 == ((*val_s) & bits))
                return uint64_t(pos);
            (*val_s) &= ~bits;
            bits <<= 1;
            ++pos;
        }
    }
    return uint64_t(pos);
}

uint64_t AllocatorLevel01Loose::claim_free_to_left_l1(uint64_t offs) {
    uint64_t l0_pos_end = offs / l0_granularity;
    uint64_t l0_pos_start = _claim_free_to_left_l0(int64_t(l0_pos_end));
    if (l0_pos_start < l0_pos_end) {
        _mark_l1_on_l0(
            p2align(l0_pos_start, uint64_t(bits_per_slotset)),
            p2roundup(l0_pos_end, uint64_t(bits_per_slotset)));
        return l0_granularity * (l0_pos_end - l0_pos_start);
    }
    return 0;
}

uint64_t AllocatorLevel01Loose::claim_free_to_right_l1(uint64_t offs) {
    uint64_t l0_pos_start = offs / l0_granularity;
    uint64_t l0_pos_end = _claim_free_to_right_l0(int64_t(l0_pos_start));
    if (l0_pos_start < l0_pos_end) {
        _mark_l1_on_l0(
            p2align(l0_pos_start, uint64_t(bits_per_slotset)),
            p2roundup(l0_pos_end, uint64_t(bits_per_slotset)));
        return l0_granularity * (l0_pos_end - l0_pos_start);
    }
    return 0;
}

// =====================================================================
// AllocatorLevel02 — claim-free forwarding with L2 update
// =====================================================================

template <typename L1>
uint64_t AllocatorLevel02<L1>::claim_free_to_left(uint64_t offset) {
    std::lock_guard l(lock);
    auto allocated = l1.claim_free_to_left_l1(offset);
    clab_assert(available >= allocated);
    available -= allocated;

    uint64_t l2_pos = (offset - allocated) / l2_granularity;
    uint64_t l2_pos_end =
        p2roundup(int64_t(offset), int64_t(l2_granularity)) / l2_granularity;
    _mark_l2_on_l1(l2_pos, l2_pos_end);
    return allocated;
}

template <typename L1>
uint64_t AllocatorLevel02<L1>::claim_free_to_right(uint64_t offset) {
    std::lock_guard l(lock);
    auto allocated = l1.claim_free_to_right_l1(offset);
    clab_assert(available >= allocated);
    available -= allocated;

    uint64_t l2_pos = offset / l2_granularity;
    int64_t end = int64_t(offset + allocated);
    uint64_t l2_pos_end =
        p2roundup(end, int64_t(l2_granularity)) / l2_granularity;
    _mark_l2_on_l1(l2_pos, l2_pos_end);
    return allocated;
}

}  // namespace TOPNSPC
