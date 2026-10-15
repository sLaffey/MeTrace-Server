// tests/test_database.cpp — DataBase 服务层单测（CRUD / 查询 / 标签 / 推荐 / 原子加载 / 落盘）
// 用法：cmake --build build && ./build/bin/test_database（或 ctest --test-dir build）
// 说明：分层红利——db 方法不依赖 HTTP 即可单测；使用系统临时目录中的独立文件，跑完清理。

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Item.h"
#include "service/DataBase.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::cerr << "FAIL: " << what << std::endl;
        ++failures;
    }
}

std::string tempPath(const std::string& name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

metrace::core::Item makeCreate(const std::string& type, const std::string& title,
                               const std::string& author, const std::vector<std::string>& tags,
                               int score)
{
    nlohmann::json j;
    j["type"] = type;
    j["title"] = title;
    j["author"] = author;
    j["tags"] = tags;
    j["score"] = score;
    return metrace::core::Item::fromCreateJson(j);
}

/// @brief 写一个"结构合法但条目值域非法"的库文件（type=game），用于原子加载失败测试
void writeBadDb(const std::string& path, int id)
{
    nlohmann::json item;
    item["id"] = id;
    item["type"] = "game";
    item["title"] = "坏条目";
    item["author"] = "";
    item["description"] = "";
    item["date"] = "";
    item["progress"] = 0.0;
    item["score"] = 0;
    item["comment"] = "";
    item["tags"] = nlohmann::json::array();
    item["created_at"] = 1000;
    item["updated_at"] = 1000;

    nlohmann::json data;
    data["next_id"] = id + 1;
    data["tags"] = nlohmann::json::array();
    data["items"] = nlohmann::json::array({item});

    std::ofstream out(path);
    out << data.dump();
}

void testLoadMissingThenPersist(const std::string& path)
{
    std::filesystem::remove(path);

    metrace::service::DataBase db;
    check(db.load(path), "文件不存在时按空库启动");
    check(db.queryItem({}).total == 0, "空库 total == 0");

    const metrace::service::ItemResult created =
        db.createItem(makeCreate("book", "书A", "作者", {"tag2", "tag1", "tag1"}, 80));
    check(created.status == metrace::service::WriteStatus::Ok && created.item != nullptr, "createItem 成功");
    if (created.item != nullptr) {
        check(created.item->getId() == 1, "首个条目 id == 1");
        check(created.item->getTags() == std::vector<std::string>({"tag1", "tag2"}), "入库 tags 排序去重");
        const nlohmann::json j = created.item->toJson();
        check(j["created_at"].get<std::int64_t>() == j["updated_at"].get<std::int64_t>(), "创建时两时间戳一致");
        check(j["created_at"].get<std::int64_t>() > 0, "创建时间戳已盖章");
    }
    check(std::filesystem::exists(path), "写操作后落盘文件已生成");

    // 重新加载：数据从磁盘读回，tags 保留
    metrace::service::DataBase reloaded;
    check(reloaded.load(path), "重启加载成功");
    const metrace::core::Item* item = reloaded.getItem(1);
    check(item != nullptr && item->getTitle() == "书A", "重启后条目仍在");
    check(item != nullptr && item->getTags() == std::vector<std::string>({"tag1", "tag2"}), "重启后 tags 仍在");
}

void testCrudAndQuery(const std::string& path)
{
    std::filesystem::remove(path);
    metrace::service::DataBase db;
    check(db.load(path), "空库加载成功");

    db.createItem(makeCreate("book", "书1", "作者A", {"小说"}, 90));
    db.createItem(makeCreate("movie", "影1", "导演B", {"小说"}, 85));
    db.createItem(makeCreate("book", "书2", "作者A", {"科幻"}, 0));

    check(db.queryItem({}).total == 3, "全量 total == 3");

    metrace::service::ItemQuery by_type;
    by_type.type = "book";
    check(db.queryItem(by_type).total == 2, "type 过滤 total == 2");
    check(db.queryItem(by_type).items.size() == 2, "type 过滤返回 2 条");

    metrace::service::ItemQuery by_tag;
    by_tag.tag = "小说";
    check(db.queryItem(by_tag).total == 2, "tag 过滤 total == 2");

    metrace::service::ItemQuery paged;
    paged.limit = 2;
    paged.offset = 2;
    const metrace::service::ItemQueryResult page = db.queryItem(paged);
    check(page.total == 3 && page.items.size() == 1, "分页 total 不受影响、第二页 1 条");

    metrace::service::ItemQuery scored;
    scored.sort_field = metrace::service::SortField::Score;
    scored.descending = true;
    const metrace::service::ItemQueryResult ranked = db.queryItem(scored);
    check(ranked.items.size() == 3 && ranked.items[0]->getScore() == 90, "按评分降序榜首 90");
    check(ranked.items[2]->getScore() == 0, "未评分垫底");

    // updateItem：部分更新 + 未传字段保留；不存在 → NotFound
    metrace::service::ItemPatch patch;
    patch.title = "书1（改）";
    patch.score = 66;
    const metrace::service::ItemResult updated = db.updateItem(1, patch);
    check(updated.status == metrace::service::WriteStatus::Ok && updated.item != nullptr, "updateItem 成功");
    if (updated.item != nullptr) {
        check(updated.item->getTitle() == "书1（改）" && updated.item->getScore() == 66, "更新字段生效");
        check(updated.item->getAuthor() == "作者A", "未传字段保留原值");
    }
    check(db.updateItem(999, patch).status == metrace::service::WriteStatus::NotFound, "更新不存在 id → NotFound");

    // removeItem：删除、重复删除、id 不复用
    check(db.removeItem(2) == metrace::service::WriteStatus::Ok, "removeItem 成功");
    check(db.getItem(2) == nullptr, "删除后 getItem 为空");
    check(db.removeItem(2) == metrace::service::WriteStatus::NotFound, "重复删除 → NotFound");
    const metrace::service::ItemResult again = db.createItem(makeCreate("book", "书3", "", {}, 0));
    check(again.status == metrace::service::WriteStatus::Ok && again.item != nullptr && again.item->getId() == 4,
          "删除后 id 不复用（新 id == 4）");
}

void testTags(const std::string& path)
{
    std::filesystem::remove(path);
    metrace::service::DataBase db;
    check(db.load(path), "空库加载成功");

    check(db.createTag("科幻") == metrace::service::WriteStatus::Ok, "新标签创建成功");
    check(db.createTag("科幻") == metrace::service::WriteStatus::Ok, "同名标签幂等（仍 Ok）");

    metrace::service::TagQuery q;
    q.limit = 100;
    const metrace::service::TagQueryResult all = db.queryTag(q);
    check(all.total == 1 && all.tags == std::vector<std::string>({"科幻"}), "标签表去重且有序");
}

void testAtomicLoadFailure(const std::string& path, const std::string& bad_path)
{
    std::filesystem::remove(path);
    std::filesystem::remove(bad_path);

    metrace::service::DataBase db;
    check(db.load(path), "空库加载成功");
    db.createItem(makeCreate("book", "保底条目", "", {}, 0));

    writeBadDb(bad_path, 42);
    check(!db.load(bad_path), "非法库文件加载失败（fail-fast）");
    const metrace::core::Item* kept = db.getItem(1);
    check(kept != nullptr && kept->getTitle() == "保底条目", "加载失败不触碰现有数据（原子性）");

    const metrace::service::ItemResult created = db.createItem(makeCreate("book", "后续条目", "", {}, 0));
    check(created.status == metrace::service::WriteStatus::Ok, "加载失败后库仍可用");
    check(std::filesystem::exists(path), "后续写操作仍落盘到旧路径");
}

void testReplaceLoad(const std::string& path, const std::string& other_path)
{
    std::filesystem::remove(path);
    std::filesystem::remove(other_path);

    metrace::service::DataBase source;
    check(source.load(other_path), "源库加载成功");
    source.createItem(makeCreate("music", "歌1", "", {}, 70));
    source.createItem(makeCreate("music", "歌2", "", {}, 71));

    metrace::service::DataBase db;
    check(db.load(path), "空库加载成功");
    db.createItem(makeCreate("book", "旧条目", "", {}, 0));
    check(db.queryItem({}).total == 1, "替换前 1 条");

    check(db.load(other_path), "加载另一库成功");
    check(db.queryItem({}).total == 2, "加载替换为 2 条");
    const metrace::core::Item* first = db.getItem(1);
    check(first != nullptr && first->getTitle() == "歌1", "内容为替换后库的条目（旧数据已被替换）");
}

void testRecommend(const std::string& path)
{
    std::filesystem::remove(path);
    metrace::service::DataBase db;
    check(db.load(path), "空库加载成功");

    db.createItem(makeCreate("book", "目标", "作者A", {"科幻", "小说"}, 80));
    db.createItem(makeCreate("book", "最相似", "作者A", {"科幻", "小说"}, 60));
    db.createItem(makeCreate("movie", "次相似", "导演B", {"科幻"}, 90));

    metrace::service::RecommendQuery with_target;
    with_target.item_id = 1;
    with_target.top_n = 10;
    const std::optional<std::vector<const metrace::core::Item*>> ans = db.recommend(with_target);
    check(ans.has_value(), "指定存在的 item_id 返回结果");
    if (ans.has_value()) {
        check(ans->size() == 2, "推荐不含自身（2 条候选）");
        check(!ans->empty() && ans->front()->getId() == 2, "最相似（共同标签+同作者）排第一");
        bool has_self = false;
        for (const metrace::core::Item* item : ans.value()) {
            has_self = has_self || item->getId() == 1;
        }
        check(!has_self, "结果中不含目标自身");
    }

    metrace::service::RecommendQuery missing;
    missing.item_id = 999;
    check(!db.recommend(missing).has_value(), "不存在的 item_id → nullopt");

    metrace::service::RecommendQuery cold;
    cold.top_n = 2;
    const std::optional<std::vector<const metrace::core::Item*>> cold_ans = db.recommend(cold);
    check(cold_ans.has_value() && cold_ans->size() == 2, "冷启动按 topN 取高分榜");
    check(cold_ans.has_value() && !cold_ans->empty() && cold_ans->front()->getId() == 3, "冷启动榜首为最高分");
}

} // namespace

int main()
{
    const std::string path = tempPath("metrace_test_database.json");
    const std::string other_path = tempPath("metrace_test_database_b.json");
    const std::string bad_path = tempPath("metrace_test_database_bad.json");

    testLoadMissingThenPersist(path);
    testCrudAndQuery(path);
    testTags(path);
    testAtomicLoadFailure(path, bad_path);
    testReplaceLoad(path, other_path);
    testRecommend(path);

    std::filesystem::remove(path);
    std::filesystem::remove(other_path);
    std::filesystem::remove(bad_path);

    if (failures != 0) {
        std::cerr << "test_database: " << failures << " 项检查失败" << std::endl;
        return 1;
    }
    std::cout << "test_database: 全部检查通过" << std::endl;
    return 0;
}
