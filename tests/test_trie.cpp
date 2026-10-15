// tests/test_trie.cpp — Trie 结构单测
// 用法：cmake --build build && ./build/bin/test_trie（或 ctest --test-dir build）
// 说明：普通 main + 轻量断言助手，不引测试框架；Release（NDEBUG）下同样有效。
//       重点覆盖 clear 的置空/复用（此前的悬垂指针与循环回绕缺陷在 HTTP 回归里够不到）。

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "core/Trie.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::cerr << "FAIL: " << what << std::endl;
        ++failures;
    }
}

std::vector<std::string> collect(const metrace::core::Trie& trie)
{
    std::vector<std::string> out;
    trie.forEach([&out](const std::string& s) { out.push_back(s); });
    return out;
}

void testInsertFind()
{
    metrace::core::Trie trie;
    check(!trie.find("a"), "空树 find 为假");
    check(trie.insert("a"), "新插入返回 true");
    check(!trie.insert("a"), "重复插入返回 false");
    check(trie.find("a"), "插入后可查");
    check(!trie.find("ab"), "未插入的词不可查");

    check(trie.insert("ab"), "插入前缀扩展词");
    check(trie.find("ab") && trie.find("a"), "前缀词与扩展词可共存");
    check(trie.insert("abc"), "插入更长词");
    check(trie.find("abc") && trie.find("ab") && trie.find("a"), "三层共存");
}

void testRemove()
{
    metrace::core::Trie trie;
    trie.insert("ab");
    trie.insert("abc");
    trie.insert("abd");

    check(!trie.remove("xyz"), "删除不存在的词返回 false");

    check(trie.remove("abc"), "删除叶子词返回 true");
    check(!trie.find("abc"), "被删词不可查");
    check(trie.find("ab") && trie.find("abd"), "同前缀的其它词保留");
    check(!trie.remove("abc"), "重复删除返回 false");

    check(trie.remove("ab"), "删除中间词返回 true");
    check(!trie.find("ab"), "中间词被删后不可查（前缀失效）");
    check(trie.find("abd"), "扩展词仍可查");

    check(trie.remove("abd"), "删除最后一个词返回 true");
    check(collect(trie).empty(), "全部删除后遍历为空");
    check(trie.insert("abd"), "删空后可重新插入");
    check(trie.find("abd"), "重新插入的词可查");
}

void testForEachOrder()
{
    metrace::core::Trie trie;
    trie.insert("b");
    trie.insert("a");
    trie.insert("ab");
    trie.insert("aa");
    trie.insert("中国");
    trie.insert("科幻");

    const std::vector<std::string> got = collect(trie);
    const std::vector<std::string> expected = {"a", "aa", "ab", "b", "中国", "科幻"};
    check(got == expected, "forEach 按 UTF-8 字节序（即字典序）输出");
}

void testClearAndReuse()
{
    metrace::core::Trie trie;
    check(collect(trie).empty(), "空树遍历为空");

    trie.insert("科幻");
    trie.insert("小说");
    trie.clear();
    check(!trie.find("科幻") && !trie.find("小说"), "clear 后不可查");
    check(collect(trie).empty(), "clear 后遍历为空");

    // clear 之后必须能安全地重新插入（回归：悬垂指针 / 循环下标回绕）
    check(trie.insert("科幻"), "clear 后可重新插入");
    check(trie.find("科幻"), "重新插入的词可查");
    check(collect(trie) == std::vector<std::string>({"科幻"}), "重新插入后遍历正确");

    trie.clear();
    trie.clear();
    check(collect(trie).empty(), "重复 clear 安全");
}

void testLongKey()
{
    const std::string key(50, 'a'); // 标签长度上限 50 字节
    metrace::core::Trie trie;
    trie.insert(key);
    check(trie.find(key), "50 字节键可插可查");
    check(trie.remove(key), "50 字节键可删");
    check(!trie.find(key), "删除后不可查");
    check(trie.insert(key), "删除后可重插（链清理正确）");
    check(trie.find(key), "重插后可查");
}

} // namespace

int main()
{
    testInsertFind();
    testRemove();
    testForEachOrder();
    testClearAndReuse();
    testLongKey();

    if (failures != 0) {
        std::cerr << "test_trie: " << failures << " 项检查失败" << std::endl;
        return 1;
    }
    std::cout << "test_trie: 全部检查通过" << std::endl;
    return 0;
}
