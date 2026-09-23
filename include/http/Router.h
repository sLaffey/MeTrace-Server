#pragma once

#include <httplib.h>

namespace metrace::http {

/**
 * 注册全部路由，以及访问日志、404 兜底、异常处理。
 *
 * Router 只做三件事：解析请求（交给 JsonUtil）、调用业务层（Service）、写响应。
 * 这里不出现任何 SQL，也不直接访问数据库。
 */
void registerRoutes(httplib::Server& server);

} // namespace metrace::http
