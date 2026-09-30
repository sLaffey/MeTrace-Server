#include <nlohmann/json.hpp>

#include <string>

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
    ~DataBase() = default;

    DataBase(const std::string& dbPath);

    bool save(const std::string& dbPath) const;
    bool load(const std::string& dbPath);
};

} // namespace metrace::service