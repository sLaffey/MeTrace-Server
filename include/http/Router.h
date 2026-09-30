#pragma once

#include <httplib.h>

#include "service/DataBase.h"

namespace metrace::http {

void registerRoutes(httplib::Server& server, metrace::service::DataBase& db);

}