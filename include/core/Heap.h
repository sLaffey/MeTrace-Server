#pragma once

#include <vector>
#include <cstddef>
#include <functional>

namespace metrace::core {

/// @brief 固定容量二叉堆，采用顺序存储。comp(a, b) 的语义为 a < b，实现为小根堆。
/// @tparam T 
template <typename T>
class Heap {
private:
    T* data;
    std::size_t capacity;
    std::size_t size;
    std::function<bool(const T&, const T&)> comp;

    /// @brief 将 index 处的元素向上调整到合适位置
    /// @param index 
    void siftUp(std::size_t index) {
        while (index > 0) {
            std::size_t parent = (index - 1) / 2;
            if (comp(data[index], data[parent])) {
                std::swap(data[index], data[parent]);
                index = parent;
            }
            else break;
        }
    }

    /// @brief 将 index 处的元素向下调整到合适位置
    /// @param index 
    void siftDown(std::size_t index) {
        while (true) {
            std::size_t left = index * 2 + 1;
            std::size_t right = index * 2 + 2;
            std::size_t smallest = index;
            if (left < size && comp(data[left], data[smallest])) {
                smallest = left;
            }
            if (right < size && comp(data[right], data[smallest])) {
                smallest = right;
            }
            if (smallest != index) {
                std::swap(data[index], data[smallest]);
                index = smallest;
            }
            else break;
        }
    }

public:
    explicit Heap(std::size_t capacity, std::function<bool(const T&, const T&)> comp)
        : capacity(capacity), size(0), comp(comp) {
            data = new T[capacity]{};
        }
    
    Heap(const Heap&) = delete;
    Heap& operator = (const Heap&) = delete;

    ~Heap() {
        delete[] data;
    }

    /// @brief 尝试向堆中插入元素，若堆已满且插入元素不大于堆顶，则插入失败
    /// @param value 
    /// @return 插入失败返回 false，成功返回 true
    bool push(const T& value) {
        if (size < capacity) {
            data[size++] = value;
            siftUp(size - 1);
            return true;
        }
        if (comp(data[0], value)) { // value > data[0]，丢弃堆顶插入
            data[0] = value;
            siftDown(0);
            return true;
        }
        return false;
    }

    /// @brief 弹出堆顶元素
    /// @return 堆顶元素
    T pop() {
        if (size == 0) throw std::out_of_range("Heap is empty");
        T topValue = data[0];
        data[0] = data[--size];
        siftDown(0);
        return topValue;
    }

    const T& top() const {
        return data[0];
    }

    bool empty() const {
        return size == 0;
    }

    std::size_t getSize() const {
        return size;
    }
};

} // namespace metrace::core