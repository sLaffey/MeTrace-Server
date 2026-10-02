#include "core/Trie.h"

namespace metrace::core {

/// @brief 递归删除单词
/// @param cur 
/// @param s 
/// @param depth 
/// @return 若单词已不存在返回 false
bool Trie::removeRec(Node* cur, const std::string& s, std::size_t depth)
{
    if (depth == s.size()) {
        cur->is_word = false;
    }
    else {
        unsigned char c = s[depth];
        if (cur->next[c] == nullptr) return false;
        if (removeRec(cur->next[c], s, depth + 1)) {
            delete cur->next[c];
            cur->next[c] = nullptr;
            (cur->cnt_next)--;
        }
    }
    return cur->empty();
}

void Trie::forEachRec(const Node* cur, std::string& prefix, const std::function<void (const std::string&)>& func) const
{
    if (cur->is_word) {
        func(prefix);
    }
    for (int i = 0; i < 256; ++i) {
        if (cur->next[i] != nullptr) {
            prefix.push_back(char(i));
            forEachRec(cur->next[i], prefix, func);
            prefix.pop_back();
        }
    }
}

/// @brief 插入新单词
/// @param s 
/// @return 若单词已存在返回 false
bool Trie::insert(const std::string& s)
{
    Node *cur = &head;
    for (unsigned char c : s) {
        cur = (*cur)[c];
    }
    if (cur->is_word) {
        return false;
    }
    cur->is_word = true;
    return true;
}

bool Trie::remove(const std::string& s)
{
    return removeRec(&head, s, 0);
}

bool Trie::find(const std::string& s) const
{
    const Node* cur = &head;
    for (unsigned char c : s) {
        if (cur->next[c] != nullptr) {
            cur = cur->next[c];
        }
        else return false;
    }
    return cur->is_word;
}

void Trie::forEach(const std::function<void (const std::string&)>& func) const
{
    std::string prefix;
    forEachRec(&head, prefix, func);
}

} // namespace metrace::core