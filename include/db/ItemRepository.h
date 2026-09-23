#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "db/Models.h"

namespace metrace::db {

/**
 * items 表 + item_tags 关联表 + tags 的 upsert —— 与条目相关的 SQL 全部集中在这里。
 *
 * 约定（AGENT.md §5.4）：
 *   - 每个方法第一行 `DbLock lock;`，自带事务，方法本身即原子操作；
 *   - 只用预编译语句 + 命名参数绑定，不拼接用户输入；
 *   - 出错抛 SQLite::Exception，由 HTTP 层统一转成 500。
 *
 * 注意：一次操作涉及多张表（items / item_tags / tags）时，必须在**同一个**
 * SQLite::Transaction 里完成，所以标签的 upsert 由本类自己实现，
 * 不调用 TagRepository，避免嵌套开事务。
 */
class ItemRepository {
public:
    /// 新增条目（含标签），返回自增主键。
    static std::int64_t create(const ItemInput& input);

    /// 按主键查询，不存在返回 nullopt。
    static std::optional<Item> findById(std::int64_t id);

    /// 条件分页查询，按 query.sort 排序。
    static std::vector<Item> list(const ItemQuery& query);

    /// 与 list() 相同的过滤条件，返回总条数（用于分页的 total）。
    static std::int64_t count(const ItemQuery& query);

    /// 部分更新；id 不存在返回 false。input.tags 提供时整体替换标签。
    static bool update(std::int64_t id, const ItemInput& input);

    /// 删除条目；id 不存在返回 false。item_tags 由外键级联删除。
    static bool remove(std::int64_t id);
};

} // namespace metrace::db
