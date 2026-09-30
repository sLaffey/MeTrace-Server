#include "core/Trie.h"

namespace metrace::core {

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

void Trie::insert(const std::string& s)
{
    Node *cur = &head;
    for (unsigned char c : s) {
        cur = (*cur)[c];
    }
    cur->is_word = true;
}

void Trie::remove(const std::string& s)
{
    removeRec(&head, s, 0);
}

bool Trie::query(const std::string& s) const
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

} // namespace metrace::core