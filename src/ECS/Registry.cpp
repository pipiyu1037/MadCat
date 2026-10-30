#include "Registry.h"

#include <limits>
#include <stdexcept>

namespace madcat::ecs
{
Entity Registry::create()
{
    if (queryDepth_ != 0) { throw std::logic_error("Cannot create an Entity during a Registry query"); }
    if (freeHead_ != Entity::invalidIndex)
    {
        const auto index = freeHead_;
        auto& slot = slots_[index];
        freeHead_ = slot.nextFree;
        slot.nextFree = Entity::invalidIndex;
        slot.alive = true;
        ++aliveCount_;
        return {index, slot.generation};
    }

    if (slots_.size() >= Entity::invalidIndex)
    {
        throw std::length_error("Entity index space exhausted");
    }
    const auto index = static_cast<uint32_t>(slots_.size());
    slots_.push_back({});
    ++aliveCount_;
    return {index, slots_.back().generation};
}

bool Registry::destroy(Entity entity) noexcept
{
    if (queryDepth_ != 0 || !isAlive(entity)) { return false; }

    for (auto& [type, storage] : storages_) { storage->remove(entity); }
    auto& slot = slots_[entity.index];
    slot.alive = false;
    --aliveCount_;
    if (slot.generation != std::numeric_limits<uint32_t>::max())
    {
        ++slot.generation;
        slot.nextFree = freeHead_;
        freeHead_ = entity.index;
    }
    return true;
}

bool Registry::isAlive(Entity entity) const noexcept
{
    return entity.index < slots_.size() && slots_[entity.index].alive &&
        slots_[entity.index].generation == entity.generation;
}
}
