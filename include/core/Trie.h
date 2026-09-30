#include <string>

namespace metrace::core {

class Trie {

private:
    struct Node {
        bool is_word = false;
        int cnt_next = 0;
        Node *next[256] = {nullptr};

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

public:
    Trie() = default;
    ~Trie() = default;

    void insert(const std::string& s);
    void remove(const std::string& s);
    bool query(const std::string& s) const;
};

}