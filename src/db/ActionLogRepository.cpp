#include "db/ActionLogRepository.h"

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/Database.h"

namespace metrace::db {

void ActionLogRepository::append(const std::string& action, const std::string& payloadJson)
{
    DbLock lock;
    SQLite::Database& db = Database::instance().handle();

    SQLite::Statement stmt(db, "INSERT INTO action_log(action, payload) VALUES (:action, :payload)");
    stmt.bind(":action", action);
    stmt.bind(":payload", payloadJson);
    stmt.exec();
}

} // namespace metrace::db
