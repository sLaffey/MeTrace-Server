#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace metrace::db {

/// 条目类型：书 / 影 / 音（取值与 §5.1 的 CHECK 约束一一对应）
enum class ItemType { Book, Movie, Music };

/// 收藏状态
enum class ItemStatus { Wish, Doing, Done, Dropped };

/// 列表排序字段
enum class SortField { CreatedAt, Score, Year, Title };

/// "book" → ItemType::Book；非法取值返回 nullopt
inline std::optional<ItemType> parseItemType(const std::string& text)
{
    if (text == "book") {
        return ItemType::Book;
    }
    if (text == "movie") {
        return ItemType::Movie;
    }
    if (text == "music") {
        return ItemType::Music;
    }
    return std::nullopt;
}

/// "doing" → ItemStatus::Doing；非法取值返回 nullopt
inline std::optional<ItemStatus> parseItemStatus(const std::string& text)
{
    if (text == "wish") {
        return ItemStatus::Wish;
    }
    if (text == "doing") {
        return ItemStatus::Doing;
    }
    if (text == "done") {
        return ItemStatus::Done;
    }
    if (text == "dropped") {
        return ItemStatus::Dropped;
    }
    return std::nullopt;
}

/// "score" → (Score, 升序)；"-score" → (Score, 降序)。非法取值返回 nullopt
inline std::optional<std::pair<SortField, bool>> parseSort(const std::string& text)
{
    bool desc = false;
    std::string name = text;
    if (!name.empty() && name.front() == '-') {
        desc = true;
        name.erase(name.begin());
    }

    if (name == "created_at") {
        return std::make_pair(SortField::CreatedAt, desc);
    }
    if (name == "score") {
        return std::make_pair(SortField::Score, desc);
    }
    if (name == "year") {
        return std::make_pair(SortField::Year, desc);
    }
    if (name == "title") {
        return std::make_pair(SortField::Title, desc);
    }
    return std::nullopt;
}

inline const char* toString(ItemType type)
{
    switch (type) {
    case ItemType::Book:
        return "book";
    case ItemType::Movie:
        return "movie";
    case ItemType::Music:
        return "music";
    }
    return "book";
}

inline const char* toString(ItemStatus status)
{
    switch (status) {
    case ItemStatus::Wish:
        return "wish";
    case ItemStatus::Doing:
        return "doing";
    case ItemStatus::Done:
        return "done";
    case ItemStatus::Dropped:
        return "dropped";
    }
    return "wish";
}

/// 排序字段 → SQL 列名。
/// 这是**白名单映射**：SQL 里的 ORDER BY 只能来自这里，绝不把查询参数直接拼进语句。
inline const char* toColumn(SortField field)
{
    switch (field) {
    case SortField::CreatedAt:
        return "i.created_at";
    case SortField::Score:
        return "i.score";
    case SortField::Year:
        return "i.year";
    case SortField::Title:
        return "i.title";
    }
    return "i.created_at";
}

/// 三态字段：区分「未提供」「显式置空」「有具体值」。
/// PUT 是部分更新——只有 provided 为 true 的字段才会出现在 UPDATE 语句里；
/// provided && !value 表示把该列写成 NULL。
template <typename T>
struct Patch {
    bool provided = false;
    std::optional<T> value;

    /// 便捷构造：提供了具体值
    static Patch of(const T& v)
    {
        Patch patch;
        patch.provided = true;
        patch.value = v;
        return patch;
    }

    /// 便捷构造：显式置空
    static Patch clear()
    {
        Patch patch;
        patch.provided = true;
        return patch;
    }
};

/// 一个条目（书 / 影 / 音）
struct Item {
    std::int64_t id = 0;
    ItemType type = ItemType::Book;
    std::string title;
    std::string creator;                  // 作者 / 导演 / 艺术家
    int year = 0;                         // 0 表示未知
    std::string coverUrl;
    std::string description;
    ItemStatus status = ItemStatus::Wish;
    double progress = 0.0;                // 0~1
    std::optional<double> score;          // 未评分为 nullopt
    std::string review;
    std::vector<std::string> tags;
    std::string createdAt;                // 'YYYY-MM-DD HH:MM:SS'（UTC）
    std::string updatedAt;
};

/// HTTP 层解析请求体得到的输入（业务层再做必填校验）
struct ItemInput {
    Patch<ItemType> type;
    Patch<std::string> title;
    Patch<std::string> creator;
    Patch<int> year;
    Patch<std::string> coverUrl;
    Patch<std::string> description;
    Patch<ItemStatus> status;
    Patch<double> progress;
    Patch<double> score;
    Patch<std::string> review;
    Patch<std::vector<std::string>> tags;  // 提供即整体替换
};

/// 列表 / 统计的过滤与分页条件
struct ItemQuery {
    std::optional<ItemType> type;
    std::optional<ItemStatus> status;
    std::optional<std::string> tag;       // 标签名称（精确匹配）
    SortField sort = SortField::CreatedAt;
    bool desc = true;
    int limit = 20;
    int offset = 0;
};

/// 标签
struct Tag {
    std::int64_t id = 0;
    std::string name;
};

} // namespace metrace::db
