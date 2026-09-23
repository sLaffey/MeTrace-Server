#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "db/Models.h"

namespace metrace::db {

/// tags 表的仓储，服务于 /api/tags 两个接口。条目的标签写入由 ItemRepository 统一处理。
class TagRepository {
public:
    /// 按创建顺序分页查询。
    static std::vector<Tag> listAll(int limit, int offset);

    static std::int64_t countAll();

    /// 新增标签；若同名标签已存在则直接返回已有的那条（幂等）。
    static Tag createOrGet(const std::string& name);
};

} // namespace metrace::db
