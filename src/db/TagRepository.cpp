#include "db/TagRepository.h"

#include <utility>

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/Database.h"

namespace metrace::db {

std::vector<Tag> TagRepository::listAll(int limit, int offset)
{
    DbLock lock;
    SQLite::Database& db = Database::instance().handle();

    SQLite::Statement stmt(db,
                           "SELECT id, name FROM tags ORDER BY id ASC LIMIT :limit OFFSET :offset");
    stmt.bind(":limit", limit);
    stmt.bind(":offset", offset);

    std::vector<Tag> tags;
    while (stmt.executeStep()) {
        Tag tag;
        tag.id = stmt.getColumn(0).getInt64();
        tag.name = stmt.getColumn(1).getString();
        tags.push_back(std::move(tag));
    }
    return tags;
}

std::int64_t TagRepository::countAll()
{
    DbLock lock;
    SQLite::Database& db = Database::instance().handle();

    SQLite::Statement stmt(db, "SELECT COUNT(*) FROM tags");
    stmt.executeStep(); // COUNT(*) 一定返回一行
    return stmt.getColumn(0).getInt64();
}

Tag TagRepository::createOrGet(const std::string& name)
{
    DbLock lock;
    SQLite::Database& db = Database::instance().handle();

    SQLite::Transaction tx(db);

    {
        // INSERT OR IGNORE + 回查：无论标签是否已存在都能拿到 id，天然幂等
        SQLite::Statement insert(db, "INSERT OR IGNORE INTO tags(name) VALUES (:name)");
        insert.bind(":name", name);
        insert.exec();
    }

    Tag tag;
    {
        SQLite::Statement select(db, "SELECT id, name FROM tags WHERE name = :name");
        select.bind(":name", name);
        select.executeStep();
        tag.id = select.getColumn(0).getInt64();
        tag.name = select.getColumn(1).getString();
    }

    tx.commit();
    return tag;
}

} // namespace metrace::db
