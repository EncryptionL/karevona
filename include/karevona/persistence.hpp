// Persistence abstraction for *authoritative state*. Domain models do not
// know SQL: they serialize to JSON and are stored as versioned records.
// Events and telemetry are NOT stored through this interface.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "karevona/common.hpp"

namespace karevona {

struct Record {
    std::string collection;
    std::string key;
    nlohmann::json value;
    uint64_t version = 0;  // starts at 1, +1 per successful write
    Timestamp updated_at{};
};

// Optimistic-concurrency condition for writes.
struct WriteCondition {
    enum class Kind { Any, MustNotExist, ExpectVersion } kind = Kind::Any;
    uint64_t version = 0;
    static WriteCondition any() { return {}; }
    static WriteCondition must_not_exist() { return {Kind::MustNotExist, 0}; }
    static WriteCondition expect_version(uint64_t v) { return {Kind::ExpectVersion, v}; }
};

struct ListOptions {
    std::string key_prefix;
    size_t limit = 0;  // 0 = unlimited
};

class IStateStore {
public:
    virtual ~IStateStore() = default;
    // NotFound if absent.
    virtual Result<Record> get(const std::string& collection, const std::string& key) = 0;
    // AlreadyExists (MustNotExist) / Conflict (version mismatch) / NotFound (ExpectVersion on a missing key).
    virtual Result<Record> put(const std::string& collection, const std::string& key, const nlohmann::json& value,
                               WriteCondition condition = {}) = 0;
    virtual Status erase(const std::string& collection, const std::string& key, WriteCondition condition = {}) = 0;
    // Sorted by key.
    virtual Result<std::vector<Record>> list(const std::string& collection, const ListOptions& options = {}) = 0;
    virtual Status ping() = 0;
};

class InMemoryStateStore final : public IStateStore {
public:
    Result<Record> get(const std::string& collection, const std::string& key) override;
    Result<Record> put(const std::string& collection, const std::string& key, const nlohmann::json& value,
                       WriteCondition condition = {}) override;
    Status erase(const std::string& collection, const std::string& key, WriteCondition condition = {}) override;
    Result<std::vector<Record>> list(const std::string& collection, const ListOptions& options = {}) override;
    Status ping() override { return Status::ok(); }

private:
    std::mutex mutex_;
    std::map<std::pair<std::string, std::string>, Record> records_;
};

// Typed repository over any IStateStore. T needs ADL to_json/from_json and a
// key extractor.
template <class T>
class Repository {
public:
    using KeyFn = std::string (*)(const T&);
    Repository(IStateStore& store, std::string collection, KeyFn key_fn)
        : store_(store), collection_(std::move(collection)), key_fn_(key_fn) {}

    Result<uint64_t> save(const T& value, WriteCondition condition = {}) {
        auto r = store_.put(collection_, key_fn_(value), nlohmann::json(value), condition);
        if (!r) return r.status();
        return r.value().version;
    }
    Result<T> load(const std::string& key) {
        auto r = store_.get(collection_, key);
        if (!r) return r.status();
        try {
            return r.value().value.template get<T>();
        } catch (const std::exception& ex) {
            return Status(ErrorCode::Internal, collection_ + "/" + key + ": corrupt record: " + ex.what());
        }
    }
    Result<std::vector<T>> load_all() {
        auto r = store_.list(collection_);
        if (!r) return r.status();
        std::vector<T> out;
        try {
            for (const auto& rec : r.value()) out.push_back(rec.value.template get<T>());
        } catch (const std::exception& ex) {
            return Status(ErrorCode::Internal, collection_ + ": corrupt record: " + ex.what());
        }
        return out;
    }
    Status remove(const std::string& key, WriteCondition condition = {}) {
        return store_.erase(collection_, key, condition);
    }

private:
    IStateStore& store_;
    std::string collection_;
    KeyFn key_fn_;
};

// Backend selection is configuration, not a compile-time dependency of
// callers. Returns Unimplemented for backends that were not compiled in.
// config keys: persistence.backend = "memory" | "postgres";
//              persistence.postgres.conninfo = libpq connection string.
class Config;
Result<std::unique_ptr<IStateStore>> make_state_store(const Config& config);

}  // namespace karevona
