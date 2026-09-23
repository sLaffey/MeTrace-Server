#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "db/Models.h"

namespace metrace::http {

/// 项目统一的 JSON 响应 Content-Type
inline constexpr const char* kJsonContentType = "application/json; charset=utf-8";

/// 列表接口的默认与最大分页大小（AGENT.md §6.1）
inline constexpr int kDefaultPageLimit = 20;
inline constexpr int kMaxPageLimit = 100;

/// 写 JSON 响应
void sendJson(httplib::Response& res, const nlohmann::json& body, int status = 200);

/// 写错误响应：{"error": "..."}
void sendError(httplib::Response& res, int status, const std::string& message);

/// 解析请求体；不是合法的 JSON 对象时写 400 并返回 false
bool parseJsonBody(const httplib::Request& req, httplib::Response& res, nlohmann::json& out);

/// 取整型 query 参数；缺失返回 fallback，取值非法抛 service::ValidationError
int queryInt(const httplib::Request& req, const char* name, int fallback);

/// 解析路径参数里的数字 id；非法时写 400 并返回 nullopt
std::optional<std::int64_t> parsePathId(const httplib::Request& req, httplib::Response& res);

/// JSON 请求体 → ItemInput。
/// 本函数只负责"类型转换"：字段类型不合法就抛 service::ValidationError；
/// 必填项、取值范围等业务规则交给业务层判断。
db::ItemInput parseItemInput(const nlohmann::json& body);

/// query 参数 → 列表查询条件；枚举取值非法时抛 service::ValidationError
db::ItemQuery parseItemQuery(const httplib::Request& req);

/// 读分页参数：limit 默认 20 且被截断到 [1, 100]，offset 默认 0 且不小于 0
void applyPagination(const httplib::Request& req, int& limit, int& offset);

/// 领域对象 → JSON
nlohmann::json itemToJson(const db::Item& item);
nlohmann::json tagToJson(const db::Tag& tag);

} // namespace metrace::http
