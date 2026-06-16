#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "common/buffer.h"
#include "kv/key_value_db.h"

using namespace kv;
using TOPNSPC::bufferlist;

static std::string tmpdir() {
    auto path = std::filesystem::temp_directory_path() / "rocksdb_demo_XXXXXX";
    auto s = path.string();
    if (!mkdtemp(s.data())) {
        std::cerr << "mkdtemp failed\n";
        std::abort();
    }
    return s;
}

int main() {
    std::string dbpath = tmpdir();

    // 1. Create and open
    auto db = KeyValueDB::create("rocksdb", dbpath);
    assert(db);
    int r = db->create_and_open(std::cerr);
    assert(r == 0);

    // 2. Write — all mutations go through a transaction
    auto t = db->get_transaction();
    t->set("P", "foo", bufferlist());           // key "foo" under prefix "P"
    t->set("P", "bar", bufferlist());
    t->rmkey("P", "baz");                       // delete a key
    r = db->submit_transaction_sync(t);
    assert(r == 0);

    // 3. Read
    bufferlist val;
    r = db->get("P", "foo", &val);
    std::cout << "get(foo): " << (r == 0 ? "found" : "not found") << "\n";

    // 4. Iterator — walk all keys
    {
        auto it = db->get_wholespace_iterator();
        assert(it);
        std::cout << "all keys:\n";
        for (it->seek_to_first(); it->valid(); it->next()) {
            auto [p, k] = it->raw_key();
            std::cout << "  prefix=" << p << " key=" << k << "\n";
        }
    }

    // 5. Cleanup
    db->close();
    std::filesystem::remove_all(dbpath);
    return 0;
}
