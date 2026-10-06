#pragma once

#include <string>
#include <functional>

namespace metrace::core {

class Trie {

private:
    struct Node {
        bool is_word = false;
        int cnt_next = 0;
        Node *next[256] = {nullptr};

        Node() = default;
        Node(const Node&) = delete; // 防止浅拷贝带来的问题

        ~Node() {
            for (Node* child : next) {
                if (child != nullptr) {
                    delete child;
                }
            }
        }

        bool empty() const {
            return !(is_word || cnt_next);
        }

        Node* operator [] (const unsigned char c) {
            if (next[c] != nullptr) {
                return next[c];
            }
            next[c] = new Node;
            ++cnt_next;
            return next[c];
        }
    };

    Node head;

    bool removeRec(Node* cur, const std::string& s, std::size_t depth);
    void forEachRec(const Node* cur, std::string& prefix, const std::function<void (const std::string&)>& func) const;

public:
    Trie() = default;
    ~Trie() = default;
    Trie(const Trie&) = delete; // 防止浅拷贝带来的问题
    Trie& operator = (const Trie&) = delete;

    bool insert(const std::string& s);
    bool remove(const std::string& s);
    bool find(const std::string& s) const;

    void forEach(const std::function<void (const std::string&)>& func) const;
};

}