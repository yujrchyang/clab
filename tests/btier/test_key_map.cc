#include <gtest/gtest.h>

#include <string>

#include "btier/btier_types.h"
#include "btier/key_map.h"

using namespace TOPNSPC::btier;

TEST(KeyMapTest, PutAndLookup) {
    KeyMap km;
    KeyLocation loc{1, 0, 4096};

    km.put("key1", loc, 0);

    KeyLocation result;
    EXPECT_TRUE(km.lookup("key1", &result));
    EXPECT_EQ(result.extent_id, 1u);
    EXPECT_EQ(result.offset, 0u);
    EXPECT_EQ(result.length, 4096u);
}

TEST(KeyMapTest, LookupNonExistent) {
    KeyMap km;
    KeyLocation loc;
    EXPECT_FALSE(km.lookup("nokey", &loc));
}

TEST(KeyMapTest, Erase) {
    KeyMap km;
    KeyLocation loc{1, 0, 4096};
    km.put("key1", loc, 0);

    km.erase("key1");

    KeyLocation result;
    EXPECT_FALSE(km.lookup("key1", &result));
    EXPECT_EQ(km.size(), 0u);
}

TEST(KeyMapTest, OverwriteUpdatesLocation) {
    KeyMap km;
    KeyLocation loc1{1, 0, 4096};
    KeyLocation loc2{2, 100, 2048};

    km.put("key1", loc1, 0);
    km.put("key1", loc2, 0);

    KeyLocation result;
    EXPECT_TRUE(km.lookup("key1", &result));
    EXPECT_EQ(result.extent_id, 2u);
    EXPECT_EQ(result.offset, 100u);
    EXPECT_EQ(result.length, 2048u);
}

TEST(KeyMapTest, ReverseIndex) {
    KeyMap km;
    KeyLocation loc1{1, 0, 4096};
    KeyLocation loc2{1, 4096, 4096};
    KeyLocation loc3{2, 0, 4096};

    km.put("key1", loc1, 0);
    km.put("key2", loc2, 0);
    km.put("key3", loc3, 0);

    auto keys_ext1 = km.keys_in_extent(1);
    EXPECT_EQ(keys_ext1.size(), 2u);
    EXPECT_EQ(keys_ext1.count("key1"), 1u);
    EXPECT_EQ(keys_ext1.count("key2"), 1u);

    auto keys_ext2 = km.keys_in_extent(2);
    EXPECT_EQ(keys_ext2.size(), 1u);
    EXPECT_EQ(keys_ext2.count("key3"), 1u);

    EXPECT_EQ(km.keys_in_extent_count(1), 2u);
    EXPECT_EQ(km.keys_in_extent_count(2), 1u);
    EXPECT_EQ(km.keys_in_extent_count(999), 0u);
}

TEST(KeyMapTest, ReverseIndexAfterErase) {
    KeyMap km;
    KeyLocation loc{1, 0, 4096};
    km.put("key1", loc, 0);
    km.put("key2", loc, 0);

    EXPECT_EQ(km.keys_in_extent_count(1), 2u);

    km.erase("key1");
    EXPECT_EQ(km.keys_in_extent_count(1), 1u);

    km.erase("key2");
    EXPECT_EQ(km.keys_in_extent_count(1), 0u);
}

TEST(KeyMapTest, ReverseIndexAfterOverwrite) {
    KeyMap km;
    KeyLocation loc1{1, 0, 4096};
    KeyLocation loc2{2, 0, 4096};

    km.put("key1", loc1, 0);
    EXPECT_EQ(km.keys_in_extent_count(1), 1u);

    km.put("key1", loc2, 0);
    EXPECT_EQ(km.keys_in_extent_count(1), 0u);  // old extent cleaned
    EXPECT_EQ(km.keys_in_extent_count(2), 1u);  // new extent
}

TEST(KeyMapTest, BatchUpdate) {
    KeyMap km;
    KeyLocation loc1{1, 0, 4096};
    km.put("key1", loc1, 0);
    km.put("key2", loc1, 0);

    // Batch update: move keys to new extent
    std::vector<std::pair<std::string, KeyLocation>> updates;
    updates.push_back({"key1", KeyLocation{2, 0, 4096}});
    updates.push_back({"key2", KeyLocation{2, 4096, 4096}});
    updates.push_back({"key3", KeyLocation{2, 8192, 4096}});  // new key

    km.batch_update(updates);

    KeyLocation result;
    EXPECT_TRUE(km.lookup("key1", &result));
    EXPECT_EQ(result.extent_id, 2u);
    EXPECT_EQ(result.offset, 0u);

    EXPECT_TRUE(km.lookup("key2", &result));
    EXPECT_EQ(result.extent_id, 2u);
    EXPECT_EQ(result.offset, 4096u);

    EXPECT_TRUE(km.lookup("key3", &result));
    EXPECT_EQ(result.extent_id, 2u);
    EXPECT_EQ(result.offset, 8192u);

    // Old extent should have no keys
    EXPECT_EQ(km.keys_in_extent_count(1), 0u);
    EXPECT_EQ(km.keys_in_extent_count(2), 3u);
}

TEST(KeyMapTest, StrideTrackingSequential) {
    KeyMap km;
    KeyLocation loc{1, 0, 4096};

    // Sequential LBAs (delta <= 64KB)
    // First put: delta=0 (lba=0, prev_lba=0) → increments to 1
    // Each subsequent put increments
    km.put("key1", loc, 0);
    km.put("key1", loc, 4096);   // delta = 4096
    km.put("key1", loc, 8192);   // delta = 4096
    km.put("key1", loc, 12288);  // delta = 4096

    EXPECT_EQ(km.get_consecutive_sequential("key1"), 4u);
}

TEST(KeyMapTest, StrideTrackingRandom) {
    KeyMap km;
    KeyLocation loc{1, 0, 4096};

    // Random LBAs (delta > 64KB)
    km.put("key1", loc, 0);
    km.put("key1", loc, 200000);  // delta = 200000 > 64KB

    EXPECT_EQ(km.get_consecutive_sequential("key1"), 0u);
}

TEST(KeyMapTest, StrideTrackingMixed) {
    KeyMap km;
    KeyLocation loc{1, 0, 4096};

    // Sequential then random
    km.put("key1", loc, 0);
    km.put("key1", loc, 4096);    // sequential
    km.put("key1", loc, 8192);    // sequential
    km.put("key1", loc, 500000);  // random — resets to 0

    EXPECT_EQ(km.get_consecutive_sequential("key1"), 0u);
}

TEST(KeyMapTest, StrideTrackingMaxAward) {
    KeyMap km;
    KeyLocation loc{1, 0, 4096};

    // Many sequential accesses — should cap at 63
    for (int i = 0; i < 100; i++) {
        km.put("key1", loc, i * 4096);
    }

    EXPECT_EQ(km.get_consecutive_sequential("key1"), 63u);
}

TEST(KeyMapTest, Size) {
    KeyMap km;
    EXPECT_EQ(km.size(), 0u);

    KeyLocation loc{1, 0, 4096};
    km.put("key1", loc, 0);
    km.put("key2", loc, 0);

    EXPECT_EQ(km.size(), 2u);

    km.erase("key1");
    EXPECT_EQ(km.size(), 1u);
}
