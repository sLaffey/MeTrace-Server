#include <nlohmann/json.hpp>

#include <string>

#include "core/LinkedList.h"
#include "core/Item.h"

namespace metrace::service {

class DataBase {
    metrace::core::LinkedList<metrace::core::Item> items;

public:
    DataBase() = default;
    ~DataBase() = default;

    DataBase(const std::string& dbPath);
};

} // namespace metrace::service