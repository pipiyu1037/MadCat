#pragma once

#include "RenderScene/RenderScene.h"
#include <array>

namespace madcat::sandbox
{
// Example Scene assembly only. Geometry ownership and per-frame extraction stay
// in Assets/RenderScene; components never contain GPU objects.
class ModelScene
{
public:
    ModelScene(const assets::ModelAssets& assets, assets::ModelAssetId model);
    [[nodiscard]] bool selectPose(int pose);
    [[nodiscard]] std::expected<std::vector<render::MeshDraw>, std::string> draws(float aspect);

private:
    void spawn(size_t index);
    const assets::ModelAssets& assets_;
    assets::ModelAssetId model_;
    scene::Scene scene_;
    glm::mat4 fit_{1};
    std::array<ecs::Entity, 2> parents_;
    std::array<ecs::Entity, 2> roots_;
    ecs::Entity cameraParent_;
    ecs::Entity camera_;
    int pose_ = 0;
};
}
