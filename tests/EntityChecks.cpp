#include "ECS/Registry.h"

#include <cstdio>
#include <exception>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace
{
using madcat::ecs::Entity;
using madcat::ecs::Registry;

static_assert(!std::is_copy_constructible_v<Registry> && !std::is_copy_assignable_v<Registry>);
static_assert(!std::is_move_constructible_v<Registry> && !std::is_move_assignable_v<Registry>);

void check(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

void emptyRegistry()
{
    Registry registry;
    check(registry.size() == 0, "New Registry is not empty");
    check(!registry.isAlive({}), "Default Entity is alive");
    check(!registry.destroy({}), "Default Entity can be destroyed");
    check(!registry.isAlive({0, 1}) && !registry.destroy({0, 1}), "Unallocated Entity is accepted");
    check(registry.size() == 0, "Invalid destruction changed entity count");
}

void distinctEntities()
{
    Registry registry;
    const auto first = registry.create();
    const auto second = registry.create();
    const auto copy = first;
    check(registry.isAlive(first) && registry.isAlive(second), "Created Entity is not alive");
    check(first.index != second.index && first != second, "Live entities share an index");
    check(first.generation != 0 && second.generation != 0, "Generation zero is used for a live Entity");
    check(copy == first && registry.isAlive(copy), "Copying a handle changed its identity");
    check(registry.size() == 2, "Created entity count is incorrect");
}

void destruction()
{
    Registry registry;
    const auto first = registry.create();
    const auto second = registry.create();
    const auto copy = first;
    check(registry.destroy(first), "Live Entity destruction failed");
    check(!registry.isAlive(first) && !registry.isAlive(copy), "Destroyed Entity or its copy is alive");
    check(!registry.destroy(copy), "Repeated destruction succeeded");
    check(registry.isAlive(second) && registry.size() == 1, "Destruction changed another Entity");
    check(registry.destroy(second) && registry.size() == 0, "Last Entity destruction failed");
}

void slotReuse()
{
    Registry registry;
    const auto old = registry.create();
    check(registry.destroy(old), "Could not free slot");
    const auto replacement = registry.create();
    check(replacement.index == old.index, "Free slot was not reused");
    check(replacement.generation != old.generation && replacement != old, "Reused slot has the old identity");
    check(!registry.isAlive(old) && !registry.destroy(old), "Stale handle can access reused slot");
    check(registry.isAlive(replacement) && registry.size() == 1, "Stale destruction changed replacement");
    const auto another = registry.create();
    check(another.index != replacement.index && registry.size() == 2, "Stale destruction corrupted free slots");
}

void invalidHandles()
{
    Registry registry;
    const auto entity = registry.create();
    const Entity invalid[] = {
        {}, {Entity::invalidIndex, entity.generation}, {entity.index + 1, entity.generation},
        {entity.index, 0}, {entity.index, entity.generation + 1},
        {entity.index, std::numeric_limits<uint32_t>::max()}
    };
    for (const auto handle : invalid)
    {
        check(!registry.isAlive(handle) && !registry.destroy(handle), "Invalid handle was accepted");
        check(registry.isAlive(entity) && registry.size() == 1, "Invalid handle changed entity state");
    }
}

void storageGrowth()
{
    Registry registry;
    std::vector<Entity> handles;
    constexpr size_t count = 4096;
    for (size_t i = 0; i < count; ++i) { handles.push_back(registry.create()); }
    std::set<uint32_t> indices;
    for (const auto handle : handles)
    {
        check(registry.isAlive(handle), "Storage growth invalidated a live handle");
        check(indices.insert(handle.index).second, "Storage growth duplicated an index");
    }
    check(registry.size() == count, "Entity count after storage growth is incorrect");
    for (const auto handle : handles) { check(registry.destroy(handle), "Could not destroy grown Entity"); }
    check(registry.size() == 0, "Grown Registry did not become empty");
    for (const auto handle : handles) { check(!registry.isAlive(handle), "Destroyed grown Entity is alive"); }
}

void mixedReuse()
{
    Registry registry;
    std::vector<Entity> handles;
    std::set<uint32_t> freed;
    constexpr size_t count = 256;
    for (size_t i = 0; i < count; ++i) { handles.push_back(registry.create()); }
    for (size_t i = 0; i < count; i += 2)
    {
        check(registry.destroy(handles[i]), "Mixed destruction failed");
        freed.insert(handles[i].index);
    }
    check(!registry.destroy(handles[0]), "Duplicate destruction of a free slot succeeded");
    std::vector<Entity> replacements;
    for (size_t i = 0; i < count / 2; ++i)
    {
        const auto replacement = registry.create();
        check(freed.erase(replacement.index) == 1, "Free slots were lost or reused twice");
        replacements.push_back(replacement);
    }
    check(freed.empty() && registry.size() == count, "Mixed reuse lost entities or slots");
    for (size_t i = 0; i < count; ++i)
    {
        check(registry.isAlive(handles[i]) == (i % 2 != 0), "Mixed reuse changed an old handle incorrectly");
        if (i % 2 == 0) { check(!registry.destroy(handles[i]), "Old handle destroyed a mixed replacement"); }
    }
    const auto extra = registry.create();
    for (const auto handle : handles) { check(extra.index != handle.index, "Live slot reused after free list exhausted"); }
    for (const auto handle : replacements) { check(registry.isAlive(handle), "Mixed replacement is not alive"); }
    check(registry.size() == count + 1, "Growth after mixed reuse has incorrect entity count");
}

void repeatedLifetimes()
{
    Registry registry;
    std::vector<Entity> old;
    auto current = registry.create();
    const auto slot = current.index;
    for (size_t i = 0; i < 4096; ++i)
    {
        old.push_back(current);
        check(registry.destroy(current), "Repeated lifetime destruction failed");
        current = registry.create();
        check(current.index == slot && current != old.back(), "Repeated lifetime did not reuse slot with new identity");
    }
    for (const auto handle : old)
    {
        check(!registry.isAlive(handle) && !registry.destroy(handle), "Earlier lifetime became valid again");
    }
    check(registry.isAlive(current) && registry.size() == 1, "Earlier lifetime changed current Entity");
}
}

int main()
{
    const struct
    {
        const char* name;
        void (*run)();
    } cases[] = {
        {"empty Registry", emptyRegistry}, {"distinct entities", distinctEntities},
        {"destruction and copied handles", destruction}, {"slot reuse and stale destruction", slotReuse},
        {"invalid indices and generations", invalidHandles}, {"storage growth", storageGrowth},
        {"mixed destruction and reuse", mixedReuse}, {"repeated lifetimes", repeatedLifetimes}
    };
    size_t passed = 0;
    for (const auto& test : cases)
    {
        try
        {
            test.run();
            ++passed;
            std::printf("Passed: %s\n", test.name);
        }
        catch (const std::exception& exception)
        {
            std::fprintf(stderr, "Entity check failed (%s): %s\n", test.name, exception.what());
            return 1;
        }
    }
    std::printf("Entity checks passed: %zu cases.\n", passed);
    return 0;
}
