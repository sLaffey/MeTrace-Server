#include "core/Item.h"

namespace metrace::core {

bool checkType(const std::string& type)
{
    for (const std::string& t : kItemTypeName) {
        if (t == type) {
            return true;
        }
    }
    return false;
}

bool Item::hasTag(const Tag& tag) const
{
    for (const Tag& t : tags) {
        if (t == tag) {
            return true;
        }
    }
    return false;
}

/// @brief 仅通过 id 判断条目是否相等
/// @param item 
bool Item::operator == (const Item& item) const
{
    return id == item.getId();
}

nlohmann::json Item::toJson() const
{
    nlohmann::json j;
    j["id"] = id;
    j["type"] = type;
    j["title"] = title;
    j["author"] = author;
    j["description"] = description;
    j["date"] = date;
    j["progress"] = progress;
    j["score"] = score;
    j["comment"] = comment;
    j["tags"] = tags;
    j["created_at"] = created_at;
    j["updated_at"] = updated_at;
    return j;
}

Item Item::fromJson(const nlohmann::json& j)
{
    Item item;
    item.id = j["id"];
    item.type = j["type"].get<std::string>();
    item.title = j["title"].get<std::string>();
    item.author = j["author"].get<std::string>();
    item.description = j["description"].get<std::string>();
    item.date = j["date"].get<std::string>();
    item.progress = j["progress"].get<double>();
    item.score = j["score"].get<int>();
    item.comment = j["comment"].get<std::string>();
    item.tags = j["tags"].get<std::vector<metrace::core::Tag>>();
    item.created_at = j["created_at"].get<std::string>();
    item.updated_at = j["updated_at"].get<std::string>();
    return item;
}

} // namespace metrace::core