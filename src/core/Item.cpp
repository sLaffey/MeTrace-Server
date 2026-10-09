#include <ctime>
#include <cstdint>
#include <algorithm>

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

bool checkTitle(const std::string& title)
{
    return !title.empty() && title.length() <= 200;
}

bool checkProgress(double progress)
{
    return progress >= 0 && progress <= 1;
}

bool checkScore(int score)
{
    return score >= 0 && score <= 100;
}

bool checkTag(const Tag& tag)
{
    return !tag.empty() && tag.length() <= 50;
}

bool checkTags(const std::vector<Tag>& tags)
{
    if (tags.size() > 20) return false;
    for (const auto& tag : tags) {
        if (!checkTag(tag)) return false;
    }
    return true;
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

/// @brief 从 json 创建 Item 对象，仅供 DataBase 从文件加载时使用
/// @param j
/// @return Item 对象实例
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
    item.created_at = j["created_at"].get<std::int64_t>();
    item.updated_at = j["updated_at"].get<std::int64_t>();
    return item;
}

/// @brief 从客户端 POST 的 json 构造 Item 对象
/// @param j 
/// @return Item 实例
/// @note 合法性校验由 HTTP 层保证，id 等由 DataBase 管理
Item Item::fromCreateJson(const nlohmann::json& j)
{
    Item item;

    // title 和 type 不可缺失
    item.title = j["title"].get<std::string>();
    item.type = j["type"].get<std::string>();

    // 其余可用默认值
    item.author = j.value("author", "");
    item.description = j.value("description", "");
    item.date = j.value("date", "");
    item.progress = j.value("progress", 0.0);
    item.score = j.value("score", 0);
    item.comment = j.value("comment", "");
    item.tags = j.value("tags", std::vector<Tag>());

    // 剩余 id created_at updated_at 由 DataBase 管理
    return item;
}

void Item::touch()
{
    updated_at = std::time(nullptr);
}

void Item::uniqueTags()
{
    std::sort(tags.begin(), tags.end());
    tags.erase(unique(tags.begin(), tags.end()), tags.end());
}

std::optional<std::string> validateItem(const Item& item)
{
    if (!checkType(item.getType())) return "type must be uncategorized/book/movie/music, got \"" + item.getType() + "\"";
    if (!checkTitle(item.getTitle())) return "title must be non-empty and <= 200 bytes, got \"" + item.getTitle() + "\"";
    if (!checkProgress(item.getProgress())) return "progress must be within [0, 1]";
    if (!checkScore(item.getScore())) return "score must be within [0, 100]";
    if (!checkTags(item.getTags())) return "tags must be <=20 non-empty names of <=50 bytes each";
    return std::nullopt;
}

} // namespace metrace::core