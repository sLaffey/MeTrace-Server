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

/// @brief 从 dbPath 处的 json 文件加载数据库, json 文件约定: { "items": [...], "tags": [...], "next_id": <int> }
/// @param dbPath 
/// @return true if success, false if failed
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

/// @brief save DataBase to dbPath
/// @param dbPath 
/// @return true if success, false if failed
bool DataBase::save(const std::string& dbPath) const
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    json data;
    data["next_id"] = next_id;
    data["items"] = json::array();
    items.forEach([&data] (const metrace::core::Item& item) { data.push_back(item.toJson()); });
    data["tags"] = json::array();
    tags.forEach([&data] (const metrace::core::Tag& tag) { data.push_back(tag); });

    const fs::path tmp = fs::path(dbPath) += ".tmp";
    {
        std::ofstream file(tmp);
        if (!file.is_open()) {
            std::cerr << "[DataBase] Failed to save Database: Can't open file " << tmp << std::endl;
            return false;
        }
        file << data.dump(4) << std::endl;
        file.flush();
        if (!file) {
            fs::remove(tmp);
            std::cerr << "[DataBase] Failed to save Database: Can't write file " << tmp << ", removed." << std::endl;
            return false;
        }
    }

    std::error_code ec;
    fs::rename(tmp, dbPath, ec);
    if (ec) {
        fs::remove(tmp, ec);
        std::cerr << "[DataBase] Failed to save Database: Can't rename file " << tmp
            << " -> " << dbPath << ": " << ec.message() << ", removed temp file." << std::endl;
        return false;
    }

    return true;
}

} // namespace metrace::service