#include "http/Router.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "db/Models.h"
#include "http/JsonUtil.h"
#include "service/ItemService.h"
#include "service/ServiceError.h"

namespace metrace::http {
namespace {

using nlohmann::json;

/// 单次请求的开始时间。httplib 的 pre_routing_handler 与 logger 都在同一个工作线程上执行，
/// 所以用 thread_local 传递时间戳是安全的，可以算出耗时写进访问日志。
thread_local std::chrono::steady_clock::time_point g_requestStart;

/// 访问日志、404 兜底、异常兜底、请求计时
void registerInfrastructure(httplib::Server& server)
{
    server.set_pre_routing_handler([](const httplib::Request&, httplib::Response&) {
        g_requestStart = std::chrono::steady_clock::now();
        return httplib::Server::HandlerResponse::Unhandled; // 继续走正常路由
    });

    // 记录请求方法、路径、状态码、耗时（AGENT.md §8.3）
    server.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - g_requestStart)
                                 .count();
        std::cout << req.method << " " << req.path << " -> " << res.status << " (" << elapsed
                  << "ms)" << std::endl;
    });

    // httplib 对任何 >=400 的响应都会回调这里，且调用前不会清空 res.body。
    // 所以业务代码自己写好的 4xx 响应必须原样保留，只兜底那些没有 body 的。
    server.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (!res.body.empty()) {
            return;
        }
        sendError(res, res.status, "request failed: " + req.method + " " + req.path);
    });

    // 把业务异常映射成合适的状态码，其余异常统一 500
    server.set_exception_handler(
        [](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
            try {
                std::rethrow_exception(ep);
            } catch (const service::ValidationError& e) {
                sendError(res, 400, e.what());
            } catch (const service::NotFoundError& e) {
                sendError(res, 404, e.what());
            } catch (const std::exception& e) {
                sendError(res, 500, e.what());
            } catch (...) {
                sendError(res, 500, "unknown error");
            }
        });
}

/// GET /api/items —— 过滤 + 排序 + 分页
void registerItemListRoute(httplib::Server& server)
{
    server.Get("/api/items", [](const httplib::Request& req, httplib::Response& res) {
        const db::ItemQuery query = parseItemQuery(req);

        json items = json::array();
        for (const db::Item& item : service::ItemService::list(query)) {
            items.push_back(itemToJson(item));
        }

        sendJson(res, json{{"total", service::ItemService::count(query)}, {"items", items}});
    });
}

/// POST /api/items —— 新增
void registerItemCreateRoute(httplib::Server& server)
{
    server.Post("/api/items", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        if (!parseJsonBody(req, res, body)) {
            return;
        }

        const db::Item item = service::ItemService::create(parseItemInput(body));
        sendJson(res, itemToJson(item), 201);
    });
}

/// GET /api/items/{id}
void registerItemGetRoute(httplib::Server& server)
{
    server.Get(R"(/api/items/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        const std::optional<std::int64_t> id = parsePathId(req, res);
        if (!id) {
            return;
        }

        const std::optional<db::Item> item = service::ItemService::get(*id);
        if (!item) {
            sendError(res, 404, "item not found");
            return;
        }
        sendJson(res, itemToJson(*item));
    });
}

/// PUT /api/items/{id} —— 部分更新
void registerItemUpdateRoute(httplib::Server& server)
{
    server.Put(R"(/api/items/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        const std::optional<std::int64_t> id = parsePathId(req, res);
        if (!id) {
            return;
        }

        json body;
        if (!parseJsonBody(req, res, body)) {
            return;
        }

        const db::Item item = service::ItemService::update(*id, parseItemInput(body));
        sendJson(res, itemToJson(item));
    });
}

/// DELETE /api/items/{id}
void registerItemDeleteRoute(httplib::Server& server)
{
    server.Delete(R"(/api/items/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        const std::optional<std::int64_t> id = parsePathId(req, res);
        if (!id) {
            return;
        }

        service::ItemService::remove(*id);
        res.status = 204; // 无 body
    });
}

/// GET /api/tags
void registerTagListRoute(httplib::Server& server)
{
    server.Get("/api/tags", [](const httplib::Request& req, httplib::Response& res) {
        int limit = kDefaultPageLimit;
        int offset = 0;
        applyPagination(req, limit, offset);

        json items = json::array();
        for (const db::Tag& tag : service::ItemService::listTags(limit, offset)) {
            items.push_back(tagToJson(tag));
        }

        sendJson(res, json{{"total", service::ItemService::countTags()}, {"items", items}});
    });
}

/// POST /api/tags
void registerTagCreateRoute(httplib::Server& server)
{
    server.Post("/api/tags", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        if (!parseJsonBody(req, res, body)) {
            return;
        }
        if (!body.contains("name") || !body["name"].is_string()) {
            sendError(res, 400, "field 'name' is required and must be a non-empty string");
            return;
        }

        const db::Tag tag = service::ItemService::createTag(body["name"].get<std::string>());
        sendJson(res, tagToJson(tag), 201);
    });
}

} // namespace

void registerRoutes(httplib::Server& server)
{
    registerInfrastructure(server);

    // 非业务路由：不加 /api 前缀
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("MeTrace-Server is running\n", "text/plain; charset=utf-8");
    });
    server.Get("/ping", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, json{{"status", "ok"}, {"service", "MeTrace-Server"}});
    });

    // 条目
    registerItemListRoute(server);
    registerItemCreateRoute(server);
    registerItemGetRoute(server);
    registerItemUpdateRoute(server);
    registerItemDeleteRoute(server);

    // 标签
    registerTagListRoute(server);
    registerTagCreateRoute(server);
}

} // namespace metrace::http
