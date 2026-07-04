#ifndef MIGRATION_HPP
#define MIGRATION_HPP

#include <memory>
#include <vector>
#include <algorithm>
#include <ranges>
#include <iostream>
#include <mongocxx/client.hpp>

class Migration
{
public:
    virtual ~Migration() = default;
    [[nodiscard]] virtual int version() const = 0;
    virtual void up(mongocxx::client& client, mongocxx::client_session& session) = 0;
    virtual void down(mongocxx::client& client, mongocxx::client_session& session) = 0;
};

class MigrationRegistry
{
public:
    using Factory = std::unique_ptr<Migration>(*)();

    static MigrationRegistry& instance()
    {
        static MigrationRegistry r;
        return r;
    }

    void add(const Factory& f)
    {
        factories_.push_back(f);
    }

    [[nodiscard]] std::vector<std::unique_ptr<Migration>> create_all() const
    {
        std::vector<std::unique_ptr<Migration>> v;
        v.reserve(factories_.size());
        for (const auto& f : factories_)
        {
            v.push_back(f());
        }
        return v;
    }

private:
    std::vector<Factory> factories_;
};


namespace Migrator
{
    inline int migrate(mongocxx::client& client, mongocxx::client_session& session, const int currentVersion, const int targetVersion)
    {
        const auto all = MigrationRegistry::instance().create_all();

        std::vector<Migration*> pending;
        for (auto& m : all)
        {
            const auto version = m->version();
            if (version > currentVersion && version <= targetVersion)
            {
                pending.push_back(m.get());
            }
        }

        std::sort(pending.begin(), pending.end(),
                  [](auto a, auto b) { return a->version() < b->version(); });

        for (auto* m : pending)
        {
            std::cout << "Running migration version " << m->version() << std::endl;
            m->up(client, session);
        }

        return all.empty() ? 0 :
            pending.empty() ? targetVersion :
            pending.back()->version();
    }

    inline int rollback(mongocxx::client& client, mongocxx::client_session& session, const int currentVersion, const int targetVersion)
    {
        const auto all = MigrationRegistry::instance().create_all();

        std::vector<Migration*> pending;
        for (auto& m : all)
        {
            const auto version = m->version();
            if (version <= currentVersion && version > targetVersion)
            {
                pending.push_back(m.get());
            }
        }

        std::sort(pending.begin(), pending.end(),
                  [](auto a, auto b) { return a->version() > b->version(); });

        for (auto* m : pending)
        {
            std::cout << "Running migration version " << m->version() << std::endl;
            m->down(client, session);
        }

        return pending.empty() ? currentVersion : pending.back()->version() - 1;
    }

    inline int lastVersion()
    {
        const auto all = MigrationRegistry::instance().create_all();
        int maxVersion = 0;
        for (const auto& m : all)
        {
            if (m->version() > maxVersion)
            {
                maxVersion = m->version();
            }
        }
        return maxVersion;
    }

    inline int getCurrentVersion(const mongocxx::database& database)
    {
        const auto doc = database["version"].find_one({});
        if (doc)
        {
            return (*doc)["version"].get_int32().value;
        }
        return 0;
    }

    inline void setCurrentVersion(mongocxx::client_session& session, const mongocxx::database& database, int version)
    {
        const auto update = bsoncxx::builder::basic::make_document(
            bsoncxx::builder::basic::kvp("$set",
                                         bsoncxx::builder::basic::make_document(
                                             bsoncxx::builder::basic::kvp("version", version)
                                         )
            )
        );
        database["version"].update_one(session, {}, update.view(), mongocxx::options::update().upsert(true));
    }
}

#define DN_CONCAT_INNER(a,b) a##b
#define DN_CONCAT(a,b) DN_CONCAT_INNER(a,b)


#define DN_CAT2(a,b) a##b
#define DN_CAT(a,b)  DN_CAT2(a,b)

#define REGISTER_MIGRATION_IMPL(T, N) \
namespace { \
static std::unique_ptr<Migration> DN_CAT(make_, T)() { \
return std::make_unique<T>(); \
} \
struct DN_CAT(Registrar_, N) { \
DN_CAT(Registrar_, N)() { \
MigrationRegistry::instance().add(&DN_CAT(make_, T)); \
} \
}; \
static DN_CAT(Registrar_, N) DN_CAT(_registrar_, N); \
}

#define REGISTER_MIGRATION(T) REGISTER_MIGRATION_IMPL(T, __COUNTER__)

#define DECLARE_MIGRATION(T, V) \
class T final : public Migration { \
public: \
int version() const override { return (V); } \
void up(mongocxx::client& client, mongocxx::client_session& session) override; \
void down(mongocxx::client& client, mongocxx::client_session& session) override; \
}; \
REGISTER_MIGRATION(T)


#endif //MIGRATION_HPP
