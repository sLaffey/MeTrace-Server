// tests/test_item.cpp — Item 模型单测（序列化 / 校验谓词 / tags 归一化 / 判等）
// 用法：cmake --build build && ./build/bin/test_item（或 ctest --test-dir build）
// 说明：普通 main + 轻量断言助手，不引测试框架；Release（NDEBUG）下同样有效。

#include <cstddef>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Item.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::cerr << "FAIL: " << what << std::endl;
        ++failures;
    }
}

metrace::core::Item makeItem(int id, const std::string& type, const std::string& title,
                             const std::string& author, const std::vector<std::string>& tags,
                             int score, double progress = 0.0)
{
    nlohmann::json j;
    j["id"] = id;
    j["type"] = type;
    j["title"] = title;
    j["author"] = author;
    j["description"] = "";
    j["date"] = "";
    j["progress"] = progress;
    j["score"] = score;
    j["comment"] = "";
    j["tags"] = tags;
    j["created_at"] = 1000;
    j["updated_at"] = 1000;
    return metrace::core::Item::fromJson(j);
}

void testPredicates()
{
    check(metrace::core::checkType("uncategorized") && metrace::core::checkType("book") &&
          metrace::core::checkType("movie") && metrace::core::checkType("music"),
          "checkType 四个白名单取值合法");
    check(!metrace::core::checkType("game") && !metrace::core::checkType("null") &&
          !metrace::core::checkType(""),
          "checkType 拒绝旧枚举名/未知值/空串");

    check(!metrace::core::checkTitle(""), "checkTitle 拒绝空串");
    check(metrace::core::checkTitle(std::string(200, 'a')), "checkTitle 接受 200 字节");
    check(!metrace::core::checkTitle(std::string(201, 'a')), "checkTitle 拒绝 201 字节");

    check(metrace::core::checkProgress(0.0) && metrace::core::checkProgress(1.0), "checkProgress 接受 0 与 1");
    check(!metrace::core::checkProgress(-0.1) && !metrace::core::checkProgress(1.5), "checkProgress 拒绝越界");
    check(!metrace::core::checkProgress(std::numeric_limits<double>::quiet_NaN()), "checkProgress 拒绝 NaN");
    check(!metrace::core::checkProgress(std::numeric_limits<double>::infinity()), "checkProgress 拒绝 inf");

    check(metrace::core::checkScore(0) && metrace::core::checkScore(100), "checkScore 接受 0 与 100");
    check(!metrace::core::checkScore(-1) && !metrace::core::checkScore(101), "checkScore 拒绝越界");

    check(!metrace::core::checkTag(""), "checkTag 拒绝空串");
    check(metrace::core::checkTag(std::string(50, 'a')), "checkTag 接受 50 字节");
    check(!metrace::core::checkTag(std::string(51, 'a')), "checkTag 拒绝 51 字节");

    check(metrace::core::checkTags({}), "checkTags 接受空数组");
    check(metrace::core::checkTags(std::vector<std::string>(20, "t")), "checkTags 接受 20 个");
    check(!metrace::core::checkTags(std::vector<std::string>(21, "t")), "checkTags 拒绝 21 个");
    check(!metrace::core::checkTags({"ok", ""}), "checkTags 拒绝含空串元素");
}

void testValidateItem()
{
    const metrace::core::Item good = makeItem(1, "book", "三体", "刘慈欣", {"科幻"}, 95);
    check(!metrace::core::validateItem(good).has_value(), "合法条目通过 validateItem");

    check(metrace::core::validateItem(makeItem(1, "game", "x", "", {}, 0)).has_value(), "未知 type 违约");
    check(metrace::core::validateItem(makeItem(1, "book", "", "", {}, 0)).has_value(), "空 title 违约");
    check(metrace::core::validateItem(makeItem(1, "book", "x", "", {}, 101)).has_value(), "score 越界违约");
    check(metrace::core::validateItem(makeItem(1, "book", "x", "", {}, 0, 1.5)).has_value(), "progress 越界违约");
    check(metrace::core::validateItem(makeItem(1, "book", "x", "", {""}, 0)).has_value(), "空 tag 违约");
}

void testFromCreateJsonDefaults()
{
    nlohmann::json j;
    j["type"] = "music";
    j["title"] = "默认值条目";
    const metrace::core::Item item = metrace::core::Item::fromCreateJson(j);

    check(item.getType() == "music" && item.getTitle() == "默认值条目", "必填字段正确");
    check(item.getId() == 0, "id 由 DataBase 分配（默认 0）");
    check(item.getAuthor().empty() && item.getDescription().empty() && item.getDate().empty() &&
          item.getComment().empty(), "字符串可选字段默认为空");
    check(item.getProgress() == 0.0 && item.getScore() == 0, "progress/score 默认 0");
    check(item.getTags().empty(), "tags 默认空");
}

void testRoundTrip()
{
    nlohmann::json j;
    j["id"] = 7;
    j["type"] = "movie";
    j["title"] = "星际穿越";
    j["author"] = "诺兰";
    j["description"] = "科幻";
    j["date"] = "2014-11-07";
    j["progress"] = 0.5;
    j["score"] = 93;
    j["comment"] = "好看";
    j["tags"] = {"科幻", "经典"};
    j["created_at"] = 1000;
    j["updated_at"] = 2000;

    const metrace::core::Item item = metrace::core::Item::fromJson(j);
    check(item.toJson() == j, "fromJson → toJson 往返一致");

    bool threw = false;
    try {
        (void)metrace::core::Item::fromJson(nlohmann::json{{"id", 1}});
    } catch (const nlohmann::json::exception&) {
        threw = true;
    }
    check(threw, "缺字段的 json 抛 json::exception（交由 load 收编）");
}

void testTagsNormalization()
{
    metrace::core::Item item = makeItem(1, "book", "x", "", {}, 0);
    item.setTags({"b", "a", "b", "c", "a"});
    check(item.getTags() == std::vector<std::string>({"a", "b", "c"}), "setTags 排序 + 去重");
    check(item.hasTag("b") && !item.hasTag("z"), "hasTag 命中与未命中");
}

void testEqualityAndTouch()
{
    const metrace::core::Item a = makeItem(5, "book", "甲", "", {}, 0);
    const metrace::core::Item b = makeItem(5, "movie", "乙", "作者", {"x"}, 99);
    check(a == b, "operator== 按 id 判等（内容不同仍相等）");
    check(!(a == makeItem(6, "book", "甲", "", {}, 0)), "不同 id 不相等");

    metrace::core::Item item = makeItem(1, "book", "x", "", {}, 0);
    item.touch();
    const nlohmann::json j = item.toJson();
    check(j["updated_at"].get<std::int64_t>() > 0, "touch 刷新 updated_at");
    check(j["created_at"].get<std::int64_t>() == 1000, "touch 不影响 created_at");
}

} // namespace

int main()
{
    testPredicates();
    testValidateItem();
    testFromCreateJsonDefaults();
    testRoundTrip();
    testTagsNormalization();
    testEqualityAndTouch();

    if (failures != 0) {
        std::cerr << "test_item: " << failures << " 项检查失败" << std::endl;
        return 1;
    }
    std::cout << "test_item: 全部检查通过" << std::endl;
    return 0;
}
