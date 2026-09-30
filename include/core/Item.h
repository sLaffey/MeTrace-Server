#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "core/Tag.h"

namespace metrace::core {

static const std::string kItemTypeName[4] = {
    "null", "book", "movie", "music"
};

class Item {

/*
 * 条目类，表示一个条目对象
 * 包含条目的基本信息，如标题、作者、简介、发布日期、观看进度、评分、评论、标签等
 * 提供 JSON 序列化和反序列化功能
 * ID、创建时间、更新时间自动管理，其它用户可修改
 */

private:
    int id;                         // ID
    std::string type;               // 条目类型
    std::string title;              // 条目标题
    std::string author;             // 条目作者/导演/艺术家
    std::string description;        // 条目简介
    std::string date;               // 条目发布日期 TODO: 定义成单独的类
    double progress;                // 条目观看进度
    int score;                      // 条目评分 [1-100]
    std::string comment;            // 条目评论
    std::vector<Tag> tags;          // 条目标签
    std::string created_at;         // 条目创建时间
    std::string updated_at;         // 条目更新时间

    Item() = default;

public:
    ~Item() = default;

    bool operator == (const Item& a) const;

    // set and get methods
    // void setTitle(const std::string& _title) { title = _title; }
    // void setScore(int _score) { score = _score; }
    // void setComment(const std::string& _comment) { comment = _comment; }
    // void setType(ItemType _type) { type = _type; }

    int getId() const { return id; }
    // const std::string& getTitle() const { return title; }
    // const int getScore() const { return score; }
    // const std::string& getComment() const { return comment; }
    // const ItemType getType() const { return type; }

    // JSON serialization and deserialization
    nlohmann::json toJson() const;
    static Item fromJson(const nlohmann::json&);
};

}