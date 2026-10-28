#pragma once

#include "Entity.h"

#include <cstddef>
#include <vector>

namespace madcat::ecs
{
// Owns entity lifetimes, with no components yet. Single-threaded; handles are
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
    // Does not allocate. A slot at maximum generation is permanently retired
    // instead of wrapping around and making an old handle valid again.
    [[nodiscard]] bool destroy(Entity entity) noexcept;
    [[nodiscard]] bool isAlive(Entity entity) const noexcept;
    [[nodiscard]] size_t size() const noexcept { return aliveCount_; }

private:
    struct Slot
    {
        uint32_t generation = 1;
        uint32_t nextFree = Entity::invalidIndex;
        bool alive = true;
    };

    std::vector<Slot> slots_;
    uint32_t freeHead_ = Entity::invalidIndex;
    size_t aliveCount_ = 0;
};
}
