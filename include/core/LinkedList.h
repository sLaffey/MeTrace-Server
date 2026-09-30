
namespace metrace::core {

template <typename T>
class LinkedList {
private:
    struct Node {
        T data;
        Node* next = nullptr;
        Node(const T& value) : data(value), next(nullptr) {}
    };

    Node *head;
public:
    LinkedList() : head(nullptr) {}
    ~LinkedList() {
        Node* cur = head;
        while (cur) {
            Node *next = cur->next;
            delete cur;
            cur = next;
        }
    }

    void insert(const T& value)
    {
        // TODO
    }

    void remove(const T& value)
    {
        // TODO
    }
};

} // namespace metrace::core