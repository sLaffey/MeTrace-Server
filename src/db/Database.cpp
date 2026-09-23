#include "db/Database.h"

#include <stdexcept>

namespace metrace::db {

Database& Database::instance()
{
    static Database inst; // C++11 起，局部 static 初始化是线程安全的
    return inst;
}

void Database::open(const std::string& path, int busyTimeoutMs)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (handle_) {
        return; // 已经打开，幂等返回
    }

    // OPEN_FULLMUTEX：让 sqlite3 内部也做串行化，作为 DbLock 之外的第二道保险。
    handle_ = std::make_unique<SQLite::Database>(path,
                                                 SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE |
                                                     SQLite::OPEN_FULLMUTEX,
                                                 busyTimeoutMs);

    applyPragmas(*handle_);
    migrate(*handle_);
}

SQLite::Database& Database::handle()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!handle_) {
        throw std::runtime_error(
            "database is not open; call metrace::db::Database::instance().open(path) at startup");
    }
    return *handle_;
}

void Database::applyPragmas(SQLite::Database& db)
{
    db.exec("PRAGMA journal_mode = WAL;");   // 读写并发更好，崩溃恢复更可靠
    db.exec("PRAGMA foreign_keys = ON;");    // SQLite 默认关闭，不打开则 ON DELETE CASCADE 失效
    db.exec("PRAGMA synchronous = NORMAL;"); // WAL 下的常用折中
    db.exec("PRAGMA temp_store = MEMORY;");
}

void Database::migrate(SQLite::Database& db)
{
    // 用 PRAGMA user_version 记录 schema 版本，实现最简单的增量迁移。
    // 以后新增表 / 加字段时，继续往下追加 if (version < 2) { ... } 即可。
    const int version = db.execAndGet("PRAGMA user_version;").getInt();

    if (version < 1) {
        SQLite::Transaction tx(db); // 迁移途中出错会自动回滚

        db.exec(R"sql(
            CREATE TABLE IF NOT EXISTS items (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                type        TEXT NOT NULL CHECK(type IN ('book','movie','music')),
                title       TEXT NOT NULL,
                creator     TEXT,
                year        INTEGER,
                cover_url   TEXT,
                description TEXT,
                status      TEXT DEFAULT 'wish' CHECK(status IN ('wish','doing','done','dropped')),
                progress    REAL DEFAULT 0 CHECK(progress >= 0 AND progress <= 1),
                score       REAL CHECK(score IS NULL OR (score >= 0 AND score <= 10)),
                review      TEXT,
                created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
                updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP
            );
        )sql");

        db.exec(R"sql(
            CREATE TABLE IF NOT EXISTS tags (
                id   INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT UNIQUE NOT NULL
            );
        )sql");

        db.exec(R"sql(
            CREATE TABLE IF NOT EXISTS item_tags (
                item_id INTEGER NOT NULL,
                tag_id  INTEGER NOT NULL,
                PRIMARY KEY (item_id, tag_id),
                FOREIGN KEY (item_id) REFERENCES items(id) ON DELETE CASCADE,
                FOREIGN KEY (tag_id)  REFERENCES tags(id)  ON DELETE CASCADE
            );
        )sql");

        // 审计日志：只记录历史，不参与撤销/重做（撤销由内存中的 UndoStack 负责）
        db.exec(R"sql(
            CREATE TABLE IF NOT EXISTS action_log (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                action     TEXT NOT NULL,
                payload    TEXT,
                created_at DATETIME DEFAULT CURRENT_TIMESTAMP
            );
        )sql");

        db.exec("CREATE INDEX IF NOT EXISTS idx_items_type    ON items(type);");
        db.exec("CREATE INDEX IF NOT EXISTS idx_items_status  ON items(status);");
        db.exec("CREATE INDEX IF NOT EXISTS idx_items_score   ON items(score);");
        db.exec("CREATE INDEX IF NOT EXISTS idx_items_created ON items(created_at);");
        db.exec("CREATE INDEX IF NOT EXISTS idx_item_tags_tag ON item_tags(tag_id);");

        db.exec("PRAGMA user_version = 1;");
        tx.commit();
    }
}

} // namespace metrace::db
