#include "service/ItemService.h"

#include <exception>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "db/ActionLogRepository.h"
#include "db/ItemRepository.h"
#include "db/TagRepository.h"
#include "service/ServiceError.h"

namespace metrace::service {
namespace {

using nlohmann::json;

/// 单个文本字段的长度上限（防止客户端写入超长内容）
constexpr std::size_t kTitleMaxLength = 200;
constexpr std::size_t kTagNameMaxLength = 50;
constexpr std::size_t kTagsMaxCount = 20;

/// 校验"被提供的字段"的取值；新增时的必填项由 validateForCreate 单独检查。
void validateProvidedFields(const db::ItemInput& input)
{
    // 这几个字段在数据模型里没有"空"的状态，传 null 属于非法输入而不是"清空"。
    // （不加这层校验的话，仓储层会退回默认值，等于悄悄把用户的数据改掉了。）
    if (input.type.provided && !input.type.value) {
        throw ValidationError("field 'type' cannot be null");
    }
    if (input.status.provided && !input.status.value) {
        throw ValidationError("field 'status' cannot be null");
    }
    if (input.progress.provided && !input.progress.value) {
        throw ValidationError("field 'progress' cannot be null");
    }

    if (input.title.provided &&
        (!input.title.value || input.title.value->empty() || input.title.value->size() > kTitleMaxLength)) {
        throw ValidationError("field 'title' must be a non-empty string of at most " +
                              std::to_string(kTitleMaxLength) + " characters");
    }
    if (input.year.provided && input.year.value && (*input.year.value < 0 || *input.year.value > 9999)) {
        throw ValidationError("field 'year' must be between 0 and 9999");
    }
    if (input.progress.provided && input.progress.value &&
        (*input.progress.value < 0.0 || *input.progress.value > 1.0)) {
        throw ValidationError("field 'progress' must be between 0 and 1");
    }
    if (input.score.provided && input.score.value &&
        (*input.score.value < 0.0 || *input.score.value > 10.0)) {
        throw ValidationError("field 'score' must be between 0 and 10");
    }
    if (input.tags.provided && input.tags.value && input.tags.value->size() > kTagsMaxCount) {
        throw ValidationError("too many tags, at most " + std::to_string(kTagsMaxCount) + " allowed");
    }
}

/// 新增接口的必填校验：type 与 title 必须有值。
void validateForCreate(const db::ItemInput& input)
{
    if (!input.type.provided || !input.type.value) {
        throw ValidationError("field 'type' is required and must be one of book/movie/music");
    }
    if (!input.title.provided || !input.title.value || input.title.value->empty()) {
        throw ValidationError("field 'title' is required and must be a non-empty string");
    }
}

/// 取一条必然存在的记录，取不到说明数据在两次调用之间被删了。
db::Item mustFind(std::int64_t id)
{
    const std::optional<db::Item> item = db::ItemRepository::findById(id);
    if (!item) {
        throw NotFoundError("item " + std::to_string(id) + " not found");
    }
    return *item;
}

/// 审计日志用的精简快照（完整记录也能存，但这里只留关键字段，便于人工查看）。
json snapshot(const db::Item& item)
{
    return json{
        {"id", item.id},
        {"type", db::toString(item.type)},
        {"title", item.title},
        {"status", db::toString(item.status)},
        {"progress", item.progress},
        {"score", item.score ? json(*item.score) : json(nullptr)},
        {"tags", item.tags},
    };
}

/// 写审计日志。日志属于旁路功能，写失败只打警告，绝不让主流程失败。
void logAction(const std::string& action, const json& payload)
{
    try {
        db::ActionLogRepository::append(action, payload.dump());
    } catch (const std::exception& e) {
        std::cerr << "warning: failed to append action_log(" << action << "): " << e.what()
                  << std::endl;
    }
}

} // namespace

db::Item ItemService::create(const db::ItemInput& input)
{
    validateForCreate(input);
    validateProvidedFields(input);

    const std::int64_t id = db::ItemRepository::create(input);
    const db::Item item = mustFind(id);

    logAction("create_item", json{{"id", id}, {"after", snapshot(item)}});
    return item;
}

std::optional<db::Item> ItemService::get(std::int64_t id)
{
    return db::ItemRepository::findById(id);
}

std::vector<db::Item> ItemService::list(const db::ItemQuery& query)
{
    return db::ItemRepository::list(query);
}

std::int64_t ItemService::count(const db::ItemQuery& query)
{
    return db::ItemRepository::count(query);
}

db::Item ItemService::update(std::int64_t id, const db::ItemInput& input)
{
    validateProvidedFields(input);

    const std::optional<db::Item> before = db::ItemRepository::findById(id);
    if (!before) {
        throw NotFoundError("item " + std::to_string(id) + " not found");
    }

    db::ItemRepository::update(id, input);

    const db::Item after = mustFind(id);
    logAction("update_item",
              json{{"id", id}, {"before", snapshot(*before)}, {"after", snapshot(after)}});
    return after;
}

void ItemService::remove(std::int64_t id)
{
    const std::optional<db::Item> before = db::ItemRepository::findById(id);
    if (!before) {
        throw NotFoundError("item " + std::to_string(id) + " not found");
    }

    db::ItemRepository::remove(id);

    logAction("delete_item", json{{"id", id}, {"before", snapshot(*before)}});
}

db::Tag ItemService::createTag(const std::string& name)
{
    if (name.empty() || name.size() > kTagNameMaxLength) {
        throw ValidationError("field 'name' must be a non-empty string of at most " +
                              std::to_string(kTagNameMaxLength) + " characters");
    }

    const db::Tag tag = db::TagRepository::createOrGet(name);
    logAction("create_tag", json{{"id", tag.id}, {"name", tag.name}});
    return tag;
}

std::vector<db::Tag> ItemService::listTags(int limit, int offset)
{
    return db::TagRepository::listAll(limit, offset);
}

std::int64_t ItemService::countTags()
{
    return db::TagRepository::countAll();
}

} // namespace metrace::service
