#pragma once

#include <vector>
#include <string>
#include <optional>
#include <mutex>
#include <utility>
#include <cstddef>

#include "core/LinkedList.h"
#include "core/HashTable.h"
#include "core/Item.h"
#include "core/Tag.h"
#include "core/Trie.h"

namespace metrace::service {

enum class SortField {
    CreatedAt,
    Score,
    Date,
    Title
};

struct ItemQuery {
    std::optional<std::string> type;
    std::optional<metrace::core::Tag> tag;
    std::optional<std::string> keyword; // 标题前缀过滤（/api/search 用），缺省不过滤
    SortField sort_field = SortField::CreatedAt;
    bool descending = true;
    std::size_t limit = 20;
    std::size_t offset = 0;
};

struct ItemQueryResult {
    std::size_t total;
    std::vector<const metrace::core::Item*> items;
};

struct TagQuery {
    std::size_t limit = 20;
    std::size_t offset = 0;
};

struct TagQueryResult {
    std::size_t total;
    std::vector<metrace::core::Tag> tags;
};

struct RecommendQuery {
    std::optional<int> item_id; // 缺省为全局评分榜
    std::size_t top_n = 10;     // Router 负责截断到 [1,50]
};

struct StatsQuery {
    std::optional<std::string> type; // 缺省 = 全库
};

struct StatsResult {
    std::size_t total = 0;                                       // 应用 type 过滤后的总数
    std::vector<std::pair<std::string, std::size_t>> by_type;    // 全库各类型计数（按名有序；仅含出现过的类型）
    std::vector<std::pair<std::string, std::size_t>> score_dist; // 过滤后的评分分布（桶：0,1-10,…,91-100）
};

struct ItemPatch {
    std::optional<std::string> type;
    std::optional<std::string> title;
    std::optional<std::string> author;
    std::optional<std::string> description;
    std::optional<std::string> date;
    std::optional<double> progress;
    std::optional<int> score;
    std::optional<std::string> comment;
    std::optional<std::vector<metrace::core::Tag>> tags;
};

enum class WriteStatus {
    Ok, NotFound, PersistFailed
};

struct ItemResult {
    WriteStatus status = WriteStatus::PersistFailed;
    const metrace::core::Item* item = nullptr;
};

class DataBase {
private:
    int next_id = 1;
    metrace::core::LinkedList<metrace::core::Item> items;
    metrace::core::HashTable<metrace::core::Item*> item_index; // id -> Item*, 与链表同步维护
    metrace::core::Trie tags;

    std::string db_path;

    mutable std::mutex mtx;

    void registerTags(const std::vector<metrace::core::Tag>& tags);
    bool removeTag(const metrace::core::Tag& tag);

    [[nodiscard]] bool saveUnlocked(const std::string& dbPath) const;
    bool loadUnlocked(const std::string& dbPath);
    void clearUnlocked();

public:
    DataBase() = default;
    DataBase(const DataBase&) = delete;
    ~DataBase() = default;

    explicit DataBase(const std::string& dbPath);

    void clear();

    [[nodiscard]] bool save(const std::string& dbPath) const;
    bool load(const std::string& dbPath);

    const metrace::core::Item* getItem(int id) const;

    const ItemResult updateItem(int id, const metrace::service::ItemPatch& patch);
    const ItemResult createItem(metrace::core::Item item);
    WriteStatus removeItem(int id);
    WriteStatus createTag(const metrace::core::Tag& tag);

    const ItemQueryResult queryItem(const metrace::service::ItemQuery& query) const;
    const TagQueryResult queryTag(const metrace::service::TagQuery& query) const;

    /// @brief 相似推荐
    /// @return nullopt 表示 item_id 指定但不存在；空 vector 表示合法空结果
    std::optional<std::vector<const metrace::core::Item*>> recommend(const RecommendQuery& query) const;

    /// @brief 统计聚合：total/by_type/score_dist
    const StatsResult stats(const StatsQuery& query) const;
};

} // namespace metrace::service