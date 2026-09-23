#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <SQLiteCpp/SQLiteCpp.h>

namespace metrace::db {

/**
 * SQLite 连接管理器（进程内单例）。
 *
 * 职责：
 *   1. 打开 / 创建数据库文件，设置 PRAGMA；
 *   2. 建表与版本迁移（PRAGMA user_version）；
 *   3. 提供全局互斥量，保证多线程访问安全。
 *
 * 线程安全说明：
 *   SQLite::Database / SQLite::Statement 都不是线程安全的，而 httplib 默认用线程池并发
 *   处理请求。因此**任何**数据库访问都必须先持有 DbLock，Repository 的每个方法第一行
 *   都应该是 `DbLock lock;`。其它模块不允许直接访问底层连接。
 */
class Database {
public:
    static Database& instance();

    /// 打开（或创建）数据库并完成 PRAGMA 与建表迁移。幂等；失败时抛 SQLite::Exception。
    void open(const std::string& path, int busyTimeoutMs = 5000);

    bool isOpen() const noexcept { return handle_ != nullptr; }

    /// 取底层连接，要求已经调用过 open()。
    SQLite::Database& handle();

    /// 暴露互斥量给 RAII 锁使用；业务代码不要直接调用。
    std::recursive_mutex& mutex() noexcept { return mutex_; }

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

private:
    Database() = default;
    ~Database() = default;

    void applyPragmas(SQLite::Database& db);
    void migrate(SQLite::Database& db);

    std::unique_ptr<SQLite::Database> handle_;
    // 用递归锁，避免 handle() 与外部 DbLock 叠加时自锁死。
    std::recursive_mutex mutex_;
};

/// 作用域锁：Repository 的每个方法开头写一行 `DbLock lock;` 即可。
class DbLock {
public:
    DbLock() : lock_(Database::instance().mutex()) {}

private:
    std::lock_guard<std::recursive_mutex> lock_;
};

} // namespace metrace::db
