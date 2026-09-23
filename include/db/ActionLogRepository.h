#pragma once

#include <string>

namespace metrace::db {

/**
 * action_log 表：审计日志，只记录"发生过什么"，不参与撤销/重做。
 * payload 由业务层序列化成 JSON 字符串后传入，因此本层不依赖 nlohmann/json。
 */
class ActionLogRepository {
public:
    /// 追加一条操作日志；id 不可用（写日志失败不应影响主流程，调用方可按需处理异常）。
    static void append(const std::string& action, const std::string& payloadJson);
};

} // namespace metrace::db
