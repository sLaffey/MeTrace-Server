#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <optional>

#include "core/LinkedList.h"
#include "core/Item.h"
#include "core/Tag.h"
#include "core/Trie.h"

namespace metrace::service {

class DataBase {
    int next_id = 0;
    metrace::core::LinkedList<metrace::core::Item> items;
    metrace::core::LinkedList<metrace::core::Tag> tags;
    
    metrace::core::Trie trie;

public:
    DataBase() = default;
    DataBase(const DataBase&) = delete;
    ~DataBase() = default;

    DataBase(const std::string& dbPath);

    bool save(const std::string& dbPath) const;
    bool load(const std::string& dbPath);

    const metrace::core::Item& getItem(int id) const;
    bool updateItem(int id, const metrace::core::Item& patch);
    bool removeItem(int id);
    bool createItem(const metrace::core::Item& item);

    std::optional<metrace::core::Tag> createTag(const metrace::core::Tag& name);
};

} // namespace metrace::service