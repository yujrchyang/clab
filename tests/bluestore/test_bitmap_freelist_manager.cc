#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "bluestore/bitmap_freelist_manager.h"
#include "bluestore/freelist_manager.h"
#include "common/buffer.h"
#include "kv/key_value_db.h"
#include "kv/merge_op/xor_merge_op.h"

using namespace TOPNSPC;

namespace {

const char *META_PREFIX = "B";
const char *BITMAP_PREFIX = "b";

bufferlist u64_bl(uint64_t v) {
    bufferlist bl;
    bl.append(reinterpret_cast<const char *>(&v), sizeof(v));
    return bl;
}

uint64_t read_u64(bufferlist &bl) {
    if (bl.length() < sizeof(uint64_t)) return 0;
    uint64_t v;
    std::memcpy(&v, bl.c_str(), sizeof(uint64_t));
    return v;
}

class BitmapFMTest : public ::testing::Test {
protected:
    std::unique_ptr<KeyValueDB> db;
    std::unique_ptr<BitmapFreelistManager> fm;

    void SetUp() override {
        db = KeyValueDB::create("memdb", "");
        ASSERT_NE(db, nullptr);
        db->set_merge_operator(BITMAP_PREFIX,
                               std::make_shared<XorMergeOperator>());
        int r = db->create_and_open(std::cerr);
        ASSERT_EQ(r, 0);

        fm = std::make_unique<BitmapFreelistManager>(META_PREFIX,
                                                     BITMAP_PREFIX);
    }

    void TearDown() override {
        fm.reset();
        db->close();
    }

    void do_create(uint64_t size, uint64_t granularity) {
        auto t = db->get_transaction();
        int r = fm->create(size, granularity, t);
        ASSERT_EQ(r, 0);
        r = db->submit_transaction_sync(t);
        ASSERT_EQ(r, 0);
    }

    void do_init() {
        int r = fm->init(db.get(), false,
                         std::function<int(const std::string &,
                                           std::string *)>());
        ASSERT_EQ(r, 0);
    }
};

// =====================================================================
// Create / Init
// =====================================================================

TEST_F(BitmapFMTest, CreateAndInit) {
    do_create(1ULL << 30, 4096);  // 1GB, 4KB blocks
    do_init();

    EXPECT_EQ(fm->get_size(), 1ULL << 30);
    EXPECT_EQ(fm->get_alloc_size(), 4096);
    // blocks = 1GB / 4KB = 262144, aligned to blocks_per_key(128)
    EXPECT_EQ(fm->get_alloc_units(), 262144);
}

TEST_F(BitmapFMTest, CreateAndInitNonPowerOf2Size) {
    do_create((1ULL << 30) + 1234, 4096);
    do_init();

    // size_ should be rounded down to block alignment
    EXPECT_EQ(fm->get_size(), (1ULL << 30) & ~4095);
}

TEST_F(BitmapFMTest, InitFromDb) {
    do_create(1ULL << 30, 4096);

    // Re-create FM and init from DB (no create, just init)
    auto fm2 = std::make_unique<BitmapFreelistManager>(META_PREFIX,
                                                       BITMAP_PREFIX);
    int r = fm2->init(db.get(), false,
                      std::function<int(const std::string &,
                                        std::string *)>());
    ASSERT_EQ(r, 0);

    EXPECT_EQ(fm2->get_size(), 1ULL << 30);
    EXPECT_EQ(fm2->get_alloc_size(), 4096);
    EXPECT_EQ(fm2->get_alloc_units(), 262144);
}

// =====================================================================
// Meta persistence checks
// =====================================================================

TEST_F(BitmapFMTest, MetaWrittenToDb) {
    do_create(1ULL << 30, 4096);

    bufferlist out;
    int r = db->get(META_PREFIX, "bytes_per_block", &out);
    ASSERT_EQ(r, 0);
    EXPECT_EQ(read_u64(out), 4096);

    r = db->get(META_PREFIX, "blocks_per_key", &out);
    ASSERT_EQ(r, 0);
    EXPECT_EQ(read_u64(out), 128);

    r = db->get(META_PREFIX, "size", &out);
    ASSERT_EQ(r, 0);
    EXPECT_EQ(read_u64(out), 1ULL << 30);

    r = db->get(META_PREFIX, "blocks", &out);
    ASSERT_EQ(r, 0);
    EXPECT_EQ(read_u64(out), 262144);
}

// =====================================================================
// Allocate / Release
// =====================================================================

TEST_F(BitmapFMTest, AllocateSingleBlock) {
    do_create(1ULL << 20, 4096);  // 1MB, 256 blocks
    do_init();

    auto t = db->get_transaction();
    fm->allocate(0, 4096, t);  // allocate block 0
    ASSERT_EQ(db->submit_transaction_sync(t), 0);
}

TEST_F(BitmapFMTest, ReleaseSingleBlock) {
    do_create(1ULL << 20, 4096);
    do_init();

    auto t = db->get_transaction();
    fm->release(4096, 4096, t);  // release block 1 (double-free is OK with XOR)
    ASSERT_EQ(db->submit_transaction_sync(t), 0);
}

// =====================================================================
// Enumerate
// =====================================================================

TEST_F(BitmapFMTest, EnumerateAllocatedBlock0) {
    do_create(1ULL << 20, 4096);
    do_init();

    // block 0 is always allocated (by create, the first block is set)
    // so the first free should be at offset 4096
    fm->enumerate_reset();
    uint64_t offset, length;
    bool found = fm->enumerate_next(db.get(), &offset, &length);
    ASSERT_TRUE(found);
    EXPECT_EQ(offset, 4096);
    EXPECT_EQ(length, (1ULL << 20) - 4096);
}

TEST_F(BitmapFMTest, EnumerateAfterAllocate) {
    do_create(1ULL << 20, 4096);
    do_init();

    // allocate block 1 (offset 4096)
    auto t = db->get_transaction();
    fm->allocate(4096, 4096, t);
    ASSERT_EQ(db->submit_transaction_sync(t), 0);

    // Now the free region starts at block 2 (offset 8192)
    fm->enumerate_reset();
    uint64_t offset, length;
    bool found = fm->enumerate_next(db.get(), &offset, &length);
    ASSERT_TRUE(found);
    EXPECT_EQ(offset, 8192);
    EXPECT_EQ(length, (1ULL << 20) - 8192);
}

TEST_F(BitmapFMTest, EnumerateAfterRelease) {
    do_create(1ULL << 20, 4096);
    do_init();

    // allocate block 1, then release it back
    auto t = db->get_transaction();
    fm->allocate(4096, 4096, t);
    ASSERT_EQ(db->submit_transaction_sync(t), 0);

    t = db->get_transaction();
    fm->release(4096, 4096, t);
    ASSERT_EQ(db->submit_transaction_sync(t), 0);

    // Block 1 should be free again (starting from 4096)
    fm->enumerate_reset();
    uint64_t offset, length;
    bool found = fm->enumerate_next(db.get(), &offset, &length);
    ASSERT_TRUE(found);
    EXPECT_EQ(offset, 4096);
    EXPECT_EQ(length, (1ULL << 20) - 4096);
}

TEST_F(BitmapFMTest, EnumerateNoFreeBeforeSize) {
    do_create(4096 * 128, 4096);  // exactly 1 key (128 blocks)
    do_init();

    // allocate everything
    auto t = db->get_transaction();
    fm->allocate(4096, 4096 * 127, t);  // blocks 1..127
    ASSERT_EQ(db->submit_transaction_sync(t), 0);

    // block 0 is allocated by create, blocks 1..127 allocated above
    fm->enumerate_reset();
    uint64_t offset, length;
    bool found = fm->enumerate_next(db.get(), &offset, &length);
    EXPECT_FALSE(found);
}

// =====================================================================
// Cross-key-boundary operations
// =====================================================================

TEST_F(BitmapFMTest, AllocateCrossKeyBoundary) {
    // Each key has 128 blocks. Allocate spanning blocks 64..191
    // (crosses from key0 to key1)
    do_create(4096 * 256, 4096);  // 2 keys
    do_init();

    auto t = db->get_transaction();
    fm->allocate(4096 * 64, 4096 * 128, t);
    ASSERT_EQ(db->submit_transaction_sync(t), 0);

    // After this, key0 has blocks 64..127 allocated, key1 has blocks 0..63 allocated
    // Free regions: block0 + blocks 1..63 in key0, blocks 64..127 in key1
    fm->enumerate_reset();
    uint64_t offset, length;
    bool found = fm->enumerate_next(db.get(), &offset, &length);
    ASSERT_TRUE(found);
    EXPECT_EQ(offset, 4096);       // block 1 (block 0 is allocated)
    EXPECT_EQ(length, 4096 * 63);  // blocks 1..63
}

TEST_F(BitmapFMTest, ReleaseCrossKeyBoundary) {
    do_create(4096 * 256, 4096);
    do_init();

    // Allocate blocks 1..127 (within key 0), then release them
    auto t = db->get_transaction();
    fm->allocate(4096, 4096 * 127, t);
    ASSERT_EQ(db->submit_transaction_sync(t), 0);

    // Blocks 1..127 are now allocated. After create+allocate, allocated = {0, 1..127}
    t = db->get_transaction();
    fm->release(4096, 4096 * 127, t);  // release blocks 1..127
    ASSERT_EQ(db->submit_transaction_sync(t), 0);

    // Only block 0 is allocated, rest is free
    fm->enumerate_reset();
    uint64_t offset, length;
    bool found = fm->enumerate_next(db.get(), &offset, &length);
    ASSERT_TRUE(found);
    EXPECT_EQ(offset, 4096);
    EXPECT_EQ(length, 4096 * 255);
}

// =====================================================================
// Null manager mode
// =====================================================================

TEST_F(BitmapFMTest, NullManagerNoOp) {
    do_create(1ULL << 20, 4096);
    do_init();
    fm->set_null_manager();
    ASSERT_TRUE(fm->is_null_manager());

    auto t = db->get_transaction();
    fm->allocate(0, 4096, t);
    fm->release(4096, 4096, t);
    ASSERT_EQ(db->submit_transaction_sync(t), 0);

    // Block 0 should still be allocated (null_manager doesn't write KV)
    fm->enumerate_reset();
    uint64_t offset, length;
    bool found = fm->enumerate_next(db.get(), &offset, &length);
    ASSERT_TRUE(found);
    EXPECT_EQ(offset, 4096);
}

// =====================================================================
// get_meta
// =====================================================================

TEST_F(BitmapFMTest, GetMeta) {
    do_create(1ULL << 20, 4096);
    do_init();

    std::vector<std::pair<std::string, std::string>> meta;
    fm->get_meta(0, &meta);

    bool has_blocks = false, has_size = false;
    bool has_bpb = false, has_bpk = false;
    for (auto &[k, v] : meta) {
        if (k == "bfm_blocks") has_blocks = true;
        if (k == "bfm_size") has_size = true;
        if (k == "bfm_bytes_per_block") has_bpb = true;
        if (k == "bfm_blocks_per_key") has_bpk = true;
    }
    EXPECT_TRUE(has_blocks);
    EXPECT_TRUE(has_size);
    EXPECT_TRUE(has_bpb);
    EXPECT_TRUE(has_bpk);
}

// =====================================================================
// EOF protection: blocks past EOF are pre-allocated
// =====================================================================

TEST_F(BitmapFMTest, PastEofPreAllocated) {
    // size not aligned to blocks_per_key boundary
    do_create(4096 * 130, 4096);  // 130 blocks, rounds up to 128*2 = 256 blocks
    do_init();

    // blocks beyond 130*4096 should be allocated (not free)
    fm->enumerate_reset();
    uint64_t offset, length;
    while (fm->enumerate_next(db.get(), &offset, &length)) {
        ASSERT_LE(offset + length, 4096 * 130);
    }
}

}  // namespace
