// tests/test_heap.cpp — Heap（固定容量淘汰堆）结构单测
// 用法：cmake --build build && ./build/bin/test_heap（或 ctest --test-dir build）
// 说明：普通 main + 轻量断言助手，不引测试框架；Release（NDEBUG）下同样有效。
//       语义：comp 为标准 less-than 序；堆保留"按 comp 最大"的 k 个元素，堆顶 = 保留集中最小者。

#include <cstddef>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "core/Heap.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::cerr << "FAIL: " << what << std::endl;
        ++failures;
    }
}

void testKeepLargest()
{
    // comp = less：保留最大的 3 个；堆顶 = 其中最小者；弹出序升序
    metrace::core::Heap<int> heap(3, std::less<int>());
    for (int i = 1; i <= 10; ++i) {
        const bool accepted = heap.push(i);
        check(accepted, "未满/更优时 push 返回 true（i=" + std::to_string(i) + "）");
    }
    check(heap.getSize() == 3, "满员后 size 固定为 3");
    check(heap.top() == 8, "堆顶 = 保留集中最小者（8）");

    std::vector<int> popped;
    while (!heap.empty()) {
        popped.push_back(heap.pop());
    }
    check(popped == std::vector<int>({8, 9, 10}), "保留最大 3 个，弹出序 8,9,10");
    check(heap.getSize() == 0, "弹出后 size == 0");
}

void testKeepSmallest()
{
    // comp = greater：保留最小的 3 个；堆顶 = 其中最大者；弹出序降序
    metrace::core::Heap<int> heap(3, std::greater<int>());
    for (int i = 10; i >= 1; --i) {
        heap.push(i);
    }
    check(heap.getSize() == 3, "满员后 size 固定为 3");
    check(heap.top() == 3, "堆顶 = 保留集中最大者（3）");

    std::vector<int> popped;
    while (!heap.empty()) {
        popped.push_back(heap.pop());
    }
    check(popped == std::vector<int>({3, 2, 1}), "保留最小 3 个，弹出序 3,2,1");
}

void testPushFullRejects()
{
    metrace::core::Heap<int> heap(2, std::less<int>());
    heap.push(5);
    heap.push(6);
    check(!heap.push(4), "满员且新元素不优于堆顶时 push 返回 false");
    check(heap.getSize() == 2 && heap.top() == 5, "被拒后堆内容不变");

    check(heap.push(7), "满员但新元素更优时顶掉堆顶返回 true");
    check(heap.top() == 6 && heap.getSize() == 2, "顶掉后堆顶更新为 6");
}

struct Candidate {
    int score;
    int id;
};

// 全序比较器（queryItem 的模式）：分数高者优；同分 id 小者优；
// comp(a, b) = "b 比 a 更优"（即 a 更差、更靠堆顶）
bool better(const Candidate& a, const Candidate& b)
{
    if (a.score != b.score) return a.score > b.score;
    return a.id < b.id;
}

// Heap 需要的是"更差者靠前"：直接给函数指针，避免 captureless lambda 触发 GCC 误报
bool worseFirst(const Candidate& a, const Candidate& b)
{
    return better(b, a);
}

void testTotalOrderTieBreak()
{
    metrace::core::Heap<Candidate> heap(3, worseFirst);

    const Candidate c1{5, 2};
    const Candidate c2{5, 1};
    const Candidate c3{5, 3};
    const Candidate c4{6, 9};
    heap.push(c1);
    heap.push(c2);
    heap.push(c3);
    heap.push(c4);

    check(heap.getSize() == 3, "容量 3 保留 3 个候选");
    check(heap.top().score == 5 && heap.top().id == 2, "堆顶 = 保留集中最差者（同分 id 大）；c3 被淘汰");
    check(heap.getSize() == 3, "堆顶未变化时 size 不变");

    std::vector<int> ids;
    while (!heap.empty()) {
        ids.push_back(heap.pop().id);
    }
    // 最差→最优：{5,2} → {5,1} → {6,9}；{5,3} 被淘汰
    check(ids == std::vector<int>({2, 1, 9}), "全序弹出：id 序列 2,1,9（c3 被淘汰）");
}

void testErrors()
{
    metrace::core::Heap<int> heap(2, std::less<int>());
    bool threw = false;
    try {
        heap.pop();
    } catch (const std::out_of_range&) {
        threw = true;
    }
    check(threw, "空堆 pop 抛 std::out_of_range");

    threw = false;
    try {
        metrace::core::Heap<int> zero(0, std::less<int>());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "容量 0 构造抛 std::invalid_argument");
}

} // namespace

int main()
{
    static_assert(!std::is_copy_constructible_v<metrace::core::Heap<int>>, "Heap 应删除拷贝构造");
    static_assert(!std::is_copy_assignable_v<metrace::core::Heap<int>>, "Heap 应删除拷贝赋值");

    testKeepLargest();
    testKeepSmallest();
    testPushFullRejects();
    testTotalOrderTieBreak();
    testErrors();

    if (failures != 0) {
        std::cerr << "test_heap: " << failures << " 项检查失败" << std::endl;
        return 1;
    }
    std::cout << "test_heap: 全部检查通过" << std::endl;
    return 0;
}
