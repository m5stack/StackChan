
/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <vector>
#include <memory>
#include <functional>

namespace stackchan {

class Poolable {
public:
    virtual ~Poolable() = default;

    // 标记对象需要销毁。
    // 通常由对象自身调用（例如，在其 Update 方法内部）。
    void requestDestroy()
    {
        _destroy_requested = true;
    }

    bool isDestroyRequested() const
    {
        return _destroy_requested;
    }

private:
    bool _destroy_requested = false;
};

/**
 * @brief 通用对象池
 * 适用于不在乎最大容量，更关注对象自身生命周期管理的场景。
 *
 * @tparam T
 */
template <typename T>
class ObjectPool {
public:
    /**
     * @brief 创建一个新对象并返回其 ID。
     * 使用空闲列表策略实现 O(1) 插入。
     *
     * @param obj 对象实例的唯一指针
     * @return int 分配的对象 ID (索引)
     */
    int create(std::unique_ptr<T> obj)
    {
        // 1. 如果有空闲槽位，重用它
        if (!_free_indices.empty()) {
            int id = _free_indices.back();
            _free_indices.pop_back();
            _objs[id] = std::move(obj);
            return id;
        }

        // 2. 没有空闲槽位，追加到末尾
        _objs.push_back(std::move(obj));
        return (int)_objs.size() - 1;
    }

    /**
     * @brief 通过 ID 获取对象实例。
     *
     * @param id 对象 ID
     * @return T* 对象指针，如果无效或为空则返回 nullptr
     */
    T* get(int id)
    {
        if (id < 0 || id >= (int)_objs.size()) {
            return nullptr;
        }
        return static_cast<T*>(_objs[id].get());
    }

    /**
     * @brief 遍历所有活跃对象。
     *
     * @param func 接收 (Object*, ID) 的回调函数
     */
    void forEach(const std::function<void(T*, int)>& func)
    {
        for (int i = 0; i < (int)_objs.size(); ++i) {
            if (_objs[i]) {
                func(static_cast<T*>(_objs[i].get()), i);
            }
        }
    }

    /**
     * @brief Poolable 的主要适配方法：
     * 检查所有对象，看它们是否请求了销毁。
     * 应该每帧调用一次（例如，在游戏循环结束时）。
     */
    void cleanup()
    {
        for (int i = 0; i < (int)_objs.size(); ++i) {
            // 如果对象存在且请求了销毁
            if (_objs[i] && _objs[i]->isDestroyRequested()) {
                destroy(i);
            }
        }
    }

    /**
     * @brief 通过 ID 销毁对象并释放槽位。
     *
     * @param id 对象 ID
     * @return true 如果成功销毁，false 如果无效或已经为空
     */
    bool destroy(int id)
    {
        if (id < 0 || id >= (int)_objs.size()) {
            return false;
        }

        // 防止重复释放
        if (!_objs[id]) {
            return false;
        }

        // 释放内存
        _objs[id].reset();

        // 将此 ID 添加到空闲列表以便立即重用
        _free_indices.push_back(id);

        return true;
    }

    /**
     * @brief 清除所有对象并重置池。
     */
    void clear()
    {
        _objs.clear();
        _free_indices.clear();
    }

    /**
     * @brief 获取总容量（最大索引 + 1）。
     * 注意：这包括空槽位（空洞）。
     *
     * @return size_t
     */
    size_t size()
    {
        return _objs.size();
    }

    /**
     * @brief 获取实际活跃对象的数量。
     *
     * @return size_t
     */
    size_t activeCount()
    {
        return _objs.size() - _free_indices.size();
    }

private:
    std::vector<std::unique_ptr<Poolable>> _objs;
    std::vector<int> _free_indices;
};

/**
 * @brief 环形对象池
 * 适用于大量对象并限定数量的场景（定长）。
 * 当池满时，新对象会自动覆盖最旧的对象。
 *
 * @tparam T
 */
template <typename T>
class RingObjectPool {
public:
    /**
     * @brief
     * @param capacity 池子的最大容量
     */
    explicit RingObjectPool(size_t capacity) : _capacity(capacity), _cursor(0)
    {
        // 预先分配好所有槽位，全是 nullptr
        _objs.resize(capacity);
    }

    /**
     * @brief 创建/生成一个新对象。
     * 如果当前槽位已有对象，该旧对象会被立即析构（覆盖）。
     *
     * @param obj 新对象的唯一指针
     * @return int 分配的 ID (索引)
     */
    int create(std::unique_ptr<T> obj)
    {
        int id = _cursor;

        // 如果该位置已有对象，unique_ptr 的赋值操作会自动析构旧对象
        // 这一步实现了 "自动顶替最旧数据"
        _objs[id] = std::move(obj);

        // 移动游标，如果超过容量则回到 0
        _cursor = (_cursor + 1) % _capacity;

        return id;
    }

    /**
     * @brief 通过 ID 获取对象
     * 注意：在环形池中，如果你持有 ID 太久，该 ID 对应的内容可能已经被新对象覆盖。
     *
     * @param id 对象索引
     * @return T* 对象指针，如果是空槽位则返回 nullptr
     */
    T* get(int id)
    {
        if (id < 0 || id >= (int)_objs.size()) {
            return nullptr;
        }
        return _objs[id].get();
    }

    /**
     * @brief 遍历所有活跃的对象
     * 会自动跳过空槽位 (nullptr)
     */
    void forEach(const std::function<void(T*, int)>& func)
    {
        for (int i = 0; i < (int)_objs.size(); ++i) {
            if (_objs[i]) {
                func(_objs[i].get(), i);
            }
        }
    }

    /**
     * @brief 清理主动请求销毁的对象。
     * 针对那些还没轮到被覆盖，但逻辑上已经结束的对象（比如血量归零的敌人）。
     */
    void cleanup()
    {
        for (auto& ptr : _objs) {
            // 如果对象存在，且请求了销毁
            if (ptr && ptr->isDestroyRequested()) {
                ptr.reset();  // 释放内存，变为空槽位 (nullptr)
            }
        }
    }

    /**
     * @brief 获取总容量
     */
    size_t capacity() const
    {
        return _capacity;
    }

    /**
     * @brief 获取当前实际存活的对象数量
     */
    size_t activeCount() const
    {
        size_t count = 0;
        for (const auto& ptr : _objs) {
            if (ptr) count++;
        }
        return count;
    }

    /**
     * @brief 强制清空整个池子
     */
    void clear()
    {
        for (auto& ptr : _objs) {
            ptr.reset();
        }
        _cursor = 0;  // 重置游标
    }

private:
    std::vector<std::unique_ptr<T>> _objs;
    size_t _capacity;
    int _cursor;  // 当前写入位置的指针
};

}  // namespace stackchan
