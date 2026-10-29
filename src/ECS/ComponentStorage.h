#pragma once

#include "Entity.h"

#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace madcat::ecs::detail
{
// Registry owns one pool per component type. The erased operation is only used
// to clean every pool when an Entity is destroyed.
class ComponentStorageBase
{
public:
    virtual ~ComponentStorageBase() = default;
    virtual bool remove(Entity entity) noexcept = 0;
};

template<typename T>
class ComponentStorage final : public ComponentStorageBase
{
    static_assert(std::is_same_v<T, std::remove_cvref_t<T>> &&
        std::is_object_v<T> && !std::is_array_v<T>, "Component must be an unqualified object type");
    static_assert(std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_move_assignable_v<T> && std::is_nothrow_destructible_v<T>,
        "Component movement and destruction must not throw");

public:
    template<typename... Args>
    T* add(Entity entity, Args&&... args)
    {
        if (tryGet(entity)) { return nullptr; }

        // Construct before any relocation: arguments may refer to another
        // component in this pool. No membership is published until push succeeds.
        T value(std::forward<Args>(args)...);
        if (entity.index >= sparse_.size())
        {
            sparse_.resize(static_cast<size_t>(entity.index) + 1, absent);
        }
        dense_.push_back({entity, std::move(value)});
        sparse_[entity.index] = dense_.size() - 1;
        return &dense_.back().value;
    }

    [[nodiscard]] T* tryGet(Entity entity) noexcept
    {
        const auto index = find(entity);
        return index == absent ? nullptr : &dense_[index].value;
    }

    [[nodiscard]] const T* tryGet(Entity entity) const noexcept
    {
        const auto index = find(entity);
        return index == absent ? nullptr : &dense_[index].value;
    }

    bool remove(Entity entity) noexcept override
    {
        const auto index = find(entity);
        if (index == absent) { return false; }
        if (index != dense_.size() - 1)
        {
            dense_[index] = std::move(dense_.back());
            sparse_[dense_[index].entity.index] = index;
        }
        dense_.pop_back();
        sparse_[entity.index] = absent;
        return true;
    }

private:
    static constexpr size_t absent = std::numeric_limits<size_t>::max();
    struct Entry
    {
        Entity entity;
        T value;
    };

    [[nodiscard]] size_t find(Entity entity) const noexcept
    {
        if (entity.index >= sparse_.size()) { return absent; }
        const auto index = sparse_[entity.index];
        return index < dense_.size() && dense_[index].entity == entity ? index : absent;
    }

    std::vector<size_t> sparse_;
    std::vector<Entry> dense_;
};
}
