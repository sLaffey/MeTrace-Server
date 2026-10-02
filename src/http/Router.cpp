#include <charconv>

#include "http/Router.h"
#include "service/DataBase.h"

namespace metrace::http {

namespace {

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

} // namespace

void registerRoutes(httplib::Server& server, metrace::service::DataBase& db)
{
    server.Get("/ping", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("MeTrace-Server is running", "application/json");
    });

    // /api/items 接口，返回 Item 列表
    server.Get("/api/items", [&db](const httplib::Request& req, httplib::Response& res) {
        const auto& parsed_query = parseItemQuery(req);
        if (!parsed_query.has_value()) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid query parameters"})", "application/json");
            return;
        }

        const auto& query = parsed_query.value();
        const auto& ans = db.query(query);

        
    });
}

} // namespace metrace::http