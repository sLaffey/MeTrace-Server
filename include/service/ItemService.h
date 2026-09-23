#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "db/Models.h"

namespace metrace::service {

/**
 * 条目相关的业务逻辑层。
 *
 * 职责：校验业务规则、补默认值、编排仓储调用、写审计日志。
 * HTTP 层只负责把 JSON 转成 db::ItemInput（见 http/JsonUtil.h），业务规则全部收敛在这里。
 *
 * 约定：校验失败抛 ValidationError，记录不存在抛 NotFoundError，
 * 由 HTTP 层的异常处理器统一映射成 400 / 404。
 */
class ItemService {
public:
    /// 新增条目并返回完整记录。
    static db::Item create(const db::ItemInput& input);

    /// 查询单条；不存在返回 nullopt。
    static std::optional<db::Item> get(std::int64_t id);

    static std::vector<db::Item> list(const db::ItemQuery& query);

    /// 与 list() 相同过滤条件的总条数，用于分页响应里的 total。
    static std::int64_t count(const db::ItemQuery& query);

    /// 部分更新并返回更新后的记录；id 不存在抛 NotFoundError。
    static db::Item update(std::int64_t id, const db::ItemInput& input);

    /// 删除条目；id 不存在抛 NotFoundError。
    static void remove(std::int64_t id);

    /// 新增标签（同名幂等，返回已存在的那条）。
    static db::Tag createTag(const std::string& name);

    static std::vector<db::Tag> listTags(int limit, int offset);

    static std::int64_t countTags();
};

} // namespace metrace::service
