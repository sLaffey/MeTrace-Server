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
        res.set_content(ins->toJson(), kJsonType);
        return;
    });
}

} // namespace metrace::http