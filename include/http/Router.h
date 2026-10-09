#pragma once

namespace httplib { class Server; }

namespace metrace::service { class DataBase; }

namespace metrace::http {

void registerRoutes(httplib::Server& server, metrace::service::DataBase& db);

}