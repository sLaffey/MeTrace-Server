#include "http/JsonUtil.h"

#include <algorithm>
#include <exception>
#include <string>
#include <vector>

#include "service/ServiceError.h"

namespace metrace::http {
namespace {

using nlohmann::json;

/// 取一个可空文本字段的三态值：null 或空串都表示"清空"，字符串表示"设为该值"。
db::Patch<std::string> parseTextPatch(const json& body, const char* key)
{
    if (!body.contains(key)) {
        return {}; // 未提供 → 不修改
    }

    const json& value = body.at(key);
    if (value.is_null()) {
        return db::Patch<std::string>::clear();
    }
    if (!value.is_string()) {
        throw service::ValidationError(std::string("field '") + key + "' must be a string or null");
    }

    const std::string text = value.get<std::string>();
    if (text.empty()) {
        return db::Patch<std::string>::clear();
    }
    return db::Patch<std::string>::of(text);
}

/// 取一个可空数值字段（null 表示清空，如 score 置为未评分）。
db::Patch<double> parseDoublePatch(const json& body, const char* key)
{
    if (!body.contains(key)) {
        return {};
    }

    const json& value = body.at(key);
    if (value.is_null()) {
        return db::Patch<double>::clear();
    }
    if (!value.is_number()) {
        throw service::ValidationError(std::string("field '") + key + "' must be a number or null");
    }
    return db::Patch<double>::of(value.get<double>());
}

/// 取 year 字段（整数或 null）
db::Patch<int> parseYearPatch(const json& body)
{
    if (!body.contains("year")) {
        return {};
    }

    const json& value = body.at("year");
    if (value.is_null()) {
        return db::Patch<int>::clear();
    }
    if (!value.is_number_integer()) {
        throw service::ValidationError("field 'year' must be an integer or null");
    }
    return db::Patch<int>::of(value.get<int>());
}

/// 取 type 字段
db::Patch<db::ItemType> parseTypePatch(const json& body)
{
    if (!body.contains("type")) {
        return {};
    }

    const json& value = body.at("type");
    if (value.is_null()) {
        return db::Patch<db::ItemType>::clear();
    }
    if (!value.is_string()) {
        throw service::ValidationError("field 'type' must be a string or null");
    }

    const std::optional<db::ItemType> type = db::parseItemType(value.get<std::string>());
    if (!type) {
        throw service::ValidationError("field 'type' must be one of book/movie/music");
    }
    return db::Patch<db::ItemType>::of(*type);
}

/// 取 status 字段
db::Patch<db::ItemStatus> parseStatusPatch(const json& body)
{
    if (!body.contains("status")) {
        return {};
    }

    const json& value = body.at("status");
    if (value.is_null()) {
        return db::Patch<db::ItemStatus>::clear();
    }
    if (!value.is_string()) {
        throw service::ValidationError("field 'status' must be a string or null");
    }

    const std::optional<db::ItemStatus> status = db::parseItemStatus(value.get<std::string>());
    if (!status) {
        throw service::ValidationError("field 'status' must be one of wish/doing/done/dropped");
    }
    return db::Patch<db::ItemStatus>::of(*status);
}

/// 取 tags 字段：数组表示整体替换，null 表示清空；数组内重复项会被去掉。
db::Patch<std::vector<std::string>> parseTagsPatch(const json& body)
{
    if (!body.contains("tags")) {
        return {};
    }

    const json& value = body.at("tags");
    if (value.is_null()) {
        return db::Patch<std::vector<std::string>>::clear();
    }
    if (!value.is_array()) {
        throw service::ValidationError("field 'tags' must be an array of strings");
    }

    std::vector<std::string> names;
    for (const json& entry : value) {
        if (!entry.is_string()) {
            throw service::ValidationError("field 'tags' must be an array of strings");
        }
        const std::string name = entry.get<std::string>();
        if (name.empty()) {
            continue;
        }
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    return db::Patch<std::vector<std::string>>::of(names);
}

/// 空串输出 null，便于客户端统一判空。
json textOrNull(const std::string& text)
{
    return text.empty() ? json(nullptr) : json(text);
}

} // namespace

void sendJson(httplib::Response& res, const json& body, int status)
{
    res.status = status;
    res.set_content(body.dump(), kJsonContentType);
}

void sendError(httplib::Response& res, int status, const std::string& message)
{
    sendJson(res, json{{"error", message}}, status);
}

bool parseJsonBody(const httplib::Request& req, httplib::Response& res, json& out)
{
    out = json::parse(req.body, nullptr, false); // 不抛异常，用 is_discarded 判断
    if (out.is_discarded() || !out.is_object()) {
        sendError(res, 400, "request body must be a JSON object");
        return false;
    }
    return true;
}

int queryInt(const httplib::Request& req, const char* name, int fallback)
{
    const std::string raw = req.get_param_value(name);
    if (raw.empty()) {
        return fallback;
    }
    try {
        return std::stoi(raw);
    } catch (const std::exception&) {
        throw service::ValidationError(std::string("query parameter '") + name + "' must be an integer");
    }
}

std::optional<std::int64_t> parsePathId(const httplib::Request& req, httplib::Response& res)
{
    constexpr std::size_t kIdGroupIndex = 1; // 路由里的 (\d+) 是第 1 个捕获组

    if (req.matches.size() <= kIdGroupIndex) {
        sendError(res, 400, "path parameter 'id' is missing");
        return std::nullopt;
    }

    try {
        const std::int64_t id = std::stoll(req.matches[kIdGroupIndex].str());
        if (id <= 0) {
            throw std::out_of_range("id must be positive");
        }
        return id;
    } catch (const std::exception&) {
        sendError(res, 400, "path parameter 'id' must be a positive integer");
        return std::nullopt;
    }
}

db::ItemInput parseItemInput(const json& body)
{
    db::ItemInput input;
    input.type = parseTypePatch(body);
    input.title = parseTextPatch(body, "title");
    input.creator = parseTextPatch(body, "creator");
    input.year = parseYearPatch(body);
    input.coverUrl = parseTextPatch(body, "cover_url");
    input.description = parseTextPatch(body, "description");
    input.status = parseStatusPatch(body);
    input.progress = parseDoublePatch(body, "progress");
    input.score = parseDoublePatch(body, "score");
    input.review = parseTextPatch(body, "review");
    input.tags = parseTagsPatch(body);
    return input;
}

db::ItemQuery parseItemQuery(const httplib::Request& req)
{
    db::ItemQuery query;

    const std::string type = req.get_param_value("type");
    if (!type.empty()) {
        const std::optional<db::ItemType> parsed = db::parseItemType(type);
        if (!parsed) {
            throw service::ValidationError("query parameter 'type' must be one of book/movie/music");
        }
        query.type = *parsed;
    }

    const std::string status = req.get_param_value("status");
    if (!status.empty()) {
        const std::optional<db::ItemStatus> parsed = db::parseItemStatus(status);
        if (!parsed) {
            throw service::ValidationError(
                "query parameter 'status' must be one of wish/doing/done/dropped");
        }
        query.status = *parsed;
    }

    const std::string tag = req.get_param_value("tag");
    if (!tag.empty()) {
        query.tag = tag;
    }

    const std::string sort = req.get_param_value("sort");
    if (!sort.empty()) {
        const std::optional<std::pair<db::SortField, bool>> parsed = db::parseSort(sort);
        if (!parsed) {
            throw service::ValidationError(
                "query parameter 'sort' must be one of score/year/created_at/title, "
                "optionally prefixed with '-' for descending order");
        }
        query.sort = parsed->first;
        query.desc = parsed->second;
    }

    applyPagination(req, query.limit, query.offset);
    return query;
}

void applyPagination(const httplib::Request& req, int& limit, int& offset)
{
    limit = std::clamp(queryInt(req, "limit", kDefaultPageLimit), 1, kMaxPageLimit);
    offset = std::max(queryInt(req, "offset", 0), 0);
}

json itemToJson(const db::Item& item)
{
    json body;
    body["id"] = item.id;
    body["type"] = db::toString(item.type);
    body["title"] = item.title;
    body["creator"] = textOrNull(item.creator);
    body["year"] = item.year == 0 ? json(nullptr) : json(item.year);
    body["cover_url"] = textOrNull(item.coverUrl);
    body["description"] = textOrNull(item.description);
    body["status"] = db::toString(item.status);
    body["progress"] = item.progress;
    body["score"] = item.score ? json(*item.score) : json(nullptr);
    body["review"] = textOrNull(item.review);
    body["tags"] = item.tags;
    body["created_at"] = item.createdAt;
    body["updated_at"] = item.updatedAt;
    return body;
}

json tagToJson(const db::Tag& tag)
{
    return json{{"id", tag.id}, {"name", tag.name}};
}

} // namespace metrace::http
