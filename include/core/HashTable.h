#pragma once

#include <cstddef>
#include <iterator>

namespace metrace::core {

/// @brief 链地址法哈希表：键固定 int，值类型由模板参数给出；桶数取素数，负载因子 > 0.75 时扩容（摘下重挂）
/// @tparam V 值类型
/// @note 平均查找/插入/删除 O(1)，最坏 O(n)；单次扩容 O(n) 摊还到每次插入为 O(1)
template <typename V>
class HashTable {
private:
    struct Entry {
        int key;
        V value;
        Entry* next = nullptr;

        Entry(int k, const V& v) : key(k), value(v), next(nullptr) {}
    };

    Entry** buckets;
    std::size_t bucket_count;
    std::size_t size;

    // 约 2 倍的素数序列；到最后一档后不再扩容
    static constexpr std::size_t kBucketSizes[] = {
        17, 37, 79, 163, 331, 673, 1361, 2729, 5471,
        10949, 21911, 43853, 87719, 175447, 350899
    };
    std::size_t bucket_step;

    /// @brief 散列函数：把 key 映射到 [0, count)
    static std::size_t bucketIndex(int key, std::size_t count)
    {
        return static_cast<std::size_t>(key) % count;
    }

    /// @brief 扩容到 new_count 个桶
    void rehash(std::size_t new_count);

public:
    HashTable();
    ~HashTable();

    HashTable(const HashTable&) = delete;             // 禁止浅拷贝
    HashTable& operator = (const HashTable&) = delete;

    /// @brief 查找 key，返回指向表内 value 的指针；不存在返回 nullptr
    V* find(int key) const;

    /// @brief 插入键值对；key 已存在则覆盖旧值，唯一性由调用方保证
    void insert(int key, const V& value);

    /// @brief 删除 key；找到并删除返回 true，不存在返回 false
    bool erase(int key);

    /// @brief 清空全部条目
    void clear();

    std::size_t getSize() const { return size; }
};

template <typename V>
HashTable<V>::HashTable()
    : buckets(new Entry*[kBucketSizes[0]]()), bucket_count(kBucketSizes[0]), size(0), bucket_step(0)
{
}

template <typename V>
HashTable<V>::~HashTable()
{
    clear();
    delete[] buckets;
}

template <typename V>
V* HashTable<V>::find(int key) const
{
    for (Entry* cur = buckets[bucketIndex(key, bucket_count)]; cur != nullptr; cur = cur->next) {
        if (cur->key == key) {
            return &cur->value;
        }
    }
    return nullptr;
}

template <typename V>
void HashTable<V>::insert(int key, const V& value)
{
    const std::size_t index = bucketIndex(key, bucket_count);
    for (Entry* cur = buckets[index]; cur != nullptr; cur = cur->next) {
        if (cur->key == key) {
            cur->value = value; // 已存在则覆盖
            return;
        }
    }

    Entry* entry = new Entry(key, value);
    entry->next = buckets[index];
    buckets[index] = entry;
    ++size;

    // 负载因子 > 0.75
    if (size * 4 > bucket_count * 3 && bucket_step + 1 < std::size(kBucketSizes)) {
        ++bucket_step;
        rehash(kBucketSizes[bucket_step]);
    }
}

template <typename V>
bool HashTable<V>::erase(int key)
{
    const std::size_t index = bucketIndex(key, bucket_count);
    Entry* cur = buckets[index];
    Entry* prev = nullptr;
    while (cur != nullptr) {
        if (cur->key == key) {
            if (prev == nullptr) {
                buckets[index] = cur->next;
            }
            else {
                prev->next = cur->next;
            }
            delete cur;
            --size;
            return true;
        }
        prev = cur;
        cur = cur->next;
    }
    return false;
}

template <typename V>
void HashTable<V>::clear()
{
    for (std::size_t i = 0; i < bucket_count; ++i) {
        Entry* cur = buckets[i];
        while (cur != nullptr) {
            Entry* next = cur->next;
            delete cur;
            cur = next;
        }
        buckets[i] = nullptr;
    }
    size = 0;
}

template <typename V>
void HashTable<V>::rehash(std::size_t new_count)
{
    Entry** fresh = new Entry*[new_count]();
    for (std::size_t i = 0; i < bucket_count; ++i) {
        Entry* cur = buckets[i];
        while (cur != nullptr) {
            Entry* next = cur->next;
            const std::size_t index = bucketIndex(cur->key, new_count);
            cur->next = fresh[index];
            fresh[index] = cur;
            cur = next;
        }
    }
    delete[] buckets;
    buckets = fresh;
    bucket_count = new_count;
}

} // namespace metrace::core
