// tests/test_hashtable.cpp — HashTable 结构单测
// 用法：cmake --build build && ./build/bin/test_hashtable（或 ctest --test-dir build）
// 说明：普通 main + 轻量断言助手，不引测试框架；Release（NDEBUG）下同样有效，不依赖 assert。

#include <cstddef>
#include <iostream>
#include <string>
#include <type_traits>

#include "core/HashTable.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::cerr << "FAIL: " << what << std::endl;
        ++failures;
    }
}

void testBasic()
{
    metrace::core::HashTable<int> table;
    check(table.getSize() == 0, "空表 size == 0");
    check(table.find(1) == nullptr, "空表 find 未命中返回 nullptr");
    check(!table.erase(1), "空表 erase 未命中返回 false");

    table.insert(1, 100);
    table.insert(2, 200);
    table.insert(3, 300);
    check(table.getSize() == 3, "插入 3 条后 size == 3");
    check(table.find(1) != nullptr && *table.find(1) == 100, "find(1) == 100");
    check(table.find(2) != nullptr && *table.find(2) == 200, "find(2) == 200");
    check(table.find(3) != nullptr && *table.find(3) == 300, "find(3) == 300");
    check(table.find(4) == nullptr, "find(4) 未命中");

    table.insert(2, 999);
    check(table.getSize() == 3, "重复插入不增加 size");
    check(*table.find(2) == 999, "重复插入覆盖旧值");

    check(table.erase(2), "erase(2) 命中");
    check(!table.erase(2), "重复 erase(2) 返回 false");
    check(table.getSize() == 2, "erase 后 size == 2");
    check(table.find(2) == nullptr, "erase 后 find(2) 未命中");
    check(table.find(1) != nullptr && table.find(3) != nullptr, "erase 不影响同表其它键");
}

void testCollision()
{
    // 初始 17 个桶：0 与 17 落在同一个桶（经典链地址冲突用例）
    metrace::core::HashTable<int> table;
    table.insert(0, 10);
    table.insert(17, 170);
    table.insert(34, 340);
    check(table.getSize() == 3, "冲突键全部插入");
    check(table.find(0) != nullptr && *table.find(0) == 10, "冲突链上 find(0)");
    check(table.find(17) != nullptr && *table.find(17) == 170, "冲突链上 find(17)");
    check(table.find(34) != nullptr && *table.find(34) == 340, "冲突链上 find(34)");

    check(table.erase(17), "删除冲突链中间节点");
    check(table.find(0) != nullptr && *table.find(0) == 10, "删中间节点后 head 仍可查");
    check(table.find(34) != nullptr && *table.find(34) == 340, "删中间节点后尾部仍可查");
    check(table.find(17) == nullptr, "被删键不可查");
    check(table.getSize() == 2, "冲突删除后 size 正确");
}

void testGrowthAndClear()
{
    metrace::core::HashTable<int> table;
    const int total = 1000;
    for (int i = 0; i < total; ++i) {
        table.insert(i, i * 2);
    }
    check(table.getSize() == static_cast<std::size_t>(total), "跨多次扩容后 size == 1000");
    bool all_found = true;
    for (int i = 0; i < total; ++i) {
        int* value = table.find(i);
        if (value == nullptr || *value != i * 2) {
            all_found = false;
            break;
        }
    }
    check(all_found, "扩容后全部键可查且值正确");

    check(table.erase(0) && table.erase(500) && table.erase(999), "扩容后跨桶删除");
    check(table.getSize() == static_cast<std::size_t>(total - 3), "删除后 size 正确");
    check(table.find(0) == nullptr && table.find(500) == nullptr && table.find(999) == nullptr, "被删键不可查");
    check(table.find(1) != nullptr && table.find(998) != nullptr, "相邻键不受影响");

    table.clear();
    check(table.getSize() == 0, "clear 后 size == 0");
    check(table.find(1) == nullptr, "clear 后不可查");
    table.insert(7, 700);
    check(*table.find(7) == 700, "clear 后可继续使用");
}

void testValueSemantics()
{
    // 表不拥有值：销毁表不析构/释放 value；find 返回的是"表内存储的 value"的地址
    int a = 1, b = 2;
    metrace::core::HashTable<int*> table;
    table.insert(1, &a);
    table.insert(2, &b);

    int** slot = table.find(1);
    check(slot != nullptr && *slot == &a, "find 返回表内存储值的地址");
    *slot = &b;
    check(*table.find(1) == &b, "可通过 find 返回的指针修改存储的 value");

    {
        metrace::core::HashTable<int*> temp;
        temp.insert(1, &a);
    }
    check(a == 1 && b == 2, "临时表析构不影响值本体");

    const metrace::core::HashTable<int*>& const_ref = table;
    check(const_ref.find(1) != nullptr && *const_ref.find(1) == &b, "const 表可 find");
}

} // namespace

int main()
{
    static_assert(!std::is_copy_constructible_v<metrace::core::HashTable<int>>, "HashTable 应删除拷贝构造");
    static_assert(!std::is_copy_assignable_v<metrace::core::HashTable<int>>, "HashTable 应删除拷贝赋值");

    testBasic();
    testCollision();
    testGrowthAndClear();
    testValueSemantics();

    if (failures != 0) {
        std::cerr << "test_hashtable: " << failures << " 项检查失败" << std::endl;
        return 1;
    }
    std::cout << "test_hashtable: 全部检查通过" << std::endl;
    return 0;
}
