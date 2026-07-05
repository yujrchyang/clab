#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "btier/btier_types.h"
#include "btier/config.h"
#include "common/common_fwd.h"

namespace TOPNSPC::btier {

struct MigrationStats;

// ── BtierObserver ──────────────────────────────────────────────
// Observability helpers: logging, tracing, stats export.
// Uses spdlog for structured logging and a per-run trace file for
// migration events.
class BtierObserver {
public:
    explicit BtierObserver(const std::string &trace_path = "");
    ~BtierObserver();

    // Migration event logging
    void log_migration_start(uint64_t extent_id, Tier from, Tier to);
    void log_migration_result(uint64_t extent_id, Tier from, Tier to,
                              const std::string &result, uint64_t duration_ms);
    void log_compaction_result(uint64_t extent_id,
                               const std::string &result,
                               uint64_t duration_ms);

    // Extent dump for debugging
    void dump_extent(uint64_t extent_id, const DiskLocation &loc,
                     uint32_t used_bytes, uint32_t live_bytes,
                     uint64_t raw_metrics);

    // Enable/disable logging
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

private:
    bool enabled_ = true;
    std::ofstream trace_file_;
};

}  // namespace TOPNSPC::btier
