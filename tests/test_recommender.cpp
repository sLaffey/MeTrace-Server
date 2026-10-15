// tests/test_recommender.cpp — Recommender（相似度 + 推荐排序 + TopN）结构单测
// 用法：cmake --build build && ./build/bin/test_recommender（或 ctest --test-dir build）
// 说明：普通 main + 轻量断言助手，不引测试框架；Release（NDEBUG）下同样有效。

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Item.h"
#include "core/Recommender.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::cerr << "FAIL: " << what << std::endl;
        ++failures;
    }
}

/// @brief 造一个合法 Item（走 fromJson，可显式指定 id/评分；tags 由 setTags 归一化）
metrace::core::Item makeItem(int id, int rating, const std::string& type,
                             const std::string& author, const std::vector<std::string>& tags)
{
    nlohmann::json j;
    j["id"] = id;
    j["type"] = type;
    j["title"] = "t";
    j["author"] = author;
    j["description"] = "";
    j["date"] = "";
    j["progress"] = 0.0;
    j["score"] = rating;
    j["comment"] = "";
    j["tags"] = tags;
    j["created_at"] = 1000;
    j["updated_at"] = 1000;

    metrace::core::Item item = metrace::core::Item::fromJson(j);
    item.setTags(tags); // fromJson 不归一化；setTags 负责排序去重
    return item;
}

void testSimilarity()
{
    const metrace::core::Item target = makeItem(100, 0, "book", "A", {"x", "y"});

    check(metrace::core::similarityScore(target, makeItem(1, 0, "book", "A", {"x", "y"})) == 9,
          "2 个共同标签 + 同作者 + 同类型 = 3×2+2+1");
    check(metrace::core::similarityScore(target, makeItem(2, 0, "book", "B", {"x"})) == 4,
          "1 个共同标签 + 同类型 = 3+1");
    check(metrace::core::similarityScore(target, makeItem(3, 0, "movie", "A", {})) == 2,
          "仅同作者 = 2");
    check(metrace::core::similarityScore(target, makeItem(4, 0, "book", "C", {})) == 1,
          "仅同类型 = 1");
    check(metrace::core::similarityScore(target, makeItem(5, 0, "music", "C", {"z"})) == 0,
          "无交集 = 0");

    const metrace::core::Item empty_author_a = makeItem(6, 0, "book", "", {});
    const metrace::core::Item empty_author_b = makeItem(7, 0, "book", "", {});
    check(metrace::core::similarityScore(empty_author_a, empty_author_b) == 1,
          "空作者不算同作者（只剩同类型 = 1）");

    const metrace::core::Item one_tag = makeItem(2, 0, "book", "B", {"x"});
    check(metrace::core::similarityScore(target, one_tag) == metrace::core::similarityScore(one_tag, target),
          "相似度对称");
}

void testRanking()
{
    const metrace::core::Item a = makeItem(1, 80, "book", "A", {});
    const metrace::core::Item b = makeItem(2, 90, "book", "A", {});
    const metrace::core::Item c = makeItem(3, 0, "book", "A", {});
    const metrace::core::Item d = makeItem(4, 0, "book", "A", {});

    const metrace::core::Recommendation r1{5, &a};
    const metrace::core::Recommendation r2{7, &b};
    check(metrace::core::betterRecommendation(r2, r1), "相似度高者优先");
    check(!metrace::core::betterRecommendation(r1, r2), "比较反对称");

    const metrace::core::Recommendation ra{5, &a}; // 评分 80
    const metrace::core::Recommendation rb{5, &b}; // 评分 90
    check(metrace::core::betterRecommendation(rb, ra), "同分评分高者优先");

    const metrace::core::Recommendation rc{5, &c}; // 未评分
    check(metrace::core::betterRecommendation(ra, rc), "同分已评 > 未评");
    check(metrace::core::betterRecommendation(rb, rc), "同分高评分 > 未评");

    const metrace::core::Recommendation rd{5, &d}; // 未评分 id=4
    check(metrace::core::betterRecommendation(rc, rd), "同分同未评按 id 升序");
}

void testRecommendTopN()
{
    // 目标：tags{x,y} / 作者 A / book；候选相似度递增
    const metrace::core::Item target = makeItem(100, 0, "book", "A", {"x", "y"});
    const metrace::core::Item c1 = makeItem(1, 0, "book", "A", {"x", "y"}); // 9
    const metrace::core::Item c2 = makeItem(2, 0, "book", "B", {"x"});      // 4
    const metrace::core::Item c3 = makeItem(3, 0, "movie", "A", {});        // 2
    const metrace::core::Item c4 = makeItem(4, 0, "music", "C", {"z"});     // 0

    const std::vector<const metrace::core::Item*> candidates = {&c1, &c2, &c3, &c4};

    const std::vector<const metrace::core::Item*> top3 =
        metrace::core::recommendTopN(candidates, &target, 3);
    check(top3.size() == 3, "topN=3 返回 3 条");
    check(top3.size() == 3 && top3[0]->getId() == 1 && top3[1]->getId() == 2 && top3[2]->getId() == 3,
          "按相似度降序取前 3");

    const std::vector<const metrace::core::Item*> all =
        metrace::core::recommendTopN(candidates, &target, 10);
    check(all.size() == 4, "topN 超过候选数时返回全部");
    check(all.size() == 4 && all[3]->getId() == 4, "最不相似排在最后");

    check(metrace::core::recommendTopN({}, &target, 3).empty(), "空候选返回空");
    check(metrace::core::recommendTopN(candidates, &target, 0).empty(), "topN=0 返回空");

    // 同相似度时按评分裁决
    const metrace::core::Item r1 = makeItem(10, 60, "book", "B", {"x"}); // 相似度 4
    const metrace::core::Item r2 = makeItem(11, 90, "book", "B", {"x"}); // 相似度 4
    const std::vector<const metrace::core::Item*> rated = {&r1, &r2};
    const std::vector<const metrace::core::Item*> ranked =
        metrace::core::recommendTopN(rated, &target, 2);
    check(ranked.size() == 2 && ranked[0]->getId() == 11 && ranked[1]->getId() == 10,
          "同相似度按评分降序");
}

void testColdStart()
{
    const metrace::core::Item a = makeItem(1, 90, "book", "A", {});
    const metrace::core::Item b = makeItem(2, 0, "book", "A", {});
    const metrace::core::Item c = makeItem(3, 50, "book", "A", {});

    const std::vector<const metrace::core::Item*> candidates = {&a, &b, &c};
    const std::vector<const metrace::core::Item*> ranked =
        metrace::core::recommendTopN(candidates, nullptr, 10);
    check(ranked.size() == 3, "冷启动返回全部候选");
    check(ranked.size() == 3 && ranked[0]->getId() == 1 && ranked[1]->getId() == 3 && ranked[2]->getId() == 2,
          "冷启动按评分降序、未评分最后");

    const std::vector<const metrace::core::Item*> top1 =
        metrace::core::recommendTopN(candidates, nullptr, 1);
    check(top1.size() == 1 && top1[0]->getId() == 1, "冷启动 topN=1 为最高分");
}

} // namespace

int main()
{
    testSimilarity();
    testRanking();
    testRecommendTopN();
    testColdStart();

    if (failures != 0) {
        std::cerr << "test_recommender: " << failures << " 项检查失败" << std::endl;
        return 1;
    }
    std::cout << "test_recommender: 全部检查通过" << std::endl;
    return 0;
}
