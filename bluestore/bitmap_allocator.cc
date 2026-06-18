#include "bluestore/bitmap_allocator.h"

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

}  // namespace TOPNSPC
