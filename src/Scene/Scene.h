#pragma once

#include "ECS/Registry.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace madcat::scene
{
// Right-handed, Y up, meters, radians, column vectors. Local matrix = T * R * S.
// Quaternion construction uses (w, x, y, z). Scene normalizes accepted rotations.
struct LocalTransform
{
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

class Scene
{
    // One private ECS component per scene Entity; only Scene writes hierarchy
    // and derived matrices. No mutable Registry escapes this boundary.
    struct Node
    {
        LocalTransform local;
        ecs::Entity parent;
        std::vector<ecs::Entity> children;
        glm::mat4 world{1.0f};
    };

    template<typename T>
    static constexpr bool userComponent = !std::is_same_v<std::remove_cvref_t<T>, Node> &&
        !std::is_same_v<std::remove_cvref_t<T>, LocalTransform>;

public:
    // Owns a private Registry. Single-threaded, noncopyable/nonmovable.
    // create() gives an identity root; throws on allocation or during each().
    [[nodiscard]] ecs::Entity create();
    [[nodiscard]] bool isAlive(ecs::Entity entity) const noexcept { return registry_.isAlive(entity); }
    [[nodiscard]] size_t size() const noexcept { return registry_.size(); }

    // Ordinary deletion refuses parents with children. Subtree deletion is
    // explicit, iterative and children-first. Both refuse active Scene queries.
    // destroySubtree allocates its work list before changing the scene.
    [[nodiscard]] bool destroy(ecs::Entity entity) noexcept;
    [[nodiscard]] bool destroySubtree(ecs::Entity entity);

    // Only Scene changes parent/child membership. Default Entity detaches to root.
    // Reparenting preserves local TRS, rejects dead parents, self/cycles and each().
    // Allocation failure leaves the old hierarchy intact.
    [[nodiscard]] bool setParent(ecs::Entity entity, ecs::Entity parent);
    [[nodiscard]] ecs::Entity parent(ecs::Entity entity) const noexcept;
    [[nodiscard]] std::span<const ecs::Entity> children(ecs::Entity entity) const noexcept;

    // Invalid handles, nonfinite fields or zero quaternion return false unchanged.
    // Zero/negative scales are allowed. Field edits are allowed during each().
    [[nodiscard]] bool setLocalTransform(ecs::Entity entity, const LocalTransform& local) noexcept;
    [[nodiscard]] const LocalTransform* localTransform(ecs::Entity entity) const noexcept;

    // Rebuild all world matrices, parents first, without recursion or TRS
    // decomposition. Call outside each(), after all local/hierarchy edits.
    // Nonfinite composed matrices return false; allocation may throw. Accessors
    // expose no world matrix until a complete successful update.
    [[nodiscard]] bool updateWorldTransforms();
    [[nodiscard]] bool worldTransformsCurrent() const noexcept { return worldsCurrent_; }
    [[nodiscard]] const glm::mat4* worldTransform(ecs::Entity entity) const noexcept;

    // Scene-owned data uses the accessors above. User components (Camera,
    // future Mesh, Material...) retain Registry's type and reference contracts.
    // Their constructors, moves and destructors must not reenter this Scene.
    template<typename T, typename... Args> requires userComponent<T>
    [[nodiscard]] T* add(ecs::Entity entity, Args&&... args)
    {
        return registry_.add<T>(entity, std::forward<Args>(args)...);
    }

    template<typename T> requires userComponent<T>
    [[nodiscard]] T* tryGet(ecs::Entity entity) noexcept { return registry_.tryGet<T>(entity); }

    template<typename T> requires userComponent<T>
    [[nodiscard]] const T* tryGet(ecs::Entity entity) const noexcept { return registry_.tryGet<T>(entity); }

    template<typename T> requires userComponent<T>
    [[nodiscard]] bool remove(ecs::Entity entity) noexcept { return registry_.remove<T>(entity); }

    template<typename First, typename... Rest, typename Function>
        requires (userComponent<First> && (... && userComponent<Rest>))
    void each(Function&& function)
    {
        const IterationScope scope(*this);
        registry_.each<First, Rest...>(std::forward<Function>(function));
    }

    template<typename First, typename... Rest, typename Function>
        requires (userComponent<First> && (... && userComponent<Rest>))
    void each(Function&& function) const
    {
        const IterationScope scope(*this);
        registry_.each<First, Rest...>(std::forward<Function>(function));
    }

private:
    class IterationScope
    {
    public:
        explicit IterationScope(const Scene& scene) noexcept : scene_(scene) { ++scene_.iterationDepth_; }
        ~IterationScope() { --scene_.iterationDepth_; }
        IterationScope(const IterationScope&) = delete;
        IterationScope& operator=(const IterationScope&) = delete;
    private:
        const Scene& scene_;
    };

    void detach(ecs::Entity entity, Node& node) noexcept;

    ecs::Registry registry_;
    mutable size_t iterationDepth_ = 0;
    bool worldsCurrent_ = false;
};
}
