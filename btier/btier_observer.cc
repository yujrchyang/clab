#include "btier/btier_observer.h"

#include <chrono>
#include <iomanip>

#include <spdlog/spdlog.h>
#include "btier/btier_types.h"

namespace TOPNSPC::btier {

namespace {

const char *tier_str(Tier t) {
    return (t == Tier::FAST) ? "FAST" : "SLOW";
}

}  // anonymous namespace

BtierObserver::BtierObserver(const std::string &trace_path) {
    if (!trace_path.empty()) {
        trace_file_.open(trace_path, std::ios::app);
    }
}

BtierObserver::~BtierObserver() {
    if (trace_file_.is_open()) {
        trace_file_.close();
    }
}

void BtierObserver::log_migration_start(uint64_t extent_id, Tier from, Tier to) {
    if (!enabled_) return;
    spdlog::info("migration start: extent={} from={} to={}",
                 extent_id, tier_str(from), tier_str(to));
}

void BtierObserver::log_migration_result(uint64_t extent_id, Tier from,
                                         Tier to, const std::string &result,
                                         uint64_t duration_ms) {
    if (!enabled_) return;
    spdlog::info("migration done: extent={} {}→{} result={} duration={}ms",
                 extent_id, tier_str(from), tier_str(to),
                 result, duration_ms);

    if (trace_file_.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::seconds>(
                      now.time_since_epoch())
                      .count();
        trace_file_ << "[" << ts << "] " << extent_id << " "
                    << tier_str(from) << " " << tier_str(to) << " "
                    << result << " " << duration_ms << "ms\n";
        trace_file_.flush();
    }
}

void BtierObserver::log_compaction_result(uint64_t extent_id,
                                          const std::string &result,
                                          uint64_t duration_ms) {
    if (!enabled_) return;
    spdlog::info("compaction done: extent={} result={} duration={}ms",
                 extent_id, result, duration_ms);
}

void BtierObserver::dump_extent(uint64_t extent_id, const DiskLocation &loc,
                                uint32_t used_bytes, uint32_t live_bytes,
                                uint64_t raw_metrics) {
    if (!enabled_) return;
    spdlog::debug(
        "extent {}: tier={} offset={} len={} used={} live={} "
        "access={} write={} random={} gen={}",
        extent_id, tier_str(loc.tier), loc.offset, loc.length,
        used_bytes, live_bytes,
        ExtentMetrics::access_count(raw_metrics),
        ExtentMetrics::write_count(raw_metrics),
        ExtentMetrics::randomness(raw_metrics),
        ExtentMetrics::generation(raw_metrics));
}

}  // namespace TOPNSPC::btier
