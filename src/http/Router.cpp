#include "http/Router.h"
#include "service/DataBase.h"

namespace metrace::http {

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
        // TODO 使用手写堆实现 Top-N 查询
    });
}

} // namespace metrace::http