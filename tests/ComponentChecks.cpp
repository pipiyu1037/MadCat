#include "ECS/Registry.h"

#include <array>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using madcat::ecs::Entity;
using madcat::ecs::Registry;

struct Value
{
    int number;
};

struct Constructed
{
    static inline int attempts = 0;
    int number;

    explicit Constructed(int value) : number(value)
    {
        ++attempts;
        if (value < 0) { throw std::runtime_error("Injected component construction failure"); }
    }
};

struct Resource
{
    int& released;
    explicit Resource(int& count) : released(count) {}
    ~Resource() { ++released; }
};
using Owned = std::unique_ptr<Resource>;

static_assert(std::is_nothrow_move_constructible_v<Constructed>);
static_assert(std::is_nothrow_move_assignable_v<Constructed>);
static_assert(std::is_same_v<decltype(std::declval<const Registry&>().tryGet<Value>({})), const Value*>);
static_assert(noexcept(std::declval<Registry&>().remove<Value>({})));
static_assert(noexcept(std::declval<Registry&>().destroy({})));

void check(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

void basicAccess()
{
    Registry registry;
    const auto entity = registry.create();
    check(!registry.tryGet<Value>(entity) && !registry.remove<Value>(entity), "Missing pool accepted access");
    auto* value = registry.add<Value>(entity, 7);
    check(value && value->number == 7, "Component was not initialized");
    value->number = 9;
    const auto& readOnly = registry;
    check(readOnly.tryGet<Value>(entity)->number == 9, "Const access did not observe mutation");
    check(registry.size() == 1, "Adding a component changed Entity count");
    check(registry.remove<Value>(entity), "Component removal failed");
    check(!readOnly.tryGet<Value>(entity) && !registry.remove<Value>(entity), "Missing component accepted access");
    check(registry.isAlive(entity), "Component removal destroyed its Entity");
    check(registry.add<Value>(entity, 11)->number == 11, "Component could not be re-added");
}

void duplicateAddition()
{
    Registry registry;
    const auto entity = registry.create();
    Constructed::attempts = 0;
    auto* original = registry.add<Constructed>(entity, 12);
    check(!registry.add<Constructed>(entity, -1), "Duplicate component was accepted");
    check(Constructed::attempts == 1, "Duplicate component constructor was called");
    check(registry.tryGet<Constructed>(entity) == original && original->number == 12,
        "Duplicate addition changed existing component");
}

void invalidHandles()
{
    Registry registry;
    const auto entity = registry.create();
    Constructed::attempts = 0;
    check(registry.add<Constructed>(entity, 19) != nullptr, "Initial component missing");
    const Entity invalid[] = {{}, {entity.index, 0}, {entity.index, entity.generation + 1},
        {Entity::invalidIndex - 1, 1}, {std::numeric_limits<uint32_t>::max(), 1}};
    for (const auto handle : invalid)
    {
        check(!registry.add<Constructed>(handle, -1), "Invalid Entity accepted addition");
        check(!registry.add<std::string>(handle, "invalid"), "Invalid Entity created a new pool component");
        check(!registry.tryGet<Constructed>(handle) && !std::as_const(registry).tryGet<Constructed>(handle),
            "Invalid Entity accessed a component");
        check(!registry.remove<Constructed>(handle) && !registry.destroy(handle), "Invalid Entity changed storage");
    }
    check(Constructed::attempts == 1 && registry.tryGet<Constructed>(entity)->number == 19,
        "Invalid access constructed or modified a component");
}

void removalRelocation()
{
    Registry registry;
    std::array<Entity, 5> entities;
    for (size_t i = 0; i < entities.size(); ++i)
    {
        entities[i] = registry.create();
        check(registry.add<Value>(entities[i], static_cast<int>(i)) != nullptr, "Setup failed");
    }
    // Remove first, middle and last entries, checking the moved entries through
    // their Entity handles. No old pointer is used after structural changes.
    for (const auto index : {0, 2, 3, 4, 1})
    {
        check(registry.remove<Value>(entities[index]), "Packed removal failed");
        check(!registry.tryGet<Value>(entities[index]), "Removed entry is still accessible");
        for (size_t i = 0; i < entities.size(); ++i)
        {
            const auto* value = registry.tryGet<Value>(entities[i]);
            if (value) { check(value->number == static_cast<int>(i), "Relocation mapped value to wrong Entity"); }
        }
    }
    for (size_t i = 0; i < entities.size(); ++i)
    {
        check(registry.add<Value>(entities[i], static_cast<int>(i + 10)) != nullptr, "Re-add after removal failed");
    }
    for (size_t i = 0; i < entities.size(); ++i)
    {
        check(registry.tryGet<Value>(entities[i])->number == static_cast<int>(i + 10), "Re-add mapping is wrong");
    }
}

void destroyedAndReusedEntity()
{
    Registry registry;
    const auto old = registry.create();
    check(registry.add<Value>(old, 23) && registry.add<std::string>(old, "old"), "Setup failed");
    check(registry.destroy(old), "Entity destruction failed");
    const auto current = registry.create();
    check(current.index == old.index && current.generation != old.generation, "Slot was not reused with new identity");
    check(!registry.tryGet<Value>(current) && !registry.tryGet<std::string>(current), "Reused Entity inherited components");
    check(registry.add<Value>(current, 29) && registry.add<std::string>(current, "new"), "Replacement setup failed");
    check(!registry.add<Value>(old, 99) && !registry.tryGet<Value>(old) && !registry.remove<Value>(old),
        "Stale handle accessed replacement component");
    check(!registry.destroy(old) && registry.tryGet<Value>(current)->number == 29 &&
        *registry.tryGet<std::string>(current) == "new", "Stale destruction changed replacement");
}

void entityCleanup()
{
    int released = 0;
    Registry registry;
    const auto first = registry.create();
    const auto second = registry.create();
    check(registry.add<Owned>(first, std::make_unique<Resource>(released)) != nullptr, "First resource missing");
    check(registry.add<Value>(first, 31) != nullptr, "Second component type missing");
    check(registry.add<Owned>(second, std::make_unique<Resource>(released)) != nullptr, "Second resource missing");
    check(registry.destroy(first) && released == 1, "Destroy did not release exactly its own resource");
    check(registry.tryGet<Owned>(second) && *registry.tryGet<Owned>(second), "Relocation lost survivor ownership");
    check(!registry.destroy(first) && released == 1, "Repeated destruction released another resource");
    check(registry.remove<Owned>(second) && released == 2, "Removal did not release resource");
    check(registry.destroy(second) && released == 2, "Destruction released removed resource twice");
}

void registryCleanup()
{
    int released = 0;
    {
        Registry registry;
        for (int i = 0; i < 128; ++i)
        {
            const auto entity = registry.create();
            check(registry.add<Owned>(entity, std::make_unique<Resource>(released)) != nullptr, "Owned add failed");
        }
        check(released == 0, "Pool growth released a live resource");
    }
    check(released == 128, "Registry destruction leaked or duplicated resource destruction");
}

void sparseGrowth()
{
    Registry registry;
    std::vector<Entity> entities;
    for (size_t i = 0; i < 4096; ++i) { entities.push_back(registry.create()); }
    // Start with a high index and keep holes in the sparse mapping.
    for (size_t i = entities.size(); i-- > 0;)
    {
        if (i % 3 == 0) { check(registry.add<Value>(entities[i], static_cast<int>(i)) != nullptr, "Sparse add failed"); }
    }
    for (size_t i = 0; i < entities.size(); ++i)
    {
        const auto* value = registry.tryGet<Value>(entities[i]);
        check((value != nullptr) == (i % 3 == 0), "Sparse hole returned wrong membership");
        if (value) { check(value->number == static_cast<int>(i), "Growth corrupted component value"); }
    }
    for (const auto entity : entities) { check(registry.destroy(entity), "Sparse destruction failed"); }
    check(registry.size() == 0, "Sparse destruction left live entities");
}

void poolIndependence()
{
    Registry registry;
    const auto entity = registry.create();
    auto* value = registry.add<Value>(entity, 37);
    check(registry.add<std::string>(entity, "material") != nullptr, "Different type on same Entity failed");
    for (int i = 0; i < 256; ++i)
    {
        const auto other = registry.create();
        check(registry.add<std::string>(other, "mesh") != nullptr, "Other pool growth failed");
    }
    check(registry.remove<std::string>(entity), "Other pool removal failed");
    check(registry.tryGet<Value>(entity) == value && value->number == 37, "Other pool invalidated Value reference");
    Registry otherRegistry;
    const auto otherEntity = otherRegistry.create();
    check(otherRegistry.add<Value>(otherEntity, 41)->number == 41 && value->number == 37,
        "Pools leaked data between registries");
}

void constructionFailure()
{
    Registry registry;
    const auto first = registry.create();
    const auto second = registry.create();
    bool caught = false;
    try { (void)registry.add<Constructed>(first, -1); }
    catch (const std::runtime_error&) { caught = true; }
    check(caught && !registry.tryGet<Constructed>(first), "Failed first construction published a component");
    check(registry.add<Constructed>(first, 43) != nullptr, "Retry after construction failure failed");
    caught = false;
    try { (void)registry.add<Constructed>(second, -1); }
    catch (const std::runtime_error&) { caught = true; }
    check(caught && !registry.tryGet<Constructed>(second), "Failed later construction published a component");
    check(registry.tryGet<Constructed>(first)->number == 43 && registry.size() == 2,
        "Construction failure changed existing data");
    check(registry.add<Constructed>(second, 47)->number == 47, "Later retry failed");
    check(registry.destroy(first) && registry.tryGet<Constructed>(second)->number == 47,
        "Cleanup after failure corrupted survivor");
}

void aliasedArguments()
{
    Registry registry;
    auto previous = registry.create();
    check(registry.add<Value>(previous, 53) != nullptr, "Alias setup failed");
    for (size_t i = 0; i < 2048; ++i)
    {
        const auto next = registry.create();
        auto* source = registry.tryGet<Value>(previous);
        // Either the component or one of its fields may be an add argument.
        auto* added = i % 2 == 0 ? registry.add<Value>(next, *source) : registry.add<Value>(next, source->number);
        check(added && added->number == 53, "Aliased argument was read after relocation");
        check(registry.tryGet<Value>(previous)->number == 53, "Aliased copy changed source");
        previous = next;
    }
}

void forwarding()
{
    int released = 0;
    Registry registry;
    const auto entity = registry.create();
    check(*registry.add<std::string>(entity, 5, 'x') == "xxxxx", "Constructor arguments were not forwarded");
    auto resource = std::make_unique<Resource>(released);
    check(registry.add<Owned>(entity, std::move(resource)) != nullptr && !resource,
        "Move-only component was not moved into storage");
    // A rejected add does not consume an rvalue argument.
    auto replacement = std::make_unique<Resource>(released);
    check(!registry.add<Owned>(entity, std::move(replacement)) && replacement,
        "Duplicate add consumed its argument");
    check(registry.destroy(entity) && released == 1, "Moved resource was not released");
}

void mixedOperations()
{
    Registry registry;
    struct Expected
    {
        Entity entity;
        bool present = false;
        int value = 0;
    };
    std::array<Expected, 64> expected;
    for (auto& item : expected) { item.entity = registry.create(); }
    uint32_t random = 0x729A31u;
    for (int step = 0; step < 4096; ++step)
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        auto& item = expected[random % expected.size()];
        switch ((random >> 8) % 4)
        {
        case 0:
            check((registry.add<Value>(item.entity, step) != nullptr) == !item.present, "Model addition mismatch");
            if (!item.present) { item.value = step; item.present = true; }
            break;
        case 1:
            check(registry.remove<Value>(item.entity) == item.present, "Model removal mismatch");
            item.present = false;
            break;
        case 2:
        {
            const auto old = item.entity;
            check(registry.destroy(old), "Model destruction failed");
            item.entity = registry.create();
            item.present = false;
            check(!registry.tryGet<Value>(old) && !registry.remove<Value>(old), "Model stale handle accepted");
            break;
        }
        case 3:
            if (item.present) { registry.tryGet<Value>(item.entity)->number = ++item.value; }
            break;
        }
        for (const auto& current : expected)
        {
            const auto* value = registry.tryGet<Value>(current.entity);
            check((value != nullptr) == current.present, "Model membership mismatch");
            if (value) { check(value->number == current.value, "Model value mismatch"); }
        }
        check(registry.size() == expected.size(), "Component operations changed Entity count");
    }
}
}

int main()
{
    const struct
    {
        const char* name;
        void (*run)();
    } cases[] = {
        {"add, mutable/const access and re-add", basicAccess}, {"duplicate addition", duplicateAddition},
        {"invalid handles", invalidHandles}, {"packed removal and relocation", removalRelocation},
        {"destroyed and reused Entity", destroyedAndReusedEntity}, {"Entity resource cleanup", entityCleanup},
        {"Registry resource cleanup", registryCleanup}, {"sparse holes and growth", sparseGrowth},
        {"independent type pools and registries", poolIndependence}, {"construction failure and retry", constructionFailure},
        {"aliased constructor arguments", aliasedArguments}, {"forwarding and move-only ownership", forwarding},
        {"mixed operations against a reference model", mixedOperations}
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
            std::fprintf(stderr, "Component check failed (%s): %s\n", test.name, exception.what());
            return 1;
        }
    }
    std::printf("Component checks passed: %zu cases.\n", passed);
    return 0;
}
