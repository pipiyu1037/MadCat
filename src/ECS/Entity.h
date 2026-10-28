#pragma once

#include <cstdint>
#include <limits>

namespace madcat::ecs
{
// A value handle local to the Registry that created it. It does not own an
// entity or identify its Registry; never pass it to another Registry.
struct Entity
{
    static constexpr uint32_t invalidIndex = std::numeric_limits<uint32_t>::max();

    uint32_t index = invalidIndex;
    uint32_t generation = 0;

    friend constexpr bool operator==(Entity, Entity) = default;
};
}
