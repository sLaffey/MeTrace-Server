#include <charconv>

#include <nlohmann/json.hpp>

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
            if (!parseIntStrict(v, limit) || limit < 0) return std::nullopt;
            q.limit = limit;
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

/// @brief 将 JSON 格式解析为 ItemPatch
/// @param body 
/// @param patch 
/// @return 错误信息，若为 nullopt 代表正常解析
static std::optional<std::string> parseItemPatch(const nlohmann::json& body, metrace::service::ItemPatch& patch)
{
    if (!body.is_object()) {
        return "body must be a JSON object";
    }

    for (auto it = body.begin(); it != body.end(); ++it) {
        const std::string& key = it.key();
        if (key == "id" || key == "created_at" || key == "updated_at") {
            return key + " is read-only";
        }
        else if (key == "type") {
            const std::string& value = it.value();
            if (!metrace::core::checkType(value)) {
                return "Invalid type " + value;
            }
            patch.type = value;
        }
        else if (key == "title") {
            const std::string& value = it.value();
            if (value.length() > 200) {
                return "Title length must not exceed 200";
            }
            patch.title = value;
        }
        else if (key == "author") {
            patch.author = it.value();
        }
        else if (key == "description") {
            patch.description = it.value();
        }
        else if (key == "date") {
            patch.date = it.value();
        }
        else if (key == "progress") {
            double val = it.value();
            if (val < 0 || val > 1) {
                return "Progress must between 0 and 1";
            }
            patch.progress = val;
        }
        else if (key == "score") {
            int value = it.value();
            if (value < 0 || value > 100) {
                return "Score must between 0 and 100";
            }
            patch.score = value;
        }
        else if (key == "comment") {
            patch.comment = it.value();
        }
        else if (key == "tags") {
            patch.tags = it.value();
        }
        else {
            return "Unknown property of item: " + key;
        }
    }

    return std::nullopt;
}

constexpr const char* kJsonType = "application/json; charset=utf-8";

} // namespace

void registerRoutes(httplib::Server& server, metrace::service::DataBase& db)
{
    server.Get("/ping", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", kJsonType);
    });

    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("MeTrace-Server is running", kJsonType);
    });

    // GET /api/items 接口，返回 Item 列表
    server.Get("/api/items", [&db](const httplib::Request& req, httplib::Response& res) {
        const auto parsed_query = parseItemQuery(req);
        if (!parsed_query.has_value()) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid query parameters"})", kJsonType);
            return;
        }

        const auto query = parsed_query.value();
        const metrace::service::QueryResult ans = db.query(query);

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
        metrace::core::Item item = metrace::core::Item::fromCreateJson(req.body);
        
        if (!checkItem(item)) {
            res.status = 400;
            res.set_content(R"({"error": "Invalid item members"})", kJsonType);
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
            res.set_content(R"({"error": "Item not found"})", kJsonType);
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
            res.set_content(R"({"error": "Item not found"})", kJsonType);
            return;
        }

        metrace::service::ItemPatch patch;
        std::optional<std::string> message = parseItemPatch(req.body, patch);
        if (message.has_value()) {
            res.status = 400;
            res.set_content(message.value(), kJsonType);
            return;
        }

        item = db.updateItem(id.value(), patch);
        res.set_content(item->toJson().dump(), kJsonType);
        return;
    });

    // DELETE /api/items/{id} 删除条目，返回 204
    server.Delete("/api/items/(\\d+)", [&db](const httplib::Request& req, httplib::Response& res) {
        const std::optional<int> id = parseItemId(req);
        const metrace::core::Item* item = id.has_value() ? db.getItem(id.value()) : nullptr;

        if (!item) {
            res.status = 404;
            res.set_content(R"({"error": "Item not found"})", kJsonType);
            return;
        }

        db.removeItem(id.value());
        res.status = 204;
        return;
    });

    // GET /api/tags 返回所有 tags 列表
    
}

} // namespace metrace::http