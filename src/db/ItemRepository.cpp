#include "db/ItemRepository.h"

#include <string>
#include <utility>

#include <SQLiteCpp/SQLiteCpp.h>

#include "db/Database.h"

namespace metrace::db {
namespace {

/// 所有查询共用的列清单；顺序必须与 readItem() 里的下标一致。
constexpr const char* kSelectColumns =
    "SELECT i.id, i.type, i.title, i.creator, i.year, i.cover_url, i.description, "
    "i.status, i.progress, i.score, i.review, i.created_at, i.updated_at FROM items i";

/// 过滤条件：参数绑定 NULL 时该条件自动失效（":type IS NULL" 恒为真）。
constexpr const char* kFilter =
    " WHERE (:type IS NULL OR i.type = :type)"
    "   AND (:status IS NULL OR i.status = :status)"
    "   AND (:tag IS NULL OR EXISTS ("
    "        SELECT 1 FROM item_tags it JOIN tags t ON t.id = it.tag_id"
    "        WHERE it.item_id = i.id AND t.name = :tag))";

/// 列允许为 NULL，直接 getString() 会有风险，统一走这个helper。
std::string textOrEmpty(const SQLite::Statement& stmt, int index)
{
    const SQLite::Column column = stmt.getColumn(index);
    return column.isNull() ? std::string() : column.getString();
}

/// 把当前行读成 Item（不含 tags）；列顺序与 kSelectColumns 一致。
Item readItem(const SQLite::Statement& stmt)
{
    Item item;
    item.id = stmt.getColumn(0).getInt64();
    item.type = parseItemType(stmt.getColumn(1).getString()).value_or(ItemType::Book);
    item.title = stmt.getColumn(2).getString();
    item.creator = textOrEmpty(stmt, 3);
    item.year = stmt.getColumn(4).isNull() ? 0 : stmt.getColumn(4).getInt();
    item.coverUrl = textOrEmpty(stmt, 5);
    item.description = textOrEmpty(stmt, 6);
    item.status = parseItemStatus(stmt.getColumn(7).getString()).value_or(ItemStatus::Wish);
    item.progress = stmt.getColumn(8).isNull() ? 0.0 : stmt.getColumn(8).getDouble();
    if (!stmt.getColumn(9).isNull()) {
        item.score = stmt.getColumn(9).getDouble();
    }
    item.review = textOrEmpty(stmt, 10);
    item.createdAt = textOrEmpty(stmt, 11);
    item.updatedAt = textOrEmpty(stmt, 12);
    return item;
}

/// 查询某个条目的全部标签名（按名称排序）。
std::vector<std::string> loadTags(SQLite::Database& db, std::int64_t itemId)
{
    SQLite::Statement stmt(db,
                           "SELECT t.name FROM item_tags it JOIN tags t ON t.id = it.tag_id"
                           " WHERE it.item_id = :id ORDER BY t.name ASC");
    stmt.bind(":id", itemId);

    std::vector<std::string> names;
    while (stmt.executeStep()) {
        names.push_back(stmt.getColumn(0).getString());
    }
    return names;
}

/// 按名称取标签 id，不存在则插入。必须在调用方的事务里执行。
std::int64_t upsertTag(SQLite::Database& db, const std::string& name)
{
    {
        SQLite::Statement insert(db, "INSERT OR IGNORE INTO tags(name) VALUES (:name)");
        insert.bind(":name", name);
        insert.exec();
    }

    SQLite::Statement select(db, "SELECT id FROM tags WHERE name = :name");
    select.bind(":name", name);
    select.executeStep();
    return select.getColumn(0).getInt64();
}

/// 把条目与一批标签关联起来（标签名不存在时自动创建）。
void linkTags(SQLite::Database& db, std::int64_t itemId, const std::vector<std::string>& names)
{
    for (const std::string& name : names) {
        const std::int64_t tagId = upsertTag(db, name);

        SQLite::Statement link(db,
                               "INSERT OR IGNORE INTO item_tags(item_id, tag_id)"
                               " VALUES (:item_id, :tag_id)");
        link.bind(":item_id", itemId);
        link.bind(":tag_id", tagId);
        link.exec();
    }
}

/// 绑定可空文本参数；未提供或为空串都写 NULL。
void bindNullableText(SQLite::Statement& stmt, const char* param, const Patch<std::string>& patch)
{
    if (patch.value && !patch.value->empty()) {
        stmt.bind(param, *patch.value);
    } else {
        stmt.bind(stmt.getIndex(param)); // 不传值 = 绑定 NULL
    }
}

/// 绑定可空浮点参数：未提供写 NULL（score 为 NULL 表示未评分）。
void bindNullableDouble(SQLite::Statement& stmt, const char* param, const Patch<double>& patch)
{
    if (patch.value) {
        stmt.bind(param, *patch.value);
    } else {
        stmt.bind(stmt.getIndex(param));
    }
}

/// 绑定过滤条件；未提供的条件绑定 NULL，从而被 SQL 里的 "IS NULL" 分支跳过。
void bindFilter(SQLite::Statement& stmt, const ItemQuery& query)
{
    if (query.type) {
        stmt.bind(":type", toString(*query.type));
    } else {
        stmt.bind(stmt.getIndex(":type"));
    }

    if (query.status) {
        stmt.bind(":status", toString(*query.status));
    } else {
        stmt.bind(stmt.getIndex(":status"));
    }

    if (query.tag && !query.tag->empty()) {
        stmt.bind(":tag", *query.tag);
    } else {
        stmt.bind(stmt.getIndex(":tag"));
    }
}

} // namespace

std::int64_t ItemRepository::create(const ItemInput& input)
{
    DbLock lock;
    SQLite::Database& db = Database::instance().handle();

    SQLite::Transaction tx(db);

    SQLite::Statement stmt(
        db,
        "INSERT INTO items(type, title, creator, year, cover_url, description, status, progress, score, review)"
        " VALUES (:type, :title, :creator, :year, :cover_url, :description, :status, :progress, :score, :review)");
    stmt.bind(":type", toString(input.type.value.value_or(ItemType::Book)));
    stmt.bind(":title", input.title.value.value_or(std::string()));
    bindNullableText(stmt, ":creator", input.creator);
    stmt.bind(":year", input.year.value.value_or(0));
    bindNullableText(stmt, ":cover_url", input.coverUrl);
    bindNullableText(stmt, ":description", input.description);
    stmt.bind(":status", toString(input.status.value.value_or(ItemStatus::Wish)));
    stmt.bind(":progress", input.progress.value.value_or(0.0));
    bindNullableDouble(stmt, ":score", input.score);
    bindNullableText(stmt, ":review", input.review);
    stmt.exec();

    const std::int64_t id = db.getLastInsertRowid();

    if (input.tags.provided && input.tags.value) {
        linkTags(db, id, *input.tags.value);
    }

    tx.commit(); // 条目与标签必须一起成功，否则一起回滚
    return id;
}

std::optional<Item> ItemRepository::findById(std::int64_t id)
{
    DbLock lock;
    SQLite::Database& db = Database::instance().handle();

    SQLite::Statement stmt(db, std::string(kSelectColumns) + " WHERE i.id = :id");
    stmt.bind(":id", id);

    if (!stmt.executeStep()) {
        return std::nullopt;
    }

    Item item = readItem(stmt);
    item.tags = loadTags(db, item.id);
    return item;
}

std::vector<Item> ItemRepository::list(const ItemQuery& query)
{
    DbLock lock;
    SQLite::Database& db = Database::instance().handle();

    std::string sql = kSelectColumns;
    sql += kFilter;
    // ORDER BY 的列名来自 Models.h 的白名单映射，用户输入只会影响"用哪个字段"，
    // 不会被拼进 SQL；再追加 i.id 保证分页时顺序稳定。
    sql += " ORDER BY ";
    sql += toColumn(query.sort);
    sql += query.desc ? " DESC" : " ASC";
    sql += ", i.id";
    sql += query.desc ? " DESC" : " ASC";
    sql += " LIMIT :limit OFFSET :offset";

    SQLite::Statement stmt(db, sql);
    bindFilter(stmt, query);
    stmt.bind(":limit", query.limit);
    stmt.bind(":offset", query.offset);

    std::vector<Item> items;
    while (stmt.executeStep()) {
        Item item = readItem(stmt);
        item.tags = loadTags(db, item.id);
        items.push_back(std::move(item));
    }
    return items;
}

std::int64_t ItemRepository::count(const ItemQuery& query)
{
    DbLock lock;
    SQLite::Database& db = Database::instance().handle();

    SQLite::Statement stmt(db, std::string("SELECT COUNT(*) FROM items i") + kFilter);
    bindFilter(stmt, query);

    stmt.executeStep(); // COUNT(*) 一定返回一行
    return stmt.getColumn(0).getInt64();
}

bool ItemRepository::update(std::int64_t id, const ItemInput& input)
{
    DbLock lock;
    SQLite::Database& db = Database::instance().handle();

    SQLite::Transaction tx(db);

    // 动态拼 SET 子句：列名与占位符都来自下面的硬编码，用户输入只影响"哪些字段被更新"。
    std::string sql = "UPDATE items SET updated_at = CURRENT_TIMESTAMP"; // SQLite 无 ON UPDATE，必须显式写
    if (input.type.provided) {
        sql += ", type = :type";
    }
    if (input.title.provided) {
        sql += ", title = :title";
    }
    if (input.creator.provided) {
        sql += ", creator = :creator";
    }
    if (input.year.provided) {
        sql += ", year = :year";
    }
    if (input.coverUrl.provided) {
        sql += ", cover_url = :cover_url";
    }
    if (input.description.provided) {
        sql += ", description = :description";
    }
    if (input.status.provided) {
        sql += ", status = :status";
    }
    if (input.progress.provided) {
        sql += ", progress = :progress";
    }
    if (input.score.provided) {
        sql += ", score = :score";
    }
    if (input.review.provided) {
        sql += ", review = :review";
    }
    sql += " WHERE id = :id";

    SQLite::Statement stmt(db, sql);
    if (input.type.provided) {
        stmt.bind(":type", toString(input.type.value.value_or(ItemType::Book)));
    }
    if (input.title.provided) {
        stmt.bind(":title", input.title.value.value_or(std::string()));
    }
    if (input.creator.provided) {
        bindNullableText(stmt, ":creator", input.creator);
    }
    if (input.year.provided) {
        stmt.bind(":year", input.year.value.value_or(0));
    }
    if (input.coverUrl.provided) {
        bindNullableText(stmt, ":cover_url", input.coverUrl);
    }
    if (input.description.provided) {
        bindNullableText(stmt, ":description", input.description);
    }
    if (input.status.provided) {
        stmt.bind(":status", toString(input.status.value.value_or(ItemStatus::Wish)));
    }
    if (input.progress.provided) {
        stmt.bind(":progress", input.progress.value.value_or(0.0));
    }
    if (input.score.provided) {
        bindNullableDouble(stmt, ":score", input.score);
    }
    if (input.review.provided) {
        bindNullableText(stmt, ":review", input.review);
    }
    stmt.bind(":id", id);

    // 受影响行数要在执行其它语句之前取，getChanges() 是连接级的、会被后续语句覆盖。
    const bool changed = stmt.exec() > 0;

    if (input.tags.provided) {
        SQLite::Statement clear(db, "DELETE FROM item_tags WHERE item_id = :id");
        clear.bind(":id", id);
        clear.exec();

        if (input.tags.value) {
            linkTags(db, id, *input.tags.value);
        }
    }

    tx.commit();
    return changed;
}

bool ItemRepository::remove(std::int64_t id)
{
    DbLock lock;
    SQLite::Database& db = Database::instance().handle();

    SQLite::Statement stmt(db, "DELETE FROM items WHERE id = :id");
    stmt.bind(":id", id);
    return stmt.exec() > 0; // item_tags 由外键 ON DELETE CASCADE 自动清理
}

} // namespace metrace::db
