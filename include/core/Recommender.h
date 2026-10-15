#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "core/Heap.h"
#include "core/Item.h"

namespace metrace::core {

/// @brief 推荐候选项：相似度打分 + 条目指针
struct Recommendation {
    int score;
    const Item* item;
};

/// @brief 相似度 = 3×共同标签数 + 2×同作者 + 1×同类型
/// @details 共同标签用双指针在两条已排序去重的 tags 上求交
int similarityScore(const Item& target, const Item& other);

/// @brief 推荐顺序：相似度高到低；同分评分高到低；再同分按 id 从小到大
bool betterRecommendation(const Recommendation& a, const Recommendation& b);

/// @brief 取与 target 最相似的 topN 条（best -> worst）
/// @param candidates 候选条目；调用方保证不含 target 自身
/// @param target 目标条目；nullptr 表示 score 全 0，退化为全局评分榜
/// @param top_n 取多少条
/// @note 实现：淘汰堆 O(n log topN)
std::vector<const Item*> recommendTopN(const std::vector<const Item*>& candidates,
                                       const Item* target, std::size_t top_n);

inline int similarityScore(const Item& target, const Item& other)
{
    const std::vector<Tag>& a = target.getTags();
    const std::vector<Tag>& b = other.getTags();
    std::size_t i = 0, j = 0;
    int score = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] < b[j]) {
            ++i;
        }
        else if (b[j] < a[i]) {
            ++j;
        }
        else {
            score += 3; // 每个共同标签 +3
            ++i;
            ++j;
        }
    }

    const std::string& author = target.getAuthor();
    if (!author.empty() && author == other.getAuthor()) {
        score += 2;
    }
    if (target.getType() == other.getType()) {
        score += 1;
    }
    return score;
}

inline bool betterRecommendation(const Recommendation& a, const Recommendation& b)
{
    if (a.score != b.score) {
        return a.score > b.score;
    }

    const int rating_a = a.item->getScore();
    const int rating_b = b.item->getScore();
    const bool a_unrated = rating_a == 0;
    const bool b_unrated = rating_b == 0;
    if (a_unrated != b_unrated) {
        return b_unrated; // 已评在前，未评最后
    }
    if (rating_a != rating_b) {
        return rating_a > rating_b;
    }
    return a.item->getId() < b.item->getId();
}

inline std::vector<const Item*> recommendTopN(const std::vector<const Item*>& candidates,
                                              const Item* target, std::size_t top_n)
{
    if (top_n == 0 || candidates.empty()) {
        return {};
    }

    Heap<Recommendation> heap(
        std::min(top_n, candidates.size()),
        [](const Recommendation& a, const Recommendation& b) {
            return betterRecommendation(b, a); // 堆顶 = 当前最差
        }
    );

    for (const Item* item : candidates) {
        const int score = target != nullptr ? similarityScore(*target, *item) : 0;
        heap.push({score, item});
    }

    std::vector<const Item*> result;
    result.reserve(heap.getSize());
    while (!heap.empty()) {
        result.push_back(heap.top().item);
        heap.pop();
    }
    std::reverse(result.begin(), result.end()); // 弹出序 = 最差在前
    return result;
}

} // namespace metrace::core
