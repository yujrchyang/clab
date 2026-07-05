#include "btier/config.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace TOPNSPC::btier {

// ── Minimal JSON value representation ──────────────────────────
// A simple recursive-descent JSON parser sufficient for BtierConfig.
// Avoids external dependency (nlohmann/json is not bundled).
namespace {

class JsonValue {
public:
    enum class Type { Null,
                      Bool,
                      Number,
                      String,
                      Object,
                      Array };

    Type type = Type::Null;
    bool bval = false;
    double nval = 0.0;
    std::string sval;
    std::map<std::string, JsonValue> oval;
    std::vector<JsonValue> aval;

    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_object() const { return type == Type::Object; }

    double as_number(double def = 0.0) const {
        return is_number() ? nval : def;
    }
    const std::string &as_string(const std::string &def = "") const {
        return is_string() ? sval : def;
    }
    const JsonValue *get(const std::string &key) const {
        auto it = oval.find(key);
        return it == oval.end() ? nullptr : &it->second;
    }
};

class JsonParser {
public:
    JsonParser(const std::string &text) : text_(text), pos_(0) {}

    bool parse(JsonValue &out) {
        skip_ws();
        if (eof()) return false;
        return parse_value(out);
    }

private:
    const std::string &text_;
    size_t pos_;

    bool eof() const { return pos_ >= text_.size(); }
    char peek() const { return eof() ? '\0' : text_[pos_]; }
    char next() { return eof() ? '\0' : text_[pos_++]; }

    void skip_ws() {
        while (!eof()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                pos_++;
            else
                break;
        }
    }

    bool parse_value(JsonValue &out) {
        skip_ws();
        if (eof()) return false;
        char c = peek();
        if (c == '{') return parse_object(out);
        if (c == '[') return parse_array(out);
        if (c == '"') return parse_string(out);
        if (c == 't' || c == 'f') return parse_bool(out);
        if (c == 'n') return parse_null(out);
        return parse_number(out);
    }

    bool parse_object(JsonValue &out) {
        out.type = JsonValue::Type::Object;
        next();  // consume '{'
        skip_ws();
        if (peek() == '}') {
            next();
            return true;
        }

        while (true) {
            skip_ws();
            if (peek() != '"') return false;
            JsonValue key;
            if (!parse_string(key)) return false;

            skip_ws();
            if (next() != ':') return false;

            JsonValue val;
            if (!parse_value(val)) return false;
            out.oval[key.sval] = std::move(val);

            skip_ws();
            char c = next();
            if (c == ',') continue;
            if (c == '}') return true;
            return false;
        }
    }

    bool parse_array(JsonValue &out) {
        out.type = JsonValue::Type::Array;
        next();  // consume '['
        skip_ws();
        if (peek() == ']') {
            next();
            return true;
        }

        while (true) {
            JsonValue val;
            if (!parse_value(val)) return false;
            out.aval.push_back(std::move(val));

            skip_ws();
            char c = next();
            if (c == ',') continue;
            if (c == ']') return true;
            return false;
        }
    }

    bool parse_string(JsonValue &out) {
        out.type = JsonValue::Type::String;
        next();  // consume opening '"'
        std::string s;
        while (!eof()) {
            char c = next();
            if (c == '"') {
                out.sval = std::move(s);
                return true;
            }
            if (c == '\\') {
                if (eof()) return false;
                char esc = next();
                switch (esc) {
                case '"':
                    s += '"';
                    break;
                case '\\':
                    s += '\\';
                    break;
                case '/':
                    s += '/';
                    break;
                case 'n':
                    s += '\n';
                    break;
                case 't':
                    s += '\t';
                    break;
                case 'r':
                    s += '\r';
                    break;
                case 'b':
                    s += '\b';
                    break;
                case 'f':
                    s += '\f';
                    break;
                default:
                    s += esc;
                    break;
                }
            } else {
                s += c;
            }
        }
        return false;
    }

    bool parse_bool(JsonValue &out) {
        out.type = JsonValue::Type::Bool;
        if (text_.compare(pos_, 4, "true") == 0) {
            out.bval = true;
            pos_ += 4;
            return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            out.bval = false;
            pos_ += 5;
            return true;
        }
        return false;
    }

    bool parse_null(JsonValue &out) {
        out.type = JsonValue::Type::Null;
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return true;
        }
        return false;
    }

    bool parse_number(JsonValue &out) {
        out.type = JsonValue::Type::Number;
        size_t start = pos_;
        if (peek() == '-') pos_++;
        while (!eof()) {
            char c = peek();
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                c == '+' || c == '-')
                pos_++;
            else
                break;
        }
        if (pos_ == start) return false;
        std::string num_str = text_.substr(start, pos_ - start);
        try {
            out.nval = std::stod(num_str);
        } catch (...) {
            return false;
        }
        return true;
    }
};

// ── JSON writer helpers ─────────────────────────────────────────
std::string esc_json(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out += c;
            break;
        }
    }
    out += '"';
    return out;
}

void write_kv(std::ostringstream &oss, const std::string &key,
              const std::string &val, bool last) {
    oss << "    " << esc_json(key) << ": " << esc_json(val);
    if (!last) oss << ",";
    oss << "\n";
}

void write_kv(std::ostringstream &oss, const std::string &key,
              double val, bool last) {
    oss << "    " << esc_json(key) << ": " << val;
    if (!last) oss << ",";
    oss << "\n";
}

void write_kv(std::ostringstream &oss, const std::string &key,
              uint64_t val, bool last) {
    oss << "    " << esc_json(key) << ": " << val;
    if (!last) oss << ",";
    oss << "\n";
}

void write_kv(std::ostringstream &oss, const std::string &key,
              uint32_t val, bool last) {
    oss << "    " << esc_json(key) << ": " << val;
    if (!last) oss << ",";
    oss << "\n";
}

void write_kv(std::ostringstream &oss, const std::string &key,
              float val, bool last) {
    oss << "    " << esc_json(key) << ": " << val;
    if (!last) oss << ",";
    oss << "\n";
}

}  // anonymous namespace

// ── BtierConfig::load / save ────────────────────────────────────

BtierConfig BtierConfig::load(const std::string &path) {
    BtierConfig cfg;
    std::ifstream ifs(path);
    if (!ifs.is_open()) return cfg;

    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string text = ss.str();

    JsonParser parser(text);
    JsonValue root;
    if (!parser.parse(root) || !root.is_object()) return cfg;

    if (auto *v = root.get("fast_dev_path"))
        cfg.fast_dev_path = v->as_string();
    if (auto *v = root.get("slow_dev_path"))
        cfg.slow_dev_path = v->as_string();
    if (auto *v = root.get("extent_size"))
        cfg.extent_size = (uint64_t)v->as_number(cfg.extent_size);
    if (auto *v = root.get("block_size"))
        cfg.block_size = (uint64_t)v->as_number(cfg.block_size);
    if (auto *v = root.get("large_value_threshold"))
        cfg.large_value_threshold =
            (uint64_t)v->as_number(cfg.large_value_threshold);
    if (auto *v = root.get("low_watermark"))
        cfg.low_watermark = v->as_number(cfg.low_watermark);
    if (auto *v = root.get("high_watermark"))
        cfg.high_watermark = v->as_number(cfg.high_watermark);
    if (auto *v = root.get("scan_interval_ms"))
        cfg.scan_interval_ms = (uint32_t)v->as_number(cfg.scan_interval_ms);
    if (auto *v = root.get("max_migrations_per_cycle"))
        cfg.max_migrations_per_cycle =
            (uint32_t)v->as_number(cfg.max_migrations_per_cycle);
    if (auto *v = root.get("max_compactions_per_cycle"))
        cfg.max_compactions_per_cycle =
            (uint32_t)v->as_number(cfg.max_compactions_per_cycle);
    if (auto *v = root.get("promote_threshold"))
        cfg.promote_threshold = (float)v->as_number(cfg.promote_threshold);
    if (auto *v = root.get("demote_threshold"))
        cfg.demote_threshold = (float)v->as_number(cfg.demote_threshold);
    if (auto *v = root.get("compaction_dead_ratio"))
        cfg.compaction_dead_ratio = v->as_number(cfg.compaction_dead_ratio);
    if (auto *v = root.get("compaction_usage_ratio"))
        cfg.compaction_usage_ratio = v->as_number(cfg.compaction_usage_ratio);
    if (auto *v = root.get("cool_interval_sec"))
        cfg.cool_interval_sec = (uint32_t)v->as_number(cfg.cool_interval_sec);
    if (auto *v = root.get("sequential_threshold"))
        cfg.sequential_threshold =
            (uint64_t)v->as_number(cfg.sequential_threshold);
    if (auto *v = root.get("journal_size"))
        cfg.journal_size = (uint64_t)v->as_number(cfg.journal_size);

    if (auto *ws = root.get("base_weights")) {
        if (ws->is_object()) {
            if (auto *v = ws->get("w_recency"))
                cfg.base_weights.w_recency =
                    (float)v->as_number(cfg.base_weights.w_recency);
            if (auto *v = ws->get("w_frequency"))
                cfg.base_weights.w_frequency =
                    (float)v->as_number(cfg.base_weights.w_frequency);
            if (auto *v = ws->get("w_randomness"))
                cfg.base_weights.w_randomness =
                    (float)v->as_number(cfg.base_weights.w_randomness);
            if (auto *v = ws->get("w_write_penalty"))
                cfg.base_weights.w_write_penalty =
                    (float)v->as_number(cfg.base_weights.w_write_penalty);
        }
    }

    return cfg;
}

int BtierConfig::save(const std::string &path) const {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return -errno;

    std::ostringstream oss;
    oss << "{\n";

    write_kv(oss, "fast_dev_path", fast_dev_path, false);
    write_kv(oss, "slow_dev_path", slow_dev_path, false);
    write_kv(oss, "extent_size", extent_size, false);
    write_kv(oss, "block_size", block_size, false);
    write_kv(oss, "large_value_threshold", large_value_threshold, false);

    // base_weights object
    oss << "    \"base_weights\": {\n";
    oss << "        \"w_recency\": " << base_weights.w_recency << ",\n";
    oss << "        \"w_frequency\": " << base_weights.w_frequency << ",\n";
    oss << "        \"w_randomness\": " << base_weights.w_randomness << ",\n";
    oss << "        \"w_write_penalty\": " << base_weights.w_write_penalty << "\n";
    oss << "    },\n";

    write_kv(oss, "low_watermark", low_watermark, false);
    write_kv(oss, "high_watermark", high_watermark, false);
    write_kv(oss, "scan_interval_ms", scan_interval_ms, false);
    write_kv(oss, "max_migrations_per_cycle", max_migrations_per_cycle, false);
    write_kv(oss, "max_compactions_per_cycle", max_compactions_per_cycle, false);
    write_kv(oss, "promote_threshold", promote_threshold, false);
    write_kv(oss, "demote_threshold", demote_threshold, false);
    write_kv(oss, "compaction_dead_ratio", compaction_dead_ratio, false);
    write_kv(oss, "compaction_usage_ratio", compaction_usage_ratio, false);
    write_kv(oss, "cool_interval_sec", cool_interval_sec, false);
    write_kv(oss, "sequential_threshold", sequential_threshold, false);
    write_kv(oss, "journal_size", journal_size, true);

    oss << "}\n";

    ofs << oss.str();
    ofs.flush();
    if (!ofs.good()) return -EIO;
    return 0;
}

}  // namespace TOPNSPC::btier
