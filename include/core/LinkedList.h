
namespace metrace::core {

template <typename T>
class LinkedList {
private:
    struct Node {
        T data;
        Node* next = nullptr;
        Node(const T& value) : data(value), next(nullptr) {}
    };

    Node* head;
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

    /// @brief 在链表头部插入一个值
    /// @param value 
    void insert(const T& value)
    {
        Node* t = new Node(value);
        t->next = head;
        head = t;
    }

    /// @brief 删除值为 value 的第一个节点，value 唯一性由调用者保证
    /// @param value 
    /// @return true 为找到并删除成功，false 为不存在
    bool remove(const T& value)
    {
        Node* cur = head, *pre = head;
        while (cur != nullptr) {
            if (cur->data == value) {
                pre->next = cur->next;
                if (cur == head) head = cur->next;
                delete cur;
                return true;
            }
            pre = cur;
            cur = cur->next;
        }
        return false;
    }
};

} // namespace metrace::core