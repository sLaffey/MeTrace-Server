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

/// @brief 按照 id 获取条目，返回指针，若不存在返回 nullptr
/// @param id 
/// @return 一个指向条目的指针，若不存在返回 nullptr
const metrace::core::Item* DataBase::getItem(int id) const
{
    const metrace::core::Item* item = items.find([id](const metrace::core::Item& item) { return item.getId() == id; });
    return item;
}

/// @brief 根据传入的 patch 更新条目，返回更新后的条目指针，若不存在返回 nullptr
/// @param id 
/// @param patch 
/// @return 更新后的条目指针，不存在则为 nullptr
const metrace::core::Item* metrace::service::DataBase::updateItem(int id, const metrace::service::ItemPatch& patch)
{
    metrace::core::Item* item = items.find([id](const metrace::core::Item& item) { return item.getId() == id; });
    if (item == nullptr) {
        return nullptr;
    }

    if (patch.type.has_value()) item->setType(patch.type.value());
    if (patch.title.has_value()) item->setTitle(patch.title.value());
    if (patch.author.has_value()) item->setAuthor(patch.author.value());
    if (patch.description.has_value()) item->setDescription(patch.description.value());
    if (patch.date.has_value()) item->setDate(patch.date.value());
    if (patch.progress.has_value()) item->setProgress(patch.progress.value());
    if (patch.score.has_value()) item->setScore(patch.score.value());
    if (patch.comment.has_value()) item->setComment(patch.comment.value());
    if (patch.tags.has_value()) item->setTags(patch.tags.value());

    return item;
}

/// @brief 根据 id 删除条目
/// @param id 
/// @return 是否成功删除
bool metrace::service::DataBase::removeItem(int id)
{
    return items.remove([id](const metrace::core::Item& item) { return item.getId() == id; });
}

/// @brief 插入新条目
/// @param item 
/// @return 指向新条目的指针
const metrace::core::Item* DataBase::createItem(const metrace::core::Item& item)
{
    return items.insert(item);
}

/// @brief 查找标签
/// @param tag 
/// @return 返回该标签的指针，不存在则为 nullptr
const metrace::core::Tag* DataBase::findTag(const metrace::core::Tag& tag) const
{
    return tags.find(tag);
}

/// @brief 插入新标签
/// @param tag 
/// @return 指向新标签的指针
const metrace::core::Tag* DataBase::createTag(const metrace::core::Tag& tag)
{
    return tags.insert(tag);
}

/// @brief 删除标签
/// @param tag 
/// @return 是否成功删除
bool DataBase::removeTag(const metrace::core::Tag& tag)
{
    return tags.remove(tag);
}

} // namespace metrace::service