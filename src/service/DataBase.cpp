#include <fstream>

#include <nlohmann/json.hpp>

#include "service/DataBase.h"

namespace metrace::service {

DataBase::DataBase(const std::string& dbPath)
{
    // json 文件约定：包含 items 和 tags 两个键值对
    using json = nlohmann::json;
    std::ifstream file(dbPath);
    json data = json::parse(file);

    for (const auto &itemJson : data["items"]) {
        items.insert(metrace::core::Item::fromJson(itemJson));
    }
}

} // namespace metrace::service