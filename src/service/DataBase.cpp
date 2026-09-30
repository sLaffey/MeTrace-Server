#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "service/DataBase.h"

namespace metrace::service {

DataBase::DataBase(const std::string& dbPath)
{
    if (!load(dbPath)) {
        throw std::runtime_error("Failed to load database file.");
    }
}

bool DataBase::load(const std::string& dbPath)
{
    // json 文件约定: { "items": [...], "tags": [...], "next_id": <int> }
    using json = nlohmann::json;
    std::ifstream file(dbPath);
    if (!file.is_open()) {
        if (!std::filesystem::exists(dbPath)) {
            std::cerr << "[DataBase] Warning: Database file doesn't exist, starting from empty." << std::endl;
            return true;
        }
        std::cerr << "[DataBase] Fatal: Can't open database file." << std::endl;
        return false;
    }
    try {
        json data = json::parse(file);
        int max_id = 0;

        for (const auto &itemJson : data["items"]) {
            metrace::core::Item item = metrace::core::Item::fromJson(itemJson);
            items.insert(item);
            max_id = std::max(max_id, item.getId());
        }
        for (const auto& itemJson : data["tags"]) {
            tags.insert(itemJson);
        }
        next_id = data.value("next_id", 0);
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[DataBase] Fatal: Can't load database file in " << dbPath << " with exception " << e.what() << std::endl;
        return false;
    }
    return true;
}

} // namespace metrace::service