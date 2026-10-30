#include "ECS/Registry.h"

#include <array>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using madcat::ecs::Entity;
using madcat::ecs::Registry;

struct Position { int value; };
struct Velocity { int value; };
struct Tag { int value; };
using Owned = std::unique_ptr<int>;

struct Constructed
{
    static inline int attempts = 0;
    int value;
    explicit Constructed(int number) : value(number)
    {
        ++attempts;
        if (number < 0) { throw std::runtime_error("Unexpected component construction"); }
    }
};

void check(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

void emptyAndMissingPools()
{
    Registry registry;
    int visits = 0;
    const auto count = [&](Entity, auto&...) { ++visits; };
    registry.each<Position>(count);
    std::as_const(registry).each<Position, Velocity>(count);
    const auto entity = registry.create();
    check(registry.add<Position>(entity, 1) != nullptr, "Setup failed");
    registry.each<Position, Velocity>(count);
    registry.each<Velocity, Position>(count);
    check(visits == 0, "Missing pool produced a match");
    check(registry.remove<Position>(entity), "Removal failed");
    registry.each<Position>(count);
    check(visits == 0, "Retained empty pool produced a match");
    check(registry.add<Velocity>(entity, 2) != nullptr, "Second setup failed");
    const auto other = registry.create();
    check(registry.add<Position>(other, 3) != nullptr, "Disjoint setup failed");
    registry.each<Position, Velocity>(count);
    check(visits == 0 && registry.size() == 2, "Disjoint pools matched or query changed entities");
}

void intersectionAndMutation()
{
    Registry registry;
    std::array<Entity, 120> entities;
    for (size_t i = 0; i < entities.size(); ++i)
    {
        entities[i] = registry.create();
        if (i % 2 == 0) { check(registry.add<Position>(entities[i], static_cast<int>(i)) != nullptr, "Position setup"); }
        if (i % 3 == 0) { check(registry.add<Velocity>(entities[i], 7) != nullptr, "Velocity setup"); }
        if (i % 5 == 0) { check(registry.add<Tag>(entities[i], 9) != nullptr, "Tag setup"); }
    }
    std::array<bool, 120> visited{};
    registry.each<Position, Velocity>([&](Entity entity, Position& position, Velocity& velocity)
    {
        const auto index = entity.index;
        check(index < entities.size() && entity == entities[index] && index % 6 == 0, "Wrong intersection");
        check(!visited[index], "Duplicate intersection match");
        visited[index] = true;
        check(&position == registry.tryGet<Position>(entity) && &velocity == registry.tryGet<Velocity>(entity),
            "Callback did not borrow components");
        position.value += velocity.value;
    });
    for (size_t i = 0; i < entities.size(); ++i)
    {
        check(visited[i] == (i % 6 == 0), "Intersection skipped an Entity");
        if (i % 2 == 0)
        {
            check(registry.tryGet<Position>(entities[i])->value == static_cast<int>(i) + (i % 6 == 0 ? 7 : 0),
                "Field edits did not persist or affected a non-match");
        }
    }
    size_t tripleVisits = 0;
    registry.each<Tag, Velocity, Position>([&](Entity entity, Tag& tag, Velocity& velocity, Position& position)
    {
        check(entity.index % 30 == 0 && tag.value == 9 && velocity.value == 7 &&
            position.value == static_cast<int>(entity.index) + 7, "Three-type arguments or intersection incorrect");
        ++tripleVisits;
    });
    check(tripleVisits == 4, "Three-type intersection count incorrect");
}

void singleAndConstAccess()
{
    Registry registry;
    const auto entity = registry.create();
    check(registry.add<Position>(entity, 11) && registry.add<Velocity>(entity, 3), "Setup failed");
    registry.each<Position>([&](Entity found, auto& position)
    {
        static_assert(std::is_same_v<decltype(position), Position&>);
        check(found == entity, "Single-type query returned wrong Entity");
        position.value += 2;
    });
    int visits = 0;
    std::as_const(registry).each<Velocity, Position>([&](Entity found, auto& velocity, auto& position)
    {
        static_assert(std::is_same_v<decltype(velocity), const Velocity&>);
        static_assert(std::is_same_v<decltype(position), const Position&>);
        check(found == entity && position.value == 13 && velocity.value == 3, "Const query incorrect");
        ++visits;
    });
    check(visits == 1, "Const query skipped or duplicated Entity");
}

void structuralChangesRejected()
{
    Registry registry;
    const auto first = registry.create();
    const auto second = registry.create();
    const auto empty = registry.create();
    check(registry.add<Position>(first, 1) && registry.add<Position>(second, 2), "Position setup failed");
    check(registry.add<Velocity>(empty, 3) && registry.add<Constructed>(empty, 4), "Other types setup failed");
    check(registry.add<Owned>(empty, std::make_unique<int>(5)) != nullptr, "Ownership setup failed");
    Constructed::attempts = 0;
    int visits = 0;
    registry.each<Position>([&](Entity entity, Position& position)
    {
        check(!registry.add<Position>(empty, 9), "Added queried type during iteration");
        check(!registry.add<Tag>(entity, 10), "Created unrelated pool during iteration");
        check(!registry.add<Constructed>(entity, -1), "Added unrelated type during iteration");
        check(!registry.remove<Position>(entity) && !registry.remove<Velocity>(empty), "Removed during iteration");
        check(!registry.destroy(entity) && !registry.destroy(empty), "Destroyed during iteration");
        bool rejected = false;
        try { (void)registry.create(); }
        catch (const std::logic_error&) { rejected = true; }
        check(rejected && registry.size() == 3 && registry.isAlive(empty), "Creation was not rejected safely");
        check(registry.tryGet<Position>(entity) == &position && **registry.tryGet<Owned>(empty) == 5,
            "Rejection moved components or released ownership");
        ++position.value;
        ++visits;
    });
    check(visits == 2 && Constructed::attempts == 0, "Rejection skipped matches or called constructor");
    check(registry.tryGet<Position>(first)->value == 2 && registry.tryGet<Position>(second)->value == 3,
        "Guard rejected field edits");
    check(registry.add<Tag>(first, 7) && registry.remove<Velocity>(empty) && registry.destroy(second),
        "Structural operations remained blocked after query");
    check(registry.isAlive(registry.create()), "Creation remained blocked");
}

void constAliasGuard()
{
    Registry registry;
    const auto entity = registry.create();
    check(registry.add<Position>(entity, 1) != nullptr, "Setup failed");
    int visits = 0;
    std::as_const(registry).each<Position>([&](Entity found, const Position& position)
    {
        check(!registry.destroy(found) && !registry.remove<Position>(found) && !registry.add<Tag>(found, 2),
            "Const query allowed structural changes through mutable alias");
        try { (void)registry.create(); check(false, "Const query allowed creation"); }
        catch (const std::logic_error&) {}
        check(position.value == 1 && registry.tryGet<Position>(found) == &position, "Const borrow changed");
        ++visits;
    });
    check(visits == 1 && registry.destroy(entity), "Const query did not release guard");
}

void nestedQueries()
{
    Registry registry;
    for (int i = 0; i < 3; ++i)
    {
        const auto entity = registry.create();
        check(registry.add<Position>(entity, i) && registry.add<Velocity>(entity, 1), "Setup failed");
    }
    int outerVisits = 0;
    int innerVisits = 0;
    registry.each<Position>([&](Entity entity, Position& position)
    {
        registry.each<Velocity, Position>([&](Entity, Velocity&, Position&) { ++innerVisits; });
        std::as_const(registry).each<Position>([&](Entity, const Position&) { ++innerVisits; });
        registry.each<Tag>([](Entity, Tag&) { check(false, "Missing nested pool matched"); });
        try
        {
            registry.each<Velocity>([](Entity, Velocity&) { throw std::runtime_error("Inner callback"); });
        }
        catch (const std::runtime_error&) {}
        check(!registry.destroy(entity) && !registry.add<Tag>(entity, 0), "Inner exit released outer guard");
        check(registry.tryGet<Position>(entity) == &position, "Nested query invalidated reference");
        ++outerVisits;
    });
    check(outerVisits == 3 && innerVisits == 18, "Nested queries skipped or duplicated matches");
    const auto entity = registry.create();
    check(registry.add<Tag>(entity, 8) != nullptr, "Nested guards were not released");
}

void exceptionRecovery()
{
    Registry registry;
    const auto entity = registry.create();
    check(registry.add<Position>(entity, 1) != nullptr, "Setup failed");
    bool caught = false;
    try
    {
        registry.each<Position>([](Entity, Position& position)
        {
            position.value = 19;
            throw std::runtime_error("Callback failure");
        });
    }
    catch (const std::runtime_error&) { caught = true; }
    check(caught && registry.tryGet<Position>(entity)->value == 19, "Exception swallowed or edits rolled back");
    check(registry.add<Velocity>(entity, 2) != nullptr, "Exception left guard active");
    caught = false;
    try { registry.each<Position>([&](Entity, Position&) { (void)registry.create(); }); }
    catch (const std::logic_error&) { caught = true; }
    check(caught && registry.remove<Velocity>(entity), "Uncaught creation rejection left guard active");
    int visits = 0;
    registry.each<Position>([&](Entity, Position&) { ++visits; });
    check(visits == 1 && registry.destroy(entity), "Query did not recover after exceptions");
}

void deferredDestructionAndReuse()
{
    Registry registry;
    std::array<Entity, 32> entities;
    for (size_t i = 0; i < entities.size(); ++i)
    {
        entities[i] = registry.create();
        check(registry.add<Position>(entities[i], static_cast<int>(i)) && registry.add<Velocity>(entities[i], 1),
            "Setup failed");
    }
    std::vector<Entity> pending;
    int visits = 0;
    registry.each<Position, Velocity>([&](Entity entity, Position& position, Velocity&)
    {
        if (position.value % 2 == 0) { pending.push_back(entity); }
        ++visits;
    });
    check(visits == 32 && pending.size() == 16 && registry.size() == 32, "Collection changed membership");
    for (const auto entity : pending) { check(registry.destroy(entity), "Deferred destruction failed"); }
    for (size_t i = 0; i < pending.size(); ++i)
    {
        const auto entity = registry.create();
        check(entity.index == pending[pending.size() - 1 - i].index &&
            entity != pending[pending.size() - 1 - i], "Slot was not reused with new generation");
        check(registry.add<Position>(entity, 100) != nullptr, "Replacement setup failed");
    }
    for (const auto old : pending)
    {
        check(!registry.isAlive(old) && !registry.tryGet<Position>(old) && !registry.destroy(old),
            "Deferred stale handle affected a replacement");
    }
    visits = 0;
    registry.each<Velocity, Position>([&](Entity entity, Velocity&, Position& position)
    {
        check(entity == entities[entity.index] && position.value % 2 == 1, "Destroyed match or inherited component");
        ++visits;
    });
    check(visits == 16, "Relocation skipped or duplicated survivors");
}

void deferredComponentChanges()
{
    Registry registry;
    std::vector<Entity> pending;
    for (int i = 0; i < 12; ++i)
    {
        const auto entity = registry.create();
        check(registry.add<Position>(entity, i) != nullptr, "Setup failed");
    }
    registry.each<Position>([&](Entity entity, Position&) { pending.push_back(entity); });
    for (size_t i = 0; i < pending.size(); ++i)
    {
        check(registry.add<Velocity>(pending[i], 2) != nullptr, "Deferred addition failed");
        if (i % 2 == 0) { check(registry.remove<Position>(pending[i]), "Deferred removal failed"); }
    }
    int visits = 0;
    registry.each<Position, Velocity>([&](Entity, Position& position, Velocity& velocity)
    {
        check(position.value % 2 == 1 && velocity.value == 2, "Query did not observe new membership");
        ++visits;
    });
    check(visits == 6, "Deferred changes produced wrong intersection");
}

void independentRegistries()
{
    Registry registry;
    Registry other;
    const auto entity = registry.create();
    check(registry.add<Position>(entity, 1) != nullptr, "Setup failed");
    registry.each<Position>([&](Entity current, Position&)
    {
        const auto second = other.create();
        check(other.add<Position>(second, 2) != nullptr, "Other Registry was locked");
        other.each<Position>([&](Entity, Position&) { check(!registry.destroy(current), "Outer guard lost"); });
        check(other.remove<Position>(second) && other.destroy(second), "Other guard leaked");
        check(!registry.destroy(current), "Other Registry released outer guard");
    });
    check(registry.destroy(entity), "Outer guard leaked");
}

void moveOnlyComponentsAndCallbacks()
{
    Registry registry;
    const auto entity = registry.create();
    check(registry.add<Owned>(entity, std::make_unique<int>(3)) != nullptr, "Setup failed");
    auto callback = [state = std::make_unique<int>(0)](Entity, Owned& owned) mutable
    {
        *owned += ++*state;
    };
    registry.each<Owned>(callback);
    registry.each<Owned>(callback);
    registry.each<Owned>([state = std::make_unique<int>(4)](Entity, Owned& owned) { *owned += *state; });
    check(**registry.tryGet<Owned>(entity) == 10, "Callback state was copied or ownership was moved");
    std::as_const(registry).each<Owned>([](Entity, auto& owned)
    {
        static_assert(std::is_same_v<decltype(owned), const Owned&>);
        check(*owned == 10, "Const query lost move-only component");
    });
}

void mixedOperations()
{
    Registry registry;
    struct Expected { Entity entity; bool position = false; bool velocity = false; int value = 0; };
    std::array<Expected, 48> model;
    for (auto& item : model) { item.entity = registry.create(); }
    unsigned random = 0x319ab42u;
    for (int step = 0; step < 1024; ++step)
    {
        random = random * 1664525u + 1013904223u;
        auto& item = model[(random >> 8) % model.size()];
        switch ((random >> 24) % 5)
        {
        case 0:
            check((registry.add<Position>(item.entity, step) != nullptr) == !item.position, "Model add mismatch");
            if (!item.position) { item.position = true; item.value = step; }
            break;
        case 1:
            check(registry.remove<Position>(item.entity) == item.position, "Model removal mismatch");
            item.position = false;
            break;
        case 2:
            if (item.velocity) { check(registry.remove<Velocity>(item.entity), "Model velocity removal"); }
            else { check(registry.add<Velocity>(item.entity, 1) != nullptr, "Model velocity addition"); }
            item.velocity = !item.velocity;
            break;
        case 3:
            check(registry.destroy(item.entity), "Model destroy mismatch");
            item.entity = registry.create();
            item.position = item.velocity = false;
            break;
        case 4:
            if (item.position) { registry.tryGet<Position>(item.entity)->value = ++item.value; }
            break;
        }
        std::array<bool, 48> visited{};
        const auto verify = [&](Entity entity, const Position& position, const Velocity& velocity)
        {
            check(entity.index < model.size(), "Unexpected query Entity");
            const auto& expected = model[entity.index];
            check(expected.entity == entity && expected.position && expected.velocity && !visited[entity.index],
                "Query model membership mismatch");
            check(position.value == expected.value && velocity.value == 1, "Query model data mismatch");
            visited[entity.index] = true;
        };
        if (step % 2 == 0) { registry.each<Position, Velocity>(verify); }
        else
        {
            std::as_const(registry).each<Velocity, Position>([&](Entity entity, const Velocity& velocity, const Position& position)
            {
                verify(entity, position, velocity);
            });
        }
        for (const auto& expected : model)
        {
            check(visited[expected.entity.index] == (expected.position && expected.velocity), "Query model skipped match");
        }
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
        {"empty, missing and disjoint pools", emptyAndMissingPools},
        {"two/three-type intersections and field edits", intersectionAndMutation},
        {"single-type and const reference access", singleAndConstAccess},
        {"structural mutation rejection", structuralChangesRejected},
        {"const alias mutation guard", constAliasGuard}, {"nested queries and guards", nestedQueries},
        {"callback exceptions and recovery", exceptionRecovery},
        {"deferred destruction, relocation and reuse", deferredDestructionAndReuse},
        {"deferred component changes", deferredComponentChanges}, {"independent Registries", independentRegistries},
        {"move-only components and callbacks", moveOnlyComponentsAndCallbacks},
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
            std::fprintf(stderr, "Query check failed (%s): %s\n", test.name, exception.what());
            return 1;
        }
    }
    std::printf("Query checks passed: %zu cases.\n", passed);
    return 0;
}
