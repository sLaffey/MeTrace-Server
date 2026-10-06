#pragma once

#include <vector>
#include <string>
#include <optional>

#include "core/LinkedList.h"
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
    SortField sort_field = SortField::CreatedAt;
    bool descending = true;
    int limit = 20;
    int offset = 0;
};

struct ItemQueryResult {
    std::size_t total;
    std::vector<const metrace::core::Item*> items;
};

struct TagQuery {
    int limit = 20;
    int offset = 0;
};

struct TagQueryResult {
    std::size_t total;
    std::vector<metrace::core::Tag> tags;
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

class DataBase {
private:
    int next_id = 0;
    metrace::core::LinkedList<metrace::core::Item> items;
    metrace::core::Trie tags;

    void registerTags(const std::vector<metrace::core::Tag>& tags);
    bool removeTag(const metrace::core::Tag& tag);

public:
    DataBase() = default;
    DataBase(const DataBase&) = delete;
    ~DataBase() = default;

    DataBase(const std::string& dbPath);

    bool save(const std::string& dbPath) const;
    bool load(const std::string& dbPath);

    const metrace::core::Item* getItem(int id) const;
    const metrace::core::Item* updateItem(int id, const metrace::service::ItemPatch& patch);
    bool removeItem(int id);
    const metrace::core::Item* createItem(metrace::core::Item item);

    bool createTag(const metrace::core::Tag& tag);

    const ItemQueryResult queryItem(const metrace::service::ItemQuery& query) const;
    const TagQueryResult queryTag(const metrace::service::TagQuery& query) const;
};

} // namespace metrace::service