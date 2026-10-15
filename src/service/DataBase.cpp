#include <fstream>
#include <iostream>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "core/LinkedList.h"
#include "core/Heap.h"
#include "core/Recommender.h"
#include "service/DataBase.h"

namespace metrace::service {

namespace {

bool byCreatedAt(const metrace::core::Item& a, const metrace::core::Item& b, bool desc)
{
    return desc ? a.getCreatedAt() > b.getCreatedAt() : a.getCreatedAt() < b.getCreatedAt();
}

bool byScore(const metrace::core::Item& a, const metrace::core::Item& b, bool desc)
{
    const bool a_unscored = a.getScore() == 0, b_unscored = b.getScore() == 0;
    if (a_unscored != b_unscored) return b_unscored; // 保证已评在未评前
    return desc ? a.getScore() > b.getScore() : a.getScore() < b.getScore();
}

bool byDate(const metrace::core::Item& a, const metrace::core::Item& b, bool desc)
{
    const int c = a.getDate().compare(b.getDate());
    return c != 0 && (desc ? c > 0 : c < 0);
}

bool byTitle(const metrace::core::Item& a, const metrace::core::Item& b, bool desc)
{
    const int c = a.getTitle().compare(b.getTitle());
    return c != 0 && (desc ? c > 0 : c < 0);
}

using FieldCompare = bool(*)(const metrace::core::Item& a, const metrace::core::Item& b, bool);
constexpr FieldCompare kFieldCompare[] = {byCreatedAt, byScore, byDate, byTitle};
static_assert(std::size(kFieldCompare) == 4, "更改 kSortField 时同步修改此处");

bool better(const metrace::core::Item& a, const metrace::core::Item& b, const metrace::service::SortField f, bool desc)
{
    const FieldCompare fc = kFieldCompare[static_cast<std::size_t>(f)];
    if (fc(a, b, desc)) return true;
    if (fc(b, a, desc)) return false;
    return a.getId() < b.getId();
}

struct StagedData {
    std::vector<metrace::core::Item> items;
    std::vector<metrace::core::Tag> tags;
    int next_id = 1;
};

} // namespace

DataBase::DataBase(const std::string& dbPath)
{
    if (!load(dbPath)) {
        throw std::runtime_error("Failed to load database file.");
    }
}

void DataBase::clearUnlocked()
{
    next_id = 1;
    items.clear();
    item_index.clear();
    tags.clear();
    db_path.clear();
}

void DataBase::clear()
{
    std::lock_guard<std::mutex> lock(mtx);
    return clearUnlocked();
}

/// @brief 从 dbPath 处的 json 文件加载数据库, json 文件约定: { "items": [...], "tags": [...], "next_id": <int> }
/// @param dbPath 
/// @return true if success, false if failed
bool DataBase::loadUnlocked(const std::string& dbPath)
{
    StagedData staged;

    // json 文件约定: { "items": [...], "tags": [...], "next_id": <int> }
    using json = nlohmann::json;
    std::ifstream file(dbPath);
    if (!file.is_open()) {
        if (!std::filesystem::exists(dbPath)) {
            std::cerr << "[DataBase] Warning: Database file doesn't exist, starting from empty." << std::endl;
            clearUnlocked();
            db_path = dbPath;
            return true;
        }
        std::cerr << "[DataBase] Fatal: Can't open database file." << std::endl;
        return false;
    }
    try {
        json data = json::parse(file);
        int max_id = 0;
        if (!(data.contains("items") && data.contains("tags"))) {
            std::cerr << "[DataBase] Fatal: Incomplete database file, missing either items or tags." << std::endl;
            return false;
        }
        if (!(data["items"].is_array() && data["tags"].is_array())) {
            std::cerr << "[DataBase] Fatal: Invalid value of items/tags, must be an array." << std::endl;
            return false;
        }
        for (const auto& itemJson : data["tags"]) {
            staged.tags.push_back(itemJson);
        }

        std::unordered_set<int> seen;
        seen.reserve(data["items"].size());
        for (const auto &itemJson : data["items"]) {
            metrace::core::Item item;

            try {
                item = metrace::core::Item::fromJson(itemJson);
            }
            catch (const nlohmann::json::exception& e) {
                std::cerr << "[DataBase] Fatal: malformed item: " << e.what() << std::endl;
                return false;
            }
            if (const auto why = metrace::core::validateItem(item)) {
                std::cerr << "[DataBase] Fatal: item " << item.id << " invalid " << why.value() << std::endl;
                return false;
            }
            if (item.id <= 0 || item.created_at <= 0 || item.updated_at < item.created_at) {
                std::cerr << "[DataBase] Fatal: item " << item.id << " has invalid id/timestamps" << std::endl;
                return false;
            }

            // 标签过滤放在校验之后
            std::vector<metrace::core::Tag> kept_tags;
            for (const auto& tag : item.getTags()) {
                if (std::find(staged.tags.begin(), staged.tags.end(), tag) == staged.tags.end()) {
                    std::cerr << "[DataBase] Warning: Tag " << tag << " in item " << item.getId()
                        << " not found in database, discarded." << std::endl;
                }
                else kept_tags.push_back(tag);
            }

            if (!seen.insert(item.getId()).second) {
                std::cerr << "[DataBase] Fatal: duplicate item id " << item.getId() << std::endl;
                return false;
            }

            item.setTags(kept_tags);
            staged.items.push_back(item);
            max_id = std::max(max_id, item.getId());
        }
        int data_id = data.value("next_id", 0);
        if (data_id < max_id + 1) {
            std::cerr << "[DataBase] Warning: \"next_id\" in database file is less or equal to some actual item id"
                << ", reallocated next_id." << std::endl;
            staged.next_id = max_id + 1;
        }
        else staged.next_id = data_id;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[DataBase] Fatal: Can't load database file in " << dbPath << " with exception " << e.what() << std::endl;
        return false;
    }

    clearUnlocked();
    for (const metrace::core::Item& item : staged.items) {
        metrace::core::Item* inserted = items.insert(item);
        item_index.insert(inserted->getId(), inserted);
    }
    for (const metrace::core::Tag& tag : staged.tags) {
        tags.insert(tag);
    }
    next_id = staged.next_id;
    db_path = dbPath;
    return true;
}

bool DataBase::load(const std::string& dbPath)
{
    std::lock_guard<std::mutex> lock(mtx);
    return loadUnlocked(dbPath);
}

/// @brief save DataBase to dbPath
/// @param dbPath 
/// @return true if success, false if failed
/// @note nodiscard http 层需要根据返回值判断是否返回 500
[[nodiscard]] bool DataBase::saveUnlocked(const std::string& dbPath) const
{
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    json data;
    data["next_id"] = next_id;
    data["items"] = json::array();
    items.forEach([&data] (const metrace::core::Item& item) { data["items"].push_back(item.toJson()); });
    data["tags"] = json::array();
    tags.forEach([&data] (const metrace::core::Tag& tag) { data["tags"].push_back(tag); });

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

[[nodiscard]] bool DataBase::save(const std::string& dbPath) const
{
    std::lock_guard<std::mutex> lock(mtx);
    return saveUnlocked(dbPath);
}

/// @brief 按照 id 获取条目，返回指针，若不存在返回 nullptr
/// @param id 
/// @return 一个指向条目的指针，若不存在返回 nullptr
/// @note 走 id 索引（HashTable），平均 O(1)；返回的指针只在锁内有效（§5.2）
const metrace::core::Item* DataBase::getItem(int id) const
{
    std::lock_guard<std::mutex> lock(mtx);
    metrace::core::Item* const* found = item_index.find(id);
    return found != nullptr ? *found : nullptr;
}

/// @brief 根据传入的 patch 更新条目
/// @param id 
/// @param patch 
/// @return WriteStatus 与指针的结构体，若持久化失败则回退至修改前
const ItemResult metrace::service::DataBase::updateItem(int id, const metrace::service::ItemPatch& patch)
{
    std::lock_guard<std::mutex> lock(mtx);
    metrace::core::Item** found = item_index.find(id);
    if (found == nullptr) {
        return {WriteStatus::NotFound, nullptr};
    }
    metrace::core::Item* item = *found;

    const metrace::core::Item before = *item;
    if (patch.type.has_value()) item->setType(patch.type.value());
    if (patch.title.has_value()) item->setTitle(patch.title.value());
    if (patch.author.has_value()) item->setAuthor(patch.author.value());
    if (patch.description.has_value()) item->setDescription(patch.description.value());
    if (patch.date.has_value()) item->setDate(patch.date.value());
    if (patch.progress.has_value()) item->setProgress(patch.progress.value());
    if (patch.score.has_value()) item->setScore(patch.score.value());
    if (patch.comment.has_value()) item->setComment(patch.comment.value());
    if (patch.tags.has_value()) item->setTags(patch.tags.value());
    item->touch();
    registerTags(item->getTags());

    if (!saveUnlocked(db_path)) {
        *item = before;
        return {WriteStatus::PersistFailed, nullptr};
    }

    return {WriteStatus::Ok, item};
}

/// @brief 根据 id 删除条目
/// @param id 
/// @return 是否成功删除
WriteStatus metrace::service::DataBase::removeItem(int id)
{
    std::lock_guard<std::mutex> lock(mtx);
    if (item_index.find(id) == nullptr) return WriteStatus::NotFound;
    const bool removed = items.remove([id](const metrace::core::Item& item) { return item.getId() == id; });
    if (!removed) return WriteStatus::NotFound; // 理论上不可达：索引与链表同锁同步
    item_index.erase(id);
    if (!saveUnlocked(db_path)) return WriteStatus::PersistFailed;
    return WriteStatus::Ok;
}

/// @brief 插入新条目，自动去重和注册 tag、分配 id、创建和更新时间
/// @param item 
/// @return 指向新条目的指针
const ItemResult DataBase::createItem(metrace::core::Item item)
{
    std::lock_guard<std::mutex> lock(mtx);
    item.id = next_id++;
    item.touch();
    item.created_at = item.updated_at;
    item.uniqueTags();
    registerTags(item.tags);
    metrace::core::Item* res = items.insert(item);
    item_index.insert(item.id, res);

    if (!saveUnlocked(db_path)) {
        items.remove(item);
        item_index.erase(item.id);
        return {WriteStatus::PersistFailed, nullptr};
    }
    return {WriteStatus::Ok, res};
}

/// @brief 插入新标签
/// @param tag 
/// @return 是否成功创建
WriteStatus DataBase::createTag(const metrace::core::Tag& tag)
{
    std::lock_guard<std::mutex> lock(mtx);
    bool res = tags.insert(tag);
    
    if (!res || saveUnlocked(db_path)) return WriteStatus::Ok;
    return WriteStatus::PersistFailed;
}

void DataBase::registerTags(const std::vector<metrace::core::Tag>& _tags)
{
    for (const auto& tag : _tags) {
        tags.insert(tag);
    }
}

/// @brief 删除标签
/// @details 删除标签时不会删除条目中引用的标签，条目中引用的标签会在加载数据库时被丢弃
/// @note 该函数不会检查条目中是否引用了该标签，调用者需要自行保证
/// @param tag 
/// @return 若标签已被删除或不存在返回 false
bool DataBase::removeTag(const metrace::core::Tag& tag)
{
    return tags.remove(tag);
}

/// @brief 按照 query 指定的规则查询 items 列表
/// @param query 
/// @return QueryResult 包含符合条件的 item 总数（用于计算总页数），以及本页的 items 列表
const ItemQueryResult DataBase::queryItem(const metrace::service::ItemQuery& query) const
{
    std::lock_guard<std::mutex> lock(mtx);

    ItemQueryResult query_result;
    query_result.total = 0;
    if (items.getSize() == 0) return query_result; // 空库不构造堆，会抛异常

    metrace::core::Heap<const metrace::core::Item*> heap(
        std::min<std::size_t>(query.offset + query.limit, items.getSize()),
        [&query](const metrace::core::Item* a, const metrace::core::Item* b) -> bool {
            return better(*b, *a, query.sort_field, query.descending);
        }
    );
    std::size_t total = 0;

    items.forEach([&heap, &query, &total](const metrace::core::Item& item) {
        if (query.type.has_value() && item.getType() != query.type.value()) {
            return;
        }
        if (query.tag.has_value() && !item.hasTag(query.tag.value())) {
            return;
        }
        if (query.keyword.has_value() &&
            item.getTitle().compare(0, query.keyword->size(), query.keyword.value()) != 0) {
            return;
        }
        ++total;
        heap.push(&item);
    });

    std::vector<const metrace::core::Item*> res;
    res.reserve(heap.getSize());
    while (!heap.empty()) {
        res.push_back(heap.top());
        heap.pop();
    }
    std::reverse(res.begin(), res.end());

    const std::size_t begin = std::min(query.offset, res.size());
    const std::size_t end = std::min(begin + query.limit, res.size());
    query_result.items.assign(res.begin() + begin, res.begin() + end);
    query_result.total = total;
    return query_result;
}

const TagQueryResult DataBase::queryTag(const TagQuery& query) const
{
    std::lock_guard<std::mutex> lock(mtx);

    TagQueryResult res;
    res.total = 0;
    res.tags.reserve(query.limit);
    tags.forEach([&](const std::string& tag) {
        ++res.total;
        if (res.total > query.offset && res.total - query.offset <= query.limit) {
            res.tags.push_back(tag);
        }
    });
    return res;
}

/// @brief 相似推荐：score = 3×共同标签数 + 2×同作者 + 1×同类型（§8.5）
/// @param query item_id 缺省 = 冷启动评分榜；top_n 已由 Router 截断
/// @return nullopt = 指定 item_id 不存在；空 vector = 合法空结果
std::optional<std::vector<const metrace::core::Item*>> DataBase::recommend(const RecommendQuery& query) const
{
    std::lock_guard<std::mutex> lock(mtx);

    const metrace::core::Item* target = nullptr;
    if (query.item_id.has_value()) {
        metrace::core::Item* const* found = item_index.find(query.item_id.value());
        if (found == nullptr) {
            return std::nullopt;
        }
        target = *found;
    }

    std::vector<const metrace::core::Item*> candidates;
    candidates.reserve(items.getSize());
    items.forEach([&candidates, target](const metrace::core::Item& item) {
        if (&item != target) { // 响应不含条目自身
            candidates.push_back(&item);
        }
    });

    return metrace::core::recommendTopN(candidates, target, query.top_n);
}

/// @brief 统计聚合：total（受 type 过滤）/ by_type（全库，按名有序）/ score_dist（受 type 过滤）
/// @param query type 缺省 = 全库
/// @note 单趟扫描 O(n)；评分桶：0、1-10、11-20、…、91-100（未评分 0 独立成桶）
const StatsResult DataBase::stats(const StatsQuery& query) const
{
    std::lock_guard<std::mutex> lock(mtx);

    static const std::string kDistLabels[11] = {
        "0", "1-10", "11-20", "21-30", "31-40", "41-50",
        "51-60", "61-70", "71-80", "81-90", "91-100"
    };

    StatsResult res;
    std::size_t dist[11] = {0};
    std::map<std::string, std::size_t> by_type;

    items.forEach([&](const metrace::core::Item& item) {
        ++by_type[item.getType()];
        if (query.type.has_value() && item.getType() != query.type.value()) {
            return;
        }
        ++res.total;
        const int score = item.getScore();
        const std::size_t bucket = score == 0 ? 0 : static_cast<std::size_t>((score - 1) / 10 + 1);
        ++dist[bucket];
    });

    res.by_type.assign(by_type.begin(), by_type.end());
    res.score_dist.reserve(11);
    for (std::size_t i = 0; i < 11; ++i) {
        res.score_dist.emplace_back(kDistLabels[i], dist[i]);
    }
    return res;
}

} // namespace metrace::service