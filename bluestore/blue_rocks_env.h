#pragma once

#include <memory>
#include <string>

#include "common/common_fwd.h"
#include "rocksdb/env.h"
#include "rocksdb/status.h"

namespace TOPNSPC {

class BlueFS;

// A RocksDB Env implementation layered on top of BlueFS.
// Files with absolute paths (starting with '/') are forwarded to
// the default POSIX Env; relative paths are served from BlueFS.
class BlueRocksEnv : public rocksdb::EnvWrapper {
public:
    explicit BlueRocksEnv(BlueFS *fs);

    rocksdb::Status NewSequentialFile(
        const std::string &fname,
        std::unique_ptr<rocksdb::SequentialFile> *result,
        const rocksdb::EnvOptions &options) override;

    rocksdb::Status NewRandomAccessFile(
        const std::string &fname,
        std::unique_ptr<rocksdb::RandomAccessFile> *result,
        const rocksdb::EnvOptions &options) override;

    rocksdb::Status NewWritableFile(
        const std::string &fname,
        std::unique_ptr<rocksdb::WritableFile> *result,
        const rocksdb::EnvOptions &options) override;

    rocksdb::Status ReuseWritableFile(
        const std::string &fname,
        const std::string &old_fname,
        std::unique_ptr<rocksdb::WritableFile> *result,
        const rocksdb::EnvOptions &options) override;

    rocksdb::Status NewDirectory(
        const std::string &name,
        std::unique_ptr<rocksdb::Directory> *result) override;

    rocksdb::Status FileExists(const std::string &fname) override;

    rocksdb::Status GetChildren(
        const std::string &dir,
        std::vector<std::string> *result) override;

    rocksdb::Status DeleteFile(const std::string &fname) override;

    rocksdb::Status CreateDir(const std::string &dirname) override;

    rocksdb::Status CreateDirIfMissing(const std::string &dirname) override;

    rocksdb::Status DeleteDir(const std::string &dirname) override;

    rocksdb::Status GetFileSize(
        const std::string &fname, uint64_t *file_size) override;

    rocksdb::Status GetFileModificationTime(
        const std::string &fname, uint64_t *file_mtime) override;

    rocksdb::Status RenameFile(
        const std::string &src, const std::string &target) override;

    rocksdb::Status LinkFile(
        const std::string &src, const std::string &target) override;

    rocksdb::Status AreFilesSame(
        const std::string &first,
        const std::string &second,
        bool *res) override;

    rocksdb::Status LockFile(
        const std::string &fname,
        rocksdb::FileLock **lock) override;

    rocksdb::Status UnlockFile(rocksdb::FileLock *lock) override;

    rocksdb::Status GetTestDirectory(std::string *path) override;

    rocksdb::Status NewLogger(
        const std::string &fname,
        std::shared_ptr<rocksdb::Logger> *result) override;

    rocksdb::Status GetAbsolutePath(
        const std::string &db_path,
        std::string *output_path) override;

private:
    BlueFS *fs_;
};

}  // namespace TOPNSPC
