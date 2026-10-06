#pragma once

#include <cstddef>
#include <functional>

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
    std::size_t size;

public:
    LinkedList() : head(nullptr), size(0) {}
    ~LinkedList() {
        Node* cur = head;
        while (cur) {
            Node *next = cur->next;
            delete cur;
            cur = next;
        }
    }
    LinkedList(const LinkedList&) = delete;

    std::size_t getSize() const { return size; }

    /// @brief 在链表头部插入一个值，会进行一次复制构造
    /// @param value 
    /// @return 指向新插入元素的指针
    const T* insert(const T& value)
    {
        ++size;
        Node* t = new Node(value);
        t->next = head;
        head = t;
        return &(t->data);
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
                --size;
                return true;
            }
            pre = cur;
            cur = cur->next;
        }
        return false;
    }

    /// @brief 遍历链表的所有元素，对其调用 func(item)
    /// @param func 
    void forEach(const std::function<void (const T&)>& func) const
    {
        for (Node* cur = head; cur != nullptr; cur = cur->next) {
            func(cur->data);
        }
    }

    /// @brief 根据 value 查找元素，返回第一个匹配的元素
    /// @param value 
    /// @return 第一个匹配元素的指针，不存在则为 nullptr
    const T* find(const T& value) const
    {
        for (Node* cur = head; cur != nullptr; cur = cur->next) {
            if (cur->data == value) {
                return &(cur->data);
            }
        }
        return nullptr;
    }

    /// @brief 常量版本，根据谓词查找元素，仅返回第一个匹配的元素，若不存在返回 nullptr
    /// @param predicate 
    /// @return 第一个匹配元素的指针，若不存在返回 nullptr
    const T* find(const std::function<bool (const T&)>& predicate) const
    {
        for (Node* cur = head; cur != nullptr; cur = cur->next) {
            if (predicate(cur->data)) {
                return &(cur->data);
            }
        }
        return nullptr;
    }

    /// @brief 可变版本，根据谓词查找元素，仅返回第一个匹配的元素，若不存在返回 nullptr
    /// @param predicate 
    /// @return 第一个匹配元素的指针，若不存在返回 nullptr
    T* find(const std::function<bool (const T&)>& predicate)
    {
        for (Node* cur = head; cur != nullptr; cur = cur->next) {
            if (predicate(cur->data)) {
                return &(cur->data);
            }
        }
        return nullptr;
    }

    /// @brief 根据谓词删除链表中第一个匹配的元素，若不存在返回 false
    /// @param predicate 
    /// @return 是否删除成功
    bool remove(const std::function<bool (const T&)>& predicate)
    {
        Node* cur = head, *pre = head;
        while (cur != nullptr) {
            if (predicate(cur->data)) {
                pre->next = cur->next;
                if (cur == head) head = cur->next;
                delete cur;
                --size;
                return true;
            }
            pre = cur;
            cur = cur->next;
        }
        return false;
    }
};

} // namespace metrace::core