// PostgreSQL implementation of IStateStore (libpq). Compiled only when
// KAREVONA_WITH_POSTGRES is ON. The DDL lives in sql/migrations/ and is
// embedded at build time so there is a single source of truth.
#include <libpq-fe.h>

#include <memory>
#include <mutex>

#include "karevona/persistence.hpp"
#include "postgres_schema.hpp"

namespace karevona {

namespace {

struct PgResultDeleter {
    void operator()(PGresult* r) const { PQclear(r); }
};
using PgResult = std::unique_ptr<PGresult, PgResultDeleter>;

Timestamp from_epoch_ms(const char* text) {
    return Timestamp(std::chrono::milliseconds(std::stoll(text)));
}

class PostgresStateStore final : public IStateStore {
public:
    static Result<std::unique_ptr<IStateStore>> connect(const std::string& conninfo) {
        PGconn* conn = PQconnectdb(conninfo.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            std::string err = PQerrorMessage(conn);
            PQfinish(conn);
            return Status(ErrorCode::Unavailable, "postgres connection failed: " + err);
        }
        std::unique_ptr<PostgresStateStore> store(new PostgresStateStore(conn));
        auto st = store->migrate();
        if (!st) return st;
        return std::unique_ptr<IStateStore>(std::move(store));
    }

    ~PostgresStateStore() override { PQfinish(conn_); }

    Result<Record> get(const std::string& collection, const std::string& key) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const char* params[] = {collection.c_str(), key.c_str()};
        PgResult r(PQexecParams(conn_, kSelect, 2, nullptr, params, nullptr, nullptr, 0));
        if (PQresultStatus(r.get()) != PGRES_TUPLES_OK) return error("get", r.get());
        if (PQntuples(r.get()) == 0) return Status(ErrorCode::NotFound, collection + "/" + key + " not found");
        return to_record(collection, key, r.get(), 0, 0);
    }

    Result<Record> put(const std::string& collection, const std::string& key, const nlohmann::json& value,
                       WriteCondition condition) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string body = value.dump();
        const std::string expected = std::to_string(condition.version);
        PgResult r;
        switch (condition.kind) {
            case WriteCondition::Kind::Any: {
                const char* params[] = {collection.c_str(), key.c_str(), body.c_str()};
                r.reset(PQexecParams(conn_, kUpsert, 3, nullptr, params, nullptr, nullptr, 0));
                break;
            }
            case WriteCondition::Kind::MustNotExist: {
                const char* params[] = {collection.c_str(), key.c_str(), body.c_str()};
                r.reset(PQexecParams(conn_, kInsertNew, 3, nullptr, params, nullptr, nullptr, 0));
                break;
            }
            case WriteCondition::Kind::ExpectVersion: {
                const char* params[] = {collection.c_str(), key.c_str(), body.c_str(), expected.c_str()};
                r.reset(PQexecParams(conn_, kUpdateVersioned, 4, nullptr, params, nullptr, nullptr, 0));
                break;
            }
        }
        if (PQresultStatus(r.get()) != PGRES_TUPLES_OK) return error("put", r.get());
        if (PQntuples(r.get()) == 0) {
            if (condition.kind == WriteCondition::Kind::MustNotExist) {
                return Status(ErrorCode::AlreadyExists, collection + "/" + key + " already exists");
            }
            return missing_or_conflict(collection, key, condition.version);
        }
        Record rec;
        rec.collection = collection;
        rec.key = key;
        rec.value = value;
        rec.version = std::stoull(PQgetvalue(r.get(), 0, 0));
        rec.updated_at = from_epoch_ms(PQgetvalue(r.get(), 0, 1));
        return rec;
    }

    Status erase(const std::string& collection, const std::string& key, WriteCondition condition) override {
        std::lock_guard<std::mutex> lock(mutex_);
        PgResult r;
        const std::string expected = std::to_string(condition.version);
        if (condition.kind == WriteCondition::Kind::ExpectVersion) {
            const char* params[] = {collection.c_str(), key.c_str(), expected.c_str()};
            r.reset(PQexecParams(conn_, kDeleteVersioned, 3, nullptr, params, nullptr, nullptr, 0));
        } else {
            const char* params[] = {collection.c_str(), key.c_str()};
            r.reset(PQexecParams(conn_, kDelete, 2, nullptr, params, nullptr, nullptr, 0));
        }
        if (PQresultStatus(r.get()) != PGRES_COMMAND_OK) return error("erase", r.get());
        if (std::string(PQcmdTuples(r.get())) == "0") {
            return missing_or_conflict(collection, key, condition.version).status();
        }
        return Status::ok();
    }

    Result<std::vector<Record>> list(const std::string& collection, const ListOptions& options) override {
        std::lock_guard<std::mutex> lock(mutex_);
        // Escape LIKE metacharacters in the prefix so it is matched literally.
        std::string like;
        for (char c : options.key_prefix) {
            if (c == '\\' || c == '%' || c == '_') like += '\\';
            like += c;
        }
        like += '%';
        const std::string limit = options.limit ? std::to_string(options.limit) : "";
        const char* params[] = {collection.c_str(), like.c_str(), options.limit ? limit.c_str() : nullptr};
        PgResult r(PQexecParams(conn_, kList, 3, nullptr, params, nullptr, nullptr, 0));
        if (PQresultStatus(r.get()) != PGRES_TUPLES_OK) return error("list", r.get());
        std::vector<Record> out;
        for (int i = 0; i < PQntuples(r.get()); ++i) {
            out.push_back(to_record(collection, PQgetvalue(r.get(), i, 0), r.get(), i, 1).value());
        }
        return out;
    }

    Status ping() override {
        std::lock_guard<std::mutex> lock(mutex_);
        PgResult r(PQexec(conn_, "SELECT 1"));
        return PQresultStatus(r.get()) == PGRES_TUPLES_OK ? Status::ok()
                                                          : Status(ErrorCode::Unavailable, PQerrorMessage(conn_));
    }

private:
    explicit PostgresStateStore(PGconn* conn) : conn_(conn) {}

    static constexpr const char* kSelect =
        "SELECT value::text, version, (extract(epoch FROM updated_at) * 1000)::bigint "
        "FROM karevona_state WHERE collection = $1 AND key = $2";
    static constexpr const char* kUpsert =
        "INSERT INTO karevona_state (collection, key, value, version, updated_at) "
        "VALUES ($1, $2, $3::jsonb, 1, now()) "
        "ON CONFLICT (collection, key) DO UPDATE SET value = EXCLUDED.value, "
        "version = karevona_state.version + 1, updated_at = now() "
        "RETURNING version, (extract(epoch FROM updated_at) * 1000)::bigint";
    static constexpr const char* kInsertNew =
        "INSERT INTO karevona_state (collection, key, value, version, updated_at) "
        "VALUES ($1, $2, $3::jsonb, 1, now()) ON CONFLICT DO NOTHING "
        "RETURNING version, (extract(epoch FROM updated_at) * 1000)::bigint";
    static constexpr const char* kUpdateVersioned =
        "UPDATE karevona_state SET value = $3::jsonb, version = version + 1, updated_at = now() "
        "WHERE collection = $1 AND key = $2 AND version = $4::bigint "
        "RETURNING version, (extract(epoch FROM updated_at) * 1000)::bigint";
    static constexpr const char* kDelete = "DELETE FROM karevona_state WHERE collection = $1 AND key = $2";
    static constexpr const char* kDeleteVersioned =
        "DELETE FROM karevona_state WHERE collection = $1 AND key = $2 AND version = $3::bigint";
    static constexpr const char* kList =
        "SELECT key, value::text, version, (extract(epoch FROM updated_at) * 1000)::bigint "
        "FROM karevona_state WHERE collection = $1 AND key LIKE $2 "
        "ORDER BY key COLLATE \"C\" LIMIT $3::bigint";

    // Columns: [key,] value, version, updated_ms starting at `first`.
    // For get() first=0; for list() first=1 and the key is passed separately.
    static Result<Record> to_record(const std::string& collection, const std::string& key, PGresult* r, int row,
                                    int key_cols) {
        Record rec;
        rec.collection = collection;
        rec.key = key;
        rec.value = nlohmann::json::parse(PQgetvalue(r, row, key_cols), nullptr, false);
        rec.version = std::stoull(PQgetvalue(r, row, key_cols + 1));
        rec.updated_at = from_epoch_ms(PQgetvalue(r, row, key_cols + 2));
        return rec;
    }

    Result<Record> missing_or_conflict(const std::string& collection, const std::string& key, uint64_t expected) {
        const char* params[] = {collection.c_str(), key.c_str()};
        PgResult r(PQexecParams(conn_, kSelect, 2, nullptr, params, nullptr, nullptr, 0));
        if (PQresultStatus(r.get()) != PGRES_TUPLES_OK) return error("lookup", r.get());
        if (PQntuples(r.get()) == 0) return Status(ErrorCode::NotFound, collection + "/" + key + " not found");
        return Status(ErrorCode::Conflict, "version mismatch: expected " + std::to_string(expected) + ", found " +
                                               PQgetvalue(r.get(), 0, 1));
    }

    Status error(const char* op, PGresult* r) {
        return Status(ErrorCode::Internal, std::string("postgres ") + op + " failed: " + PQresultErrorMessage(r));
    }

    Status migrate() {
        std::lock_guard<std::mutex> lock(mutex_);
        // Concurrent first-time connections race inside CREATE TABLE IF NOT EXISTS; serialize them. A multi-statement
        // simple query runs as one implicit transaction, so the xact-scoped lock lasts until the DDL commits.
        const std::string sql = std::string("SELECT pg_advisory_xact_lock(727274);\n") + kSchemaMigration0001;
        PgResult r(PQexec(conn_, sql.c_str()));
        if (PQresultStatus(r.get()) != PGRES_COMMAND_OK) return error("migrate", r.get());
        return Status::ok();
    }

    std::mutex mutex_;
    PGconn* conn_;
};

}  // namespace

Result<std::unique_ptr<IStateStore>> make_postgres_state_store(const std::string& conninfo) {
    return PostgresStateStore::connect(conninfo);
}

}  // namespace karevona
