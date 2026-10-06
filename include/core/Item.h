#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <cstdint>

#include "core/Tag.h"

namespace metrace::service { class DataBase; }

namespace metrace::core {

static const std::string kItemTypeName[4] = {
    "null", "book", "movie", "music"
};

bool checkType(const std::string& type);

/// @brief 条目类，表示一个条目对象
/// @details 包含条目的基本信息，如标题、作者、简介、发布日期、观看进度、评分、评论、标签等
/// 提供 JSON 序列化和反序列化功能
/// ID、创建时间、更新时间自动管理，其它用户可修改
class Item {

private:
    int id = 0;                     // ID
    std::string type;               // 条目类型 要求须在四种合法类型之内
    std::string title;              // 条目标题 必填，要求不超过 200 字符
    std::string author;             // 条目作者/导演/艺术家 
    std::string description;        // 条目简介
    std::string date;               // 条目发布日期 TODO: 定义成单独的类
    double progress = 0;            // 条目观看进度 0-1 之间的浮点数
    int score = 0;                  // 条目评分 0-100，0 表示未评分
    std::string comment;            // 条目评论
    std::vector<Tag> tags;          // 条目标签 每个标签不超过 50 字符
    std::int64_t created_at = 0;    // 条目创建时间
    std::int64_t updated_at = 0;    // 条目更新时间

    Item() = default;

    friend class ::metrace::service::DataBase; // 方便 DataBase 对 id 和时间进行管理

public:
    Item(const Item&) = default;
    ~Item() = default;

    bool operator == (const Item& a) const;

    // set and get methods
    void setType(std::string _type) { type = _type; }
    void setTitle(const std::string& _title) { title = _title; }
    void setAuthor(const std::string& _author) { author = _author; }
    void setDescription(const std::string& _description) { description = _description; }
    void setDate(const std::string& _date) { date = _date; }
    void setProgress(const double _progress) { progress = _progress; }
    void setScore(const int _score) { score = _score; }
    void setComment(const std::string& _comment) { comment = _comment; }
    void setTags(const std::vector<Tag>& _tags) { tags = _tags; uniqueTags(); }

    int getId() const { return id; }
    const std::string& getType() const { return type; }
    const std::string& getTitle() const { return title; }
    const std::string& getAuthor() const { return author; }
    const std::string& getDescription() const { return description; }
    const std::string& getDate() const { return date; }
    double getProgress() const { return progress; }
    int getScore() const { return score; }
    const std::string& getComment() const { return comment; }
    const std::vector<Tag>& getTags() const { return tags; }
    std::int64_t getCreatedAt() const { return created_at; }

    bool hasTag(const Tag& tag) const;
    void touch();
    void uniqueTags();

    // JSON serialization and deserialization
    nlohmann::json toJson() const;
    static Item fromJson(const nlohmann::json&);
    static Item fromCreateJson(const nlohmann::json&);
};

} // namespace metrace::core