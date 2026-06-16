#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <rocksdb/db.h>
#include <rocksdb/merge_operator.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/utilities/write_batch_with_index.h>
#include <rocksdb/write_batch.h>

namespace fs = std::filesystem;

static std::string tmpdir() {
    auto build_dir = fs::current_path();
    auto d = build_dir / "test_rocksdb_tmp";
    fs::create_directories(d);
    return d.string();
}

// ---------------------------------------------------------------------------
// Merge operator: element-wise int64 addition (same as Int64ArrayMergeOperator
// in the kv design)
// ---------------------------------------------------------------------------
class Int64ArrayMergeOp : public rocksdb::MergeOperator {
public:
    const char *Name() const override { return "int64_array"; }

    bool FullMergeV2(const MergeOperationInput &merge_in,
                     MergeOperationOutput *merge_out) const override {
        const rocksdb::Slice *existing = merge_in.existing_value;
        const auto &operands = merge_in.operand_list;
        if (!existing) {
            // No existing value — write the accumulation of all operands
            int64_t sum = 0;
            for (auto &op : operands) {
                auto *d = reinterpret_cast<const int64_t *>(op.data());
                size_t n = op.size() / sizeof(int64_t);
                for (size_t i = 0; i < n; i++)
                    sum += d[i];
            }
            merge_out->new_value.assign(reinterpret_cast<char *>(&sum),
                                        sizeof(sum));
            return true;
        }
        // Accumulate existing + all operands element-wise
        auto *edata = reinterpret_cast<const int64_t *>(existing->data());
        size_t elen = existing->size() / sizeof(int64_t);
        std::vector<int64_t> result(elen);
        for (size_t i = 0; i < elen; i++)
            result[i] = edata[i];
        for (auto &op : operands) {
            auto *d = reinterpret_cast<const int64_t *>(op.data());
            size_t n = op.size() / sizeof(int64_t);
            for (size_t i = 0; i < std::min(elen, n); i++)
                result[i] += d[i];
        }
        merge_out->new_value.assign(reinterpret_cast<char *>(result.data()),
                                    elen * sizeof(int64_t));
        return true;
    }
};

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class RocksDBTest : public ::testing::Test {
protected:
    std::string dbpath_;
    rocksdb::DB *db_ = nullptr;

    void SetUp() override {
        dbpath_ = tmpdir() + "/rocksdb_test_" + std::to_string(getpid()) + "_"
                  + std::to_string(rand());
        fs::remove_all(dbpath_);
    }

    void TearDown() override {
        delete db_;
        fs::remove_all(dbpath_);
    }

    void OpenDb() {
        rocksdb::Options opts;
        opts.create_if_missing = true;
        auto s = rocksdb::DB::Open(opts, dbpath_, &db_);
        ASSERT_TRUE(s.ok()) << s.ToString();
    }

    void OpenDbWithMerge() {
        rocksdb::Options opts;
        opts.create_if_missing = true;
        opts.merge_operator = std::make_shared<Int64ArrayMergeOp>();
        auto s = rocksdb::DB::Open(opts, dbpath_, &db_);
        ASSERT_TRUE(s.ok()) << s.ToString();
    }

    std::string pkey(const std::string &prefix, const std::string &key) {
        return prefix + '\0' + key;
    }
};

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------
TEST_F(RocksDBTest, OpenAndClose) {
    OpenDb();
    ASSERT_NE(db_, nullptr);
    // db_ deleted in TearDown
}

TEST_F(RocksDBTest, OpenExisting) {
    OpenDb();
    delete db_;
    db_ = nullptr;

    rocksdb::Options opts;
    opts.create_if_missing = false;
    auto s = rocksdb::DB::Open(opts, dbpath_, &db_);
    ASSERT_TRUE(s.ok()) << s.ToString();
}

// ---------------------------------------------------------------------------
// Point Read / Write
// ---------------------------------------------------------------------------
TEST_F(RocksDBTest, PutAndGet) {
    OpenDb();
    auto s = db_->Put(rocksdb::WriteOptions(), "key1", "hello rocksdb");
    ASSERT_TRUE(s.ok());

    std::string val;
    s = db_->Get(rocksdb::ReadOptions(), "key1", &val);
    ASSERT_TRUE(s.ok());
    EXPECT_EQ(val, "hello rocksdb");
}

TEST_F(RocksDBTest, GetNotFound) {
    OpenDb();
    std::string val;
    auto s = db_->Get(rocksdb::ReadOptions(), "nonexistent", &val);
    ASSERT_TRUE(s.IsNotFound());
}

TEST_F(RocksDBTest, Overwrite) {
    OpenDb();
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "k", "v1").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "k", "v2").ok());

    std::string val;
    ASSERT_TRUE(db_->Get(rocksdb::ReadOptions(), "k", &val).ok());
    EXPECT_EQ(val, "v2");
}

// ---------------------------------------------------------------------------
// Delete
// ---------------------------------------------------------------------------
TEST_F(RocksDBTest, Delete) {
    OpenDb();
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "k", "v").ok());
    ASSERT_TRUE(db_->Delete(rocksdb::WriteOptions(), "k").ok());

    std::string val;
    auto s = db_->Get(rocksdb::ReadOptions(), "k", &val);
    EXPECT_TRUE(s.IsNotFound());
}

TEST_F(RocksDBTest, DeleteNonExistent) {
    OpenDb();
    auto s = db_->Delete(rocksdb::WriteOptions(), "nonexistent");
    EXPECT_TRUE(s.ok());
}

TEST_F(RocksDBTest, SingleDelete) {
    OpenDb();
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "k", "v").ok());
    ASSERT_TRUE(
        db_->SingleDelete(rocksdb::WriteOptions(), "k").ok());

    std::string val;
    EXPECT_TRUE(
        db_->Get(rocksdb::ReadOptions(), "k", &val).IsNotFound());
}

// ---------------------------------------------------------------------------
// Range Delete (DeleteRange)
// ---------------------------------------------------------------------------
TEST_F(RocksDBTest, DeleteRange) {
    OpenDb();
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "a", "1").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "b", "2").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "c", "3").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "d", "4").ok());

    rocksdb::WriteBatch batch;
    batch.DeleteRange("b", "d");
    ASSERT_TRUE(db_->Write(rocksdb::WriteOptions(), &batch).ok());

    std::string val;
    EXPECT_TRUE(db_->Get(rocksdb::ReadOptions(), "a", &val).ok());
    EXPECT_TRUE(db_->Get(rocksdb::ReadOptions(), "b", &val).IsNotFound());
    EXPECT_TRUE(db_->Get(rocksdb::ReadOptions(), "c", &val).IsNotFound());
    EXPECT_TRUE(db_->Get(rocksdb::ReadOptions(), "d", &val).ok());
}

// ---------------------------------------------------------------------------
// Iterator
// ---------------------------------------------------------------------------
TEST_F(RocksDBTest, IteratorForward) {
    OpenDb();
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "a", "1").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "b", "2").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "c", "3").ok());

    auto it = std::unique_ptr<rocksdb::Iterator>(
        db_->NewIterator(rocksdb::ReadOptions()));
    it->SeekToFirst();
    std::vector<std::pair<std::string, std::string>> got;
    for (; it->Valid(); it->Next())
        got.emplace_back(it->key().ToString(), it->value().ToString());

    ASSERT_EQ(got.size(), 3);
    EXPECT_EQ(got[0].first, "a");
    EXPECT_EQ(got[0].second, "1");
    EXPECT_EQ(got[1].first, "b");
    EXPECT_EQ(got[1].second, "2");
    EXPECT_EQ(got[2].first, "c");
    EXPECT_EQ(got[2].second, "3");
}

TEST_F(RocksDBTest, IteratorBackward) {
    OpenDb();
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "a", "1").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "b", "2").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "c", "3").ok());

    auto it = std::unique_ptr<rocksdb::Iterator>(
        db_->NewIterator(rocksdb::ReadOptions()));
    it->SeekToLast();
    std::vector<std::pair<std::string, std::string>> got;
    for (; it->Valid(); it->Prev())
        got.emplace_back(it->key().ToString(), it->value().ToString());

    ASSERT_EQ(got.size(), 3);
    EXPECT_EQ(got[0].first, "c");
    EXPECT_EQ(got[1].first, "b");
    EXPECT_EQ(got[2].first, "a");
}

TEST_F(RocksDBTest, IteratorSeek) {
    OpenDb();
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "a", "1").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "b", "2").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "c", "3").ok());

    auto it = std::unique_ptr<rocksdb::Iterator>(
        db_->NewIterator(rocksdb::ReadOptions()));

    // lower_bound("b") — first key >= "b"
    it->Seek("b");
    ASSERT_TRUE(it->Valid());
    EXPECT_EQ(it->key().ToString(), "b");
    EXPECT_EQ(it->value().ToString(), "2");

    // upper_bound("b") — first key > "b"
    it->SeekForPrev("b");
    ASSERT_TRUE(it->Valid());
    EXPECT_EQ(it->key().ToString(), "b");

    // Past the end
    it->Seek("z");
    EXPECT_FALSE(it->Valid());
}

TEST_F(RocksDBTest, IteratorBounds) {
    OpenDb();
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "a", "1").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "b", "2").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "c", "3").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "d", "4").ok());

    rocksdb::ReadOptions ropts;
    rocksdb::Slice upper("d");
    ropts.iterate_upper_bound = &upper;

    auto it = std::unique_ptr<rocksdb::Iterator>(
        db_->NewIterator(ropts));
    std::vector<std::string> got;
    for (it->Seek("a"); it->Valid(); it->Next())
        got.push_back(it->key().ToString());

    ASSERT_EQ(got.size(), 3);
    EXPECT_EQ(got[0], "a");
    EXPECT_EQ(got[1], "b");
    EXPECT_EQ(got[2], "c");
}

// ---------------------------------------------------------------------------
// WriteBatch (Transaction)
// ---------------------------------------------------------------------------
TEST_F(RocksDBTest, WriteBatch) {
    OpenDb();
    rocksdb::WriteBatch batch;
    batch.Put("k1", "v1");
    batch.Put("k2", "v2");
    batch.Delete("k1");

    ASSERT_TRUE(db_->Write(rocksdb::WriteOptions(), &batch).ok());

    std::string val;
    EXPECT_TRUE(
        db_->Get(rocksdb::ReadOptions(), "k1", &val).IsNotFound());
    EXPECT_TRUE(db_->Get(rocksdb::ReadOptions(), "k2", &val).ok());
    EXPECT_EQ(val, "v2");
}

TEST_F(RocksDBTest, WriteBatchAtomic) {
    OpenDb();
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "k", "original").ok());

    rocksdb::WriteBatch batch;
    batch.Put("k", "updated");
    batch.Delete("nonexistent");  // no-op, fine

    ASSERT_TRUE(db_->Write(rocksdb::WriteOptions(), &batch).ok());

    std::string val;
    ASSERT_TRUE(db_->Get(rocksdb::ReadOptions(), "k", &val).ok());
    EXPECT_EQ(val, "updated");
}

// ---------------------------------------------------------------------------
// Merge Operator
// ---------------------------------------------------------------------------
TEST_F(RocksDBTest, MergeOperator) {
    OpenDbWithMerge();

    auto write_delta = [&](int64_t v) {
        std::string val(reinterpret_cast<char *>(&v), sizeof(v));
        ASSERT_TRUE(
            db_->Merge(rocksdb::WriteOptions(), "counter", val).ok());
    };

    write_delta(10);
    write_delta(20);
    write_delta(30);

    std::string result;
    ASSERT_TRUE(
        db_->Get(rocksdb::ReadOptions(), "counter", &result).ok());
    ASSERT_EQ(result.size(), sizeof(int64_t));
    int64_t sum = *reinterpret_cast<const int64_t *>(result.data());
    EXPECT_EQ(sum, 60);
}

TEST_F(RocksDBTest, MergeOperatorNonExistent) {
    OpenDbWithMerge();

    int64_t v = 42;
    std::string val(reinterpret_cast<char *>(&v), sizeof(v));
    ASSERT_TRUE(
        db_->Merge(rocksdb::WriteOptions(), "new_key", val).ok());

    std::string result;
    ASSERT_TRUE(
        db_->Get(rocksdb::ReadOptions(), "new_key", &result).ok());
    ASSERT_EQ(result.size(), sizeof(int64_t));
    int64_t got = *reinterpret_cast<const int64_t *>(result.data());
    EXPECT_EQ(got, 42);
}

// ---------------------------------------------------------------------------
// Compact
// ---------------------------------------------------------------------------
TEST_F(RocksDBTest, CompactRange) {
    OpenDb();
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "a", "1").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "b", "2").ok());

    auto s = db_->CompactRange(rocksdb::CompactRangeOptions(), nullptr,
                               nullptr);
    EXPECT_TRUE(s.ok());

    std::string val;
    ASSERT_TRUE(db_->Get(rocksdb::ReadOptions(), "a", &val).ok());
    EXPECT_EQ(val, "1");
}

TEST_F(RocksDBTest, CompactAfterDelete) {
    OpenDb();
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "k", "v").ok());
    ASSERT_TRUE(db_->Delete(rocksdb::WriteOptions(), "k").ok());

    auto s = db_->CompactRange(rocksdb::CompactRangeOptions(), nullptr,
                               nullptr);
    EXPECT_TRUE(s.ok());

    std::string val;
    EXPECT_TRUE(
        db_->Get(rocksdb::ReadOptions(), "k", &val).IsNotFound());
}

// ---------------------------------------------------------------------------
// Prefix encoding (prefix + '\0' + key)
// ---------------------------------------------------------------------------
TEST_F(RocksDBTest, PrefixIteration) {
    OpenDb();

    auto pk = [&](const std::string &pre, const std::string &k) {
        return pre + '\0' + k;
    };

    ASSERT_TRUE(
        db_->Put(rocksdb::WriteOptions(), pk("O", "obj1"), "onode1").ok());
    ASSERT_TRUE(
        db_->Put(rocksdb::WriteOptions(), pk("O", "obj2"), "onode2").ok());
    ASSERT_TRUE(
        db_->Put(rocksdb::WriteOptions(), pk("M", "k1"), "v1").ok());
    ASSERT_TRUE(
        db_->Put(rocksdb::WriteOptions(), pk("M", "k2"), "v2").ok());

    // Scan prefix "M" — keys in ["M\0", "M\1") (since "\0" < "\1")
    std::string prefix = "M";
    std::string start = prefix + '\0';
    std::string end = prefix;
    end[0]++;

    auto it = std::unique_ptr<rocksdb::Iterator>(
        db_->NewIterator(rocksdb::ReadOptions()));

    std::vector<std::string> keys;
    for (it->Seek(start); it->Valid() && it->key().compare(end) < 0;
         it->Next())
        keys.push_back(it->key().ToString());

    ASSERT_EQ(keys.size(), 2);
    EXPECT_EQ(keys[0], pk("M", "k1"));
    EXPECT_EQ(keys[1], pk("M", "k2"));
}

TEST_F(RocksDBTest, RangeDeleteByPrefix) {
    OpenDb();

    auto pk = [&](const std::string &pre, const std::string &k) {
        return pre + '\0' + k;
    };

    ASSERT_TRUE(
        db_->Put(rocksdb::WriteOptions(), pk("O", "keep"), "ok").ok());
    ASSERT_TRUE(
        db_->Put(rocksdb::WriteOptions(), pk("M", "del1"), "bye").ok());
    ASSERT_TRUE(
        db_->Put(rocksdb::WriteOptions(), pk("M", "del2"), "bye").ok());

    std::string prefix = "M";
    std::string start = prefix + '\0';
    std::string end = prefix;
    end[0]++;

    rocksdb::WriteBatch batch;
    batch.DeleteRange(start, end);
    ASSERT_TRUE(db_->Write(rocksdb::WriteOptions(), &batch).ok());

    std::string val;
    EXPECT_TRUE(
        db_->Get(rocksdb::ReadOptions(), pk("O", "keep"), &val).ok());
    EXPECT_TRUE(
        db_->Get(rocksdb::ReadOptions(), pk("M", "del1"), &val).IsNotFound());
    EXPECT_TRUE(
        db_->Get(rocksdb::ReadOptions(), pk("M", "del2"), &val).IsNotFound());
}

// ---------------------------------------------------------------------------
// MultiGet
// ---------------------------------------------------------------------------
TEST_F(RocksDBTest, MultiGet) {
    OpenDb();
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "a", "1").ok());
    ASSERT_TRUE(db_->Put(rocksdb::WriteOptions(), "b", "2").ok());

    std::vector<rocksdb::Slice> keys = {"a", "b", "c"};
    std::vector<std::string> vals(3);
    std::vector<rocksdb::Status> statuses =
        db_->MultiGet(rocksdb::ReadOptions(), keys, &vals);

    ASSERT_TRUE(statuses[0].ok());
    EXPECT_EQ(vals[0], "1");
    ASSERT_TRUE(statuses[1].ok());
    EXPECT_EQ(vals[1], "2");
    ASSERT_TRUE(statuses[2].IsNotFound());
}

// ---------------------------------------------------------------------------
// Reopen persistence
// ---------------------------------------------------------------------------
TEST_F(RocksDBTest, ReopenPersists) {
    {
        OpenDb();
        ASSERT_TRUE(
            db_->Put(rocksdb::WriteOptions(), "persist", "data").ok());
        delete db_;
        db_ = nullptr;
    }
    {
        rocksdb::Options opts;
        opts.create_if_missing = false;
        auto s = rocksdb::DB::Open(opts, dbpath_, &db_);
        ASSERT_TRUE(s.ok());

        std::string val;
        ASSERT_TRUE(
            db_->Get(rocksdb::ReadOptions(), "persist", &val).ok());
        EXPECT_EQ(val, "data");
    }
}
