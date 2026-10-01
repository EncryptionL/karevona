#include "karevona/persistence.hpp"

#include "karevona/config.hpp"

namespace karevona {

namespace {
bool version_ok(const WriteCondition& c, bool exists, uint64_t current, Status& why) {
    switch (c.kind) {
        case WriteCondition::Kind::Any:
            return true;
        case WriteCondition::Kind::MustNotExist:
            if (exists) {
                why = Status(ErrorCode::AlreadyExists, "record already exists");
                return false;
            }
            return true;
        case WriteCondition::Kind::ExpectVersion:
            if (!exists) {
                why = Status(ErrorCode::NotFound, "record not found");
                return false;
            }
            if (current != c.version) {
                why = Status(ErrorCode::Conflict, "version mismatch: expected " + std::to_string(c.version) +
                                                      ", found " + std::to_string(current));
                return false;
            }
            return true;
    }
    return true;
}
}  // namespace

Result<Record> InMemoryStateStore::get(const std::string& collection, const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = records_.find({collection, key});
    if (it == records_.end()) return Status(ErrorCode::NotFound, collection + "/" + key + " not found");
    return it->second;
}

Result<Record> InMemoryStateStore::put(const std::string& collection, const std::string& key,
                                       const nlohmann::json& value, WriteCondition condition) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = records_.find({collection, key});
    const bool exists = it != records_.end();
    Status why;
    if (!version_ok(condition, exists, exists ? it->second.version : 0, why)) return why;
    Record rec;
    rec.collection = collection;
    rec.key = key;
    rec.value = value;
    rec.version = exists ? it->second.version + 1 : 1;
    rec.updated_at = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    records_[{collection, key}] = rec;
    return rec;
}

Status InMemoryStateStore::erase(const std::string& collection, const std::string& key, WriteCondition condition) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = records_.find({collection, key});
    const bool exists = it != records_.end();
    if (!exists) return Status(ErrorCode::NotFound, collection + "/" + key + " not found");
    Status why;
    if (!version_ok(condition, exists, it->second.version, why)) return why;
    records_.erase(it);
    return Status::ok();
}

Result<std::vector<Record>> InMemoryStateStore::list(const std::string& collection, const ListOptions& options) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Record> out;
    for (auto it = records_.lower_bound({collection, options.key_prefix}); it != records_.end(); ++it) {
        if (it->first.first != collection) break;
        if (it->first.second.compare(0, options.key_prefix.size(), options.key_prefix) != 0) break;
        out.push_back(it->second);
        if (options.limit && out.size() >= options.limit) break;
    }
    return out;
}

#ifdef KAREVONA_WITH_POSTGRES
Result<std::unique_ptr<IStateStore>> make_postgres_state_store(const std::string& conninfo);
#endif

Result<std::unique_ptr<IStateStore>> make_state_store(const Config& config) {
    const std::string backend = config.get_string("persistence.backend", "memory");
    if (backend == "memory") return std::unique_ptr<IStateStore>(new InMemoryStateStore());
    if (backend == "postgres") {
#ifdef KAREVONA_WITH_POSTGRES
        return make_postgres_state_store(config.get_string("persistence.postgres.conninfo", ""));
#else
        return Status(ErrorCode::Unimplemented, "this build has no PostgreSQL support (KAREVONA_WITH_POSTGRES=OFF)");
#endif
    }
    return Status(ErrorCode::InvalidArgument, "unknown persistence backend: " + backend);
}

}  // namespace karevona
