// tests/test_linkedlist.cpp — LinkedList 结构单测
// 用法：cmake --build build && ./build/bin/test_linkedlist（或 ctest --test-dir build）
// 说明：普通 main + 轻量断言助手，不引测试框架；Release（NDEBUG）下同样有效。

#include <cstddef>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "core/LinkedList.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::cerr << "FAIL: " << what << std::endl;
        ++failures;
    }
}

void testEmpty()
{
    metrace::core::LinkedList<int> list;
    check(list.getSize() == 0, "空表 size == 0");
    check(list.find(1) == nullptr, "空表 find 返回 nullptr");
    check(!list.remove(1), "空表按值 remove 返回 false");
    check(!list.remove([](const int&) { return true; }), "空表按谓词 remove 返回 false");

    int visited = 0;
    list.forEach([&visited](const int&) { ++visited; });
    check(visited == 0, "空表 forEach 不访问任何元素");
}

void testInsertAndFind()
{
    metrace::core::LinkedList<int> list;
    const int* p1 = list.insert(1);
    const int* p2 = list.insert(2);
    const int* p3 = list.insert(3);
    check(list.getSize() == 3, "插入 3 条 size == 3");
    check(p1 != nullptr && *p1 == 1 && *p2 == 2 && *p3 == 3, "insert 返回指向新元素的指针");

    std::vector<int> order;
    list.forEach([&order](const int& v) { order.push_back(v); });
    check(order == std::vector<int>({3, 2, 1}), "头插遍历序 = 3,2,1（新→旧）");

    const int* found = list.find(2);
    check(found != nullptr && *found == 2, "find(value) 命中");
    check(list.find(9) == nullptr, "find(value) 未命中");
}

void testFindPredicates()
{
    metrace::core::LinkedList<int> list;
    list.insert(10);
    list.insert(20);
    list.insert(30);

    const metrace::core::LinkedList<int>& const_list = list;
    const int* dividend = const_list.find([](const int& v) { return v % 20 == 0; });
    check(dividend != nullptr && *dividend == 20, "const 谓词 find 命中（20）");

    // 可变谓词版：可就地改值（PUT 路径的模型）
    int* mut = list.find([](const int& v) { return v == 20; });
    check(mut != nullptr, "可变谓词 find 命中");
    if (mut != nullptr) {
        *mut = 99;
    }
    check(list.find(99) != nullptr && list.find(20) == nullptr, "可变谓词版可就地改值");
}

void testRemove()
{
    metrace::core::LinkedList<int> list;
    list.insert(1);
    list.insert(2);
    list.insert(3);

    check(list.remove(2), "按值删除中间节点");
    check(list.getSize() == 2, "删除后 size == 2");
    check(list.find(2) == nullptr, "被删元素不可查");
    check(list.find(1) != nullptr && list.find(3) != nullptr, "其余元素保留");

    check(list.remove(3), "按值删除头节点");
    check(list.find(3) == nullptr, "头节点删除后不可查");
    check(list.getSize() == 1, "头节点删除后 size == 1");

    check(!list.remove(42), "删除不存在元素返回 false");

    check(list.remove([](const int& v) { return v == 1; }), "按谓词删除");
    check(list.getSize() == 0, "按谓词删除后 size == 0");
    check(!list.remove([](const int&) { return true; }), "空表按谓词删除返回 false");
}

void testClearAndReuse()
{
    metrace::core::LinkedList<std::string> list;
    list.insert("a");
    list.insert("b");
    check(list.getSize() == 2, "插入后 size == 2");

    list.clear();
    check(list.getSize() == 0 && list.find("a") == nullptr, "clear 后为空");
    list.clear();
    check(list.getSize() == 0, "重复 clear 安全");

    list.insert("c");
    check(list.getSize() == 1 && list.find("c") != nullptr, "clear 后可复用");
}

void testPointerStability()
{
    metrace::core::LinkedList<int> list;
    const int* first = list.insert(1);
    for (int i = 2; i <= 100; ++i) {
        list.insert(i);
    }
    check(*first == 1, "后续插入不影响先返回的指针（节点不搬移）");
}

} // namespace

int main()
{
    static_assert(!std::is_copy_constructible_v<metrace::core::LinkedList<int>>, "LinkedList 应删除拷贝构造");
    static_assert(!std::is_copy_assignable_v<metrace::core::LinkedList<int>>, "LinkedList 应删除拷贝赋值");

    testEmpty();
    testInsertAndFind();
    testFindPredicates();
    testRemove();
    testClearAndReuse();
    testPointerStability();

    if (failures != 0) {
        std::cerr << "test_linkedlist: " << failures << " 项检查失败" << std::endl;
        return 1;
    }
    std::cout << "test_linkedlist: 全部检查通过" << std::endl;
    return 0;
}
