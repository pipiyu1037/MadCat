#pragma once

#include "RHI/DrawTransform.h"
#include "Scene/Scene.h"

#include <optional>

namespace madcat::sandbox
{
// Deterministic poses for exercising transforms/depth, selected by 1-7.
enum class TrianglePose { Default, ModelParent, CameraParent, NearClipped, FarClipped, DepthForward, DepthReverse };

struct TriangleDraws
{
    std::array<rhi::DrawTransform, 2> transforms;
    size_t count = 1;
};

// CPU-only example scene. GPU geometry stays in SandboxApplication, not ECS.
// Explicit model/camera selection belongs to this exercise; this is not a
// general RenderScene extractor, asset model or camera controller.
class TriangleScene
{
public:
    TriangleScene();
    [[nodiscard]] bool selectPose(TrianglePose pose);
    // Update stale world transforms, then snapshot projection * view * world.
    // Invalid aspect, failed world update or unrepresentable values -> nullopt.
    [[nodiscard]] std::optional<TriangleDraws> drawTransforms(float aspectRatio);

private:
    scene::Scene scene_;
    ecs::Entity modelParent_, model_, secondModel_, cameraParent_, camera_;
    TrianglePose pose_ = TrianglePose::Default;
};
}
