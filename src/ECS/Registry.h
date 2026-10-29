#pragma once

#include "ComponentStorage.h"

#include <cstddef>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace madcat::ecs
{
// Owns entity lifetimes and their components. Single-threaded; handles are
// valid only in this Registry and expire when destroyed or when it is released.
// Copying/moving is disabled to keep the owning Registry unambiguous.
class Registry
{
public:
    Registry() = default;

    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = delete;
    Registry& operator=(Registry&&) = delete;

    // Reuses a free slot or grows storage. Growth preserves existing handles.
    // Allocation/index exhaustion may throw, leaving all entity state intact.
    [[nodiscard]] Entity create();

    // Invalid/default/stale handles return false without changing any entity.
    // Removes all components without allocating. A slot at maximum generation
    // is permanently retired instead of making an old handle valid again.
    [[nodiscard]] bool destroy(Entity entity) noexcept;
    [[nodiscard]] bool isAlive(Entity entity) const noexcept;
    [[nodiscard]] size_t size() const noexcept { return aliveCount_; }

    // One component of T per Entity. Invalid/stale Entity or duplicate T returns
    // nullptr without constructing a component. Constructor/allocation failures
    // throw without publishing membership; forwarded arguments follow their
    // own construction/move semantics, including explicitly moved components.
    // T must be an unqualified object with noexcept move construction, move
    // assignment and destruction. Component constructors, moves and destructors
    // must not reenter this Registry.
    template<typename T, typename... Args>
    [[nodiscard]] T* add(Entity entity, Args&&... args)
    {
        if (!isAlive(entity)) { return nullptr; }
        auto* storage = findStorage<T>();
        if (!storage)
        {
            auto candidate = std::make_unique<detail::ComponentStorage<T>>();
            storage = candidate.get();
            storages_.emplace(std::type_index(typeid(T)), std::move(candidate));
        }
        return storage->add(entity, std::forward<Args>(args)...);
    }

    // Missing component or invalid/stale Entity returns nullptr.
    // Reacquire pointers/references after structural changes to that pool
    // (add/remove/destroy). Registry destruction invalidates all references.
    // Changing fields does not invalidate references; pools are independent.
    template<typename T>
    [[nodiscard]] T* tryGet(Entity entity) noexcept
    {
        if (!isAlive(entity)) { return nullptr; }
        auto* storage = findStorage<T>();
        return storage ? storage->tryGet(entity) : nullptr;
    }

    template<typename T>
    [[nodiscard]] const T* tryGet(Entity entity) const noexcept
    {
        if (!isAlive(entity)) { return nullptr; }
        const auto* storage = findStorage<T>();
        return storage ? storage->tryGet(entity) : nullptr;
    }

    // Missing component or invalid/stale Entity returns false; never allocates.
    template<typename T>
    [[nodiscard]] bool remove(Entity entity) noexcept
    {
        if (!isAlive(entity)) { return false; }
        auto* storage = findStorage<T>();
        return storage ? storage->remove(entity) : false;
    }

private:
    template<typename T>
    [[nodiscard]] detail::ComponentStorage<T>* findStorage() noexcept
    {
        const auto found = storages_.find(std::type_index(typeid(T)));
        return found == storages_.end() ? nullptr :
            static_cast<detail::ComponentStorage<T>*>(found->second.get());
    }

    template<typename T>
    [[nodiscard]] const detail::ComponentStorage<T>* findStorage() const noexcept
    {
        const auto found = storages_.find(std::type_index(typeid(T)));
        return found == storages_.end() ? nullptr :
            static_cast<const detail::ComponentStorage<T>*>(found->second.get());
    }

    struct Slot
    {
        uint32_t generation = 1;
        uint32_t nextFree = Entity::invalidIndex;
        bool alive = true;
    };

    std::vector<Slot> slots_;
    uint32_t freeHead_ = Entity::invalidIndex;
    size_t aliveCount_ = 0;
    std::unordered_map<std::type_index, std::unique_ptr<detail::ComponentStorageBase>> storages_;
};
}
