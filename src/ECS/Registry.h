#pragma once

#include "ComponentStorage.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
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
    // Throws std::logic_error during each() on this Registry.
    [[nodiscard]] Entity create();

    // Invalid/default/stale handles or an active each() return false.
    // Removes all components without allocating. A slot at maximum generation
    // is permanently retired instead of making an old handle valid again.
    [[nodiscard]] bool destroy(Entity entity) noexcept;
    [[nodiscard]] bool isAlive(Entity entity) const noexcept;
    [[nodiscard]] size_t size() const noexcept { return aliveCount_; }

    // One component of T per Entity. Invalid/stale Entity or duplicate T returns
    // nullptr without constructing a component. Active each() also returns
    // nullptr (for every component type). Constructor/allocation failures
    // throw without publishing membership; forwarded arguments follow their
    // own construction/move semantics, including explicitly moved components.
    // T must be an unqualified object with noexcept move construction, move
    // assignment and destruction. Component constructors, moves and destructors
    // must not reenter this Registry.
    template<typename T, typename... Args>
    [[nodiscard]] T* add(Entity entity, Args&&... args)
    {
        if (queryDepth_ != 0 || !isAlive(entity)) { return nullptr; }
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

    // Missing component, invalid/stale Entity or active each() returns false;
    // never allocates.
    template<typename T>
    [[nodiscard]] bool remove(Entity entity) noexcept
    {
        if (queryDepth_ != 0 || !isAlive(entity)) { return false; }
        auto* storage = findStorage<T>();
        return storage ? storage->remove(entity) : false;
    }

    // Visits each Entity with all requested component types exactly once.
    // Types follow add<T>'s rules; at least one type is required. Calls function
    // as an lvalue with (Entity, First&, Rest&...); const Registry supplies const
    // references. Return values are ignored. Missing pools give no matches.
    // Order is unspecified; nested queries and field edits are allowed.
    // create/add/remove/destroy on this Registry are rejected until the outermost
    // query exits, including queries made through const aliases. Collect Entity
    // handles and apply structural changes after each() returns; nothing is queued
    // automatically. Callback exceptions propagate and restore the query guard;
    // earlier field edits remain. Callbacks must not destroy this Registry.
    template<typename First, typename... Rest, typename Function>
    void each(Function&& function)
    {
        eachImpl<First, Rest...>(*this, std::forward<Function>(function));
    }

    template<typename First, typename... Rest, typename Function>
    void each(Function&& function) const
    {
        eachImpl<First, Rest...>(*this, std::forward<Function>(function));
    }

private:
    class QueryScope
    {
    public:
        explicit QueryScope(const Registry& registry) noexcept : registry_(registry) { ++registry_.queryDepth_; }
        ~QueryScope() { --registry_.queryDepth_; }
        QueryScope(const QueryScope&) = delete;
        QueryScope& operator=(const QueryScope&) = delete;

    private:
        const Registry& registry_;
    };

    template<typename First, typename... Rest, typename Self, typename Function>
    static void eachImpl(Self& registry, Function&& function)
    {
        // Resolve pools once; iterate the first type's dense candidates and
        // check the intersection. No pool creation, cache or query allocation.
        auto pools = std::tuple{registry.template findStorage<First>(), registry.template findStorage<Rest>()...};
        std::apply([&](auto* first, auto*... rest)
        {
            if (!first || !(... && (rest != nullptr))) { return; }
            const QueryScope scope(registry);
            for (size_t index = 0; index < first->size(); ++index)
            {
                const auto entity = first->entityAt(index);
                if ((... && (rest->tryGet(entity) != nullptr)))
                {
                    std::invoke(function, entity, *first->tryGet(entity), *rest->tryGet(entity)...);
                }
            }
        }, pools);
    }

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
    mutable size_t queryDepth_ = 0;
    std::unordered_map<std::type_index, std::unique_ptr<detail::ComponentStorageBase>> storages_;
};
}
