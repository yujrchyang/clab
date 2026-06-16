#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "kv/key_value_db.h"

namespace kv {

class MemDB : public KeyValueDB {
public:
    MemDB();

    int init(const std::string &options_str) override;
    int open(std::ostream &out) override;
    int create_and_open(std::ostream &out) override;
    void close() override;

    Transaction get_transaction() override;
    int submit_transaction(Transaction t) override;

    int get(const std::string &prefix,
            const std::set<std::string> &keys,
            std::map<std::string, TOPNSPC::bufferlist> *out) override;

    WholeSpaceIterator get_wholespace_iterator(
        IteratorOpts opts) override;

    void compact() override;

    uint64_t get_estimated_size(
        std::map<std::string, uint64_t> &extra) override;

    friend class MDBWholeSpaceIteratorImpl;

private:
    class MDBTransactionImpl;
    class MDBWholeSpaceIteratorImpl;

    std::string encode_key(const std::string &prefix,
                           const std::string &key) const;
    static std::pair<std::string, std::string> decode_key(
        const std::string &full_key);

    void _set_key(const std::string &full_key,
                  const TOPNSPC::bufferlist &bl);
    void _rmkey(const std::string &full_key);
    void _rmkeys_by_prefix(const std::string &prefix);
    void _rm_range_keys(const std::string &prefix,
                        const std::string &start,
                        const std::string &end);
    int _merge(const std::string &prefix,
                const std::string &full_key,
                const TOPNSPC::bufferlist &bl);

    mutable std::mutex m_lock_;
    std::map<std::string, std::string> db_;
    uint64_t seqno_ = 0;
};

}  // namespace kv
