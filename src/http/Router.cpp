#include <charconv>
#include <limits>
#include <utility>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>
#include <httplib.h>

#include "core/Item.h"
#include "http/Router.h"
#include "service/DataBase.h"

namespace metrace::http {

namespace {

/// @brief 较标准库更严格的字符串向整数的转换
/// @param str 
/// @param res 
/// @return 是否正常解析
static bool parseIntStrict(const std::string& str, long long& res)
{
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), res);
    return ec == std::errc() && ptr == str.data() + str.size();
}

static std::optional<int> parseItemId(const httplib::Request& req)
{
    long long id = 0;
    if (!parseIntStrict(req.matches[1].str(), id) || id > std::numeric_limits<int>::max() || id < std::numeric_limits<int>::min()) {
        return std::nullopt;
    }
    return static_cast<int>(id);
}

static std::optional<metrace::service::SortField> parseSortField(const std::string& str)
{
    using metrace::service::SortField;
    if (str == "created_at") return SortField::CreatedAt;
    if (str == "score") return SortField::Score;
    if (str == "date") return SortField::Date;
    if (str == "title") return SortField::Title;
    return std::nullopt;
}

static std::optional<metrace::service::ItemQuery> parseItemQuery(const httplib::Request& req)
{
    using metrace::service::ItemQuery;
    using metrace::service::SortField;
    ItemQuery q;

    if (req.has_param("type")) {
        const std::string& v = req.get_param_value("type");
        if (!v.empty()) { // 空值视同未提供
            if (!metrace::core::checkType(v)) return std::nullopt;
            q.type = std::move(v);
        }
    }
    if (req.has_param("tag")) {
        const std::string& v = req.get_param_value("tag");
        if (!v.empty()) q.tag = std::move(v);
    }
    if (req.has_param("sort")) {
        std::string v = req.get_param_value("sort");
        if (!v.empty()) {
            q.descending = false;
            if (v[0] == '-') {
                q.descending = true;
                v.erase(0, 1);
            }
            auto sort_field = parseSortField(v);
            if (!sort_field.has_value()) return std::nullopt;
            q.sort_field = sort_field.value();
        }
    }
    if (req.has_param("limit")) {
        const std::string& v = req.get_param_value("limit");
        if (!v.empty()) {
            long long limit;
            if (!parseIntStrict(v, limit)) return std::nullopt;
            if (limit < 1) limit = 1;
            if (limit > 100) limit = 100;
            q.limit = static_cast<std::size_t>(limit);
        }
    }
    if (req.has_param("offset")) {
        const std::string& v = req.get_param_value("offset");
        if (!v.empty()) {
            long long offset;
            if (!parseIntStrict(v, offset) || offset < 0) return std::nullopt;
            q.offset = offset;
        }
    }

    return q;
}

/// @brief 检查 Item 是否合法
/// @param item 
/// @details Item 格式见 Item 类定义 @cite /include/core/Item.h
bool checkItem(const metrace::core::Item& item)
{
    if (!metrace::core::checkType(item.getType())) {
        return false;
    }
    if (item.getTitle().empty() || item.getTitle().length() > 200) {
        return false;
    }
    return true;
}

static std::optional<std::string> checkItemJson(const nlohmann::json& j)
{
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& key = it.key();
        if (key == "id" || key == "created_at" || key == "updated_at") {
            return key + " is read-only";
        }
        else if (key == "type") {
            if (!it.value().is_string()) {
                return "Type must be a string";
            }

            const std::string& value = it.value();
            if (!metrace::core::checkType(value)) {
                return "Invalid type " + value;
            }
        }
        else if (key == "title") {
            if (!it.value().is_string()) {
                return "Title must be a string";
            }

            if (!metrace::core::checkTitle(it.value())) {
                return "Title can't be empty and its length must not exceed 200";
            }
        }
        else if (key == "author") {
            if (!it.value().is_string()) {
                return "Author must be a string";
            }
        }
        else if (key == "description") {
            if (!it.value().is_string()) {
                return "Description must be a string";
            }
        }
        else if (key == "date") {
            if (!it.value().is_string()) {
                return "Date must be a string";
            }
        }
        else if (key == "progress") {
            if (!it.value().is_number()) {
                return "Progress must be a number between 0 and 1";
            }
            if (!metrace::core::checkProgress(it.value())) {
                return "Progress must between 0 and 1";
            }
        }
        else if (key == "score") {
            if (!it.value().is_number_integer()) {
                return "Score must be a integer between 0 and 100";
            }
            if (!metrace::core::checkScore(it.value())) {
                return "Score must between 0 and 100";
            }
        }
        else if (key == "comment") {
            if (!it.value().is_string()) {
                return "Comment must be a string";
            }
        }
        else if (key == "tags") {
            if (!it.value().is_array()) return "Tags must be an array of strings";
            if (it.value().size() > 20) return "Too many tags (max 20)";
            for (const auto& t : it.value()) {
                if (!t.is_string()) return "Tags must be an array of strings";
            }
            if (!metrace::core::checkTags(it.value().get<std::vector<metrace::core::Tag>>())) {
                return "Tag must be non-empty and <=50 bytes";
            }
        }
        else {
            return "Unknown property of item: " + key;
        }
    }

    return std::nullopt;
}

/// @brief 将 JSON 格式解析为 ItemPatch
/// @param body 
/// @param patch 
/// @return 错误信息，若为 nullopt 代表正常解析
static std::optional<std::string> parseItemPatch(const nlohmann::json& body, metrace::service::ItemPatch& patch)
{
    if (!body.is_object() || body.is_discarded()) {
        return "Body must be a JSON object";
    }
    std::optional<std::string> ckres = checkItemJson(body);
    if (ckres.has_value()) return ckres;

    if (body.contains("type"))          patch.type          = body["type"].get<std::string>();
    if (body.contains("title"))         patch.title         = body["title"].get<std::string>();
    if (body.contains("author"))        patch.author        = body["author"].get<std::string>();
    if (body.contains("description"))   patch.description   = body["description"].get<std::string>();
    if (body.contains("date"))          patch.date          = body["date"].get<std::string>();
    if (body.contains("progress"))      patch.progress      = body["progress"].get<double>();
    if (body.contains("score"))         patch.score         = body["score"].get<int>();
    if (body.contains("comment"))       patch.comment       = body["comment"].get<std::string>();
    if (body.contains("tags"))          patch.tags          = body["tags"].get<std::vector<metrace::core::Tag>>();

    return std::nullopt;
}

static std::optional<metrace::service::TagQuery> parseTagQuery(const httplib::Request& req)
{
    metrace::service::TagQuery q;

    if (req.has_param("limit")) {
        const std::string& v = req.get_param_value("limit");
        if (!v.empty()) {
            long long limit;
            if (!parseIntStrict(v, limit)) return std::nullopt;
            if (limit < 1) limit = 1;
            if (limit > 100) limit = 100;
            q.limit = static_cast<std::size_t>(limit);
        }
    }
    if (req.has_param("offset")) {
        const std::string& v = req.get_param_value("offset");
        if (!v.empty()) {
            long long offset;
            if (!parseIntStrict(v, offset) || offset < 0) return std::nullopt;
            q.offset = offset;
        }
    }

    return q;
}

static std::string errorBody(const std::string& msg)
{
    nlohmann::json j;
    j["error"] = msg;
    return j.dump();
}

constexpr const char* kJsonType = "application/json; charset=utf-8";

} // namespace

void registerRoutes(httplib::Server& server, metrace::service::DataBase& db)
{
    server.Get("/ping", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok","service":"MeTrace-Server"})", kJsonType);
    });

    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("MeTrace-Server is running", "text/plain; charset=utf-8");
    });

    // GET /api/items 接口，返回 Item 列表
    server.Get("/api/items", [&db](const httplib::Request& req, httplib::Response& res) {
        const auto parsed_query = parseItemQuery(req);
        if (!parsed_query.has_value()) {
            res.status = 400;
            res.set_content(errorBody("Invalid query parameters"), kJsonType);
            return;
        }

        const auto query = parsed_query.value();
        const metrace::service::ItemQueryResult ans = db.queryItem(query);

        nlohmann::json out;
        out["total"] = ans.total;
        out["items"] = nlohmann::json::array();
        for (const metrace::core::Item* it : ans.items) {
            out["items"].push_back(it->toJson());
        }

        res.set_content(out.dump(), kJsonType);
    });

    // POST /api/items create an item, returns status code 201 and the item
    server.Post("/api/items", [&db](const httplib::Request& req, httplib::Response& res) {
        const nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);

        if (body.is_discarded() || !body.is_object()) {
            res.status = 400;
            res.set_content(errorBody("Request body is not a JSON object"), kJsonType);
            return;
        }
        const std::optional<std::string> ckres = checkItemJson(body);
        if (ckres.has_value()) {
            res.status = 400;
            res.set_content(errorBody(ckres.value()), kJsonType);
            return;
        }
        if (!body.contains("type") || !body.contains("title")) {
            res.status = 400;
            res.set_content(errorBody("type and title are required"), kJsonType);
            return;
        }

        metrace::core::Item item = metrace::core::Item::fromCreateJson(body);
        
        if (!checkItem(item)) {
            res.status = 400;
            res.set_content(errorBody("Invalid item members"), kJsonType);
            return;
        }

        const metrace::core::Item* ins = db.createItem(item);
        res.status = 201;
        res.set_content(ins->toJson().dump(), kJsonType);
        return;
    });

    // GET /api/items/{id} 返回单个条目的序列化数据
    server.Get("/api/items/(\\d+)", [&db](const httplib::Request& req, httplib::Response& res) {
        const std::optional<int> id = parseItemId(req);
        const metrace::core::Item* item = id.has_value() ? db.getItem(id.value()) : nullptr;

        if (!item) {
            res.status = 404;
            res.set_content(errorBody("Item not found"), kJsonType);
            return;
        }

        res.set_content(item->toJson().dump(), kJsonType);
        return;
    });

    // PUT /api/items/{id} 实现对条目的部分修改，不填即为不修改
    server.Put("/api/items/(\\d+)", [&db](const httplib::Request& req, httplib::Response& res) {
        const std::optional<int> id = parseItemId(req);
        const metrace::core::Item* item = id.has_value() ? db.getItem(id.value()) : nullptr;

        if (!item) {
            res.status = 404;
            res.set_content(errorBody("Item not found"), kJsonType);
            return;
        }

        metrace::service::ItemPatch patch;
        nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
        std::optional<std::string> message = parseItemPatch(body, patch);
        if (message.has_value()) {
            res.status = 400;
            res.set_content(errorBody(message.value()), kJsonType);
            return;
        }

        item = db.updateItem(id.value(), patch);
        res.set_content(item->toJson().dump(), kJsonType);
        return;
    });

    // DELETE /api/items/{id} 删除条目，返回 204
    server.Delete("/api/items/(\\d+)", [&db](const httplib::Request& req, httplib::Response& res) {
        const std::optional<int> id = parseItemId(req);
        if (!id.has_value() || !db.removeItem(id.value())) {
            res.status = 404;
            res.set_content(errorBody("Item not found"), kJsonType);
        }
        else {
            res.status = 204;
        }
        return;
    });

    // GET /api/tags 返回所有 tags 列表
    server.Get("/api/tags", [&db](const httplib::Request& req, httplib::Response& res) {
        const std::optional<metrace::service::TagQuery> q = parseTagQuery(req);

        if (!q.has_value()) {
            res.status = 400;
            res.set_content(errorBody("Invalid GET parameters"), kJsonType);
            return;
        }

        const metrace::service::TagQueryResult ans = db.queryTag(q.value());
        nlohmann::json j;
        j["total"] = ans.total;
        j["tags"] = nlohmann::json::array();
        for (const metrace::core::Tag& tag : ans.tags) {
            j["tags"].push_back(tag);
        }

        res.set_content(j.dump(), kJsonType);
        return;
    });
}

} // namespace metrace::http