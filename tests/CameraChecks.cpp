#include "Scene/Camera.h"
#include "Scene/Scene.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
using madcat::ecs::Entity;
using madcat::scene::buildCameraMatrices;
using madcat::scene::LocalTransform;
using madcat::scene::PerspectiveCamera;
using madcat::scene::Scene;

static_assert(std::is_nothrow_move_constructible_v<PerspectiveCamera>);
static_assert(noexcept(buildCameraMatrices(std::declval<const Scene&>(), Entity{}, 1.0f)));

void check(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

void near(double value, double expected)
{
    check(std::isfinite(value) && std::abs(value - expected) < 0.0001, "Unexpected camera coordinate");
}

void point(const glm::mat4& matrix, glm::vec3 input, glm::vec3 expected)
{
    const auto result = glm::dmat4(matrix) * glm::dvec4(input, 1.0);
    near(result.x, expected.x); near(result.y, expected.y); near(result.z, expected.z); near(result.w, 1.0);
}

glm::dvec3 ndc(const glm::mat4& matrix, glm::vec3 input)
{
    const auto clip = glm::dmat4(matrix) * glm::dvec4(input, 1.0);
    check(clip.w > 0.0 && std::isfinite(clip.w), "Visible point has invalid clip W");
    return glm::dvec3(clip) / clip.w;
}

Entity cameraEntity(Scene& scene, PerspectiveCamera camera = {})
{
    const auto entity = scene.create();
    check(scene.add<PerspectiveCamera>(entity, camera) != nullptr, "Camera component creation failed");
    return entity;
}

void identityAndDepth()
{
    Scene scene;
    const auto entity = cameraEntity(scene);
    check(scene.updateWorldTransforms(), "World update failed");
    const auto matrices = buildCameraMatrices(scene, entity, 1.0f);
    check(matrices.has_value(), "Default camera failed");
    point(matrices->view, {1, 2, 3}, {1, 2, 3});
    const auto* camera = scene.tryGet<PerspectiveCamera>(entity);
    near(ndc(matrices->viewProjection, {0, 0, -camera->nearPlane}).z, 0.0);
    near(ndc(matrices->viewProjection, {0, 0, -camera->farPlane}).z, 1.0);
    const auto center = ndc(matrices->viewProjection, {0, 0, -1});
    near(center.x, 0); near(center.y, 0);
    check(center.z > 0 && center.z < 1, "Interior point outside depth range");
    const auto behind = matrices->viewProjection * glm::vec4(0, 0, 1, 1);
    check(behind.w < 0, "Camera forward was not -Z");
}

void frustumAndViewport()
{
    Scene scene;
    const auto entity = cameraEntity(scene, {glm::radians(90.0f), 1.0f, 9.0f});
    check(scene.updateWorldTransforms(), "World update failed");
    const auto matrices = buildCameraMatrices(scene, entity, 2.0f);
    check(matrices.has_value(), "Perspective camera failed");
    const auto corner = ndc(matrices->viewProjection, {2, 1, -1});
    near(corner.x, 1); near(corner.y, -1); near(corner.z, 0);
    const auto opposite = ndc(matrices->viewProjection, {-2, -1, -1});
    near(opposite.x, -1); near(opposite.y, 1);
    // Positive-height viewport: camera +Y reaches the top pixel edge.
    near((corner.y + 1.0) * 720.0 / 2.0, 0);
    near((opposite.y + 1.0) * 720.0 / 2.0, 720);
    check(ndc(matrices->projection, {0, 0, -0.5f}).z < 0 &&
        ndc(matrices->projection, {0, 0, -10}).z > 1, "Near/far clipping convention wrong");
    const auto square = buildCameraMatrices(scene, entity, 1.0f);
    check(square.has_value(), "Aspect update failed");
    near(ndc(square->projection, {1, 0, -1}).x, 1);
    near(ndc(matrices->projection, {1, 0, -1}).x, 0.5);
}

void translatedAndRotated()
{
    Scene scene;
    const auto entity = cameraEntity(scene);
    LocalTransform pose;
    pose.translation = {5, 2, 3};
    pose.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    check(scene.setLocalTransform(entity, pose) && scene.updateWorldTransforms(), "Camera pose failed");
    const auto matrices = buildCameraMatrices(scene, entity, 1.0f);
    check(matrices.has_value(), "Posed camera failed");
    point(matrices->view, {5, 2, 3}, {0, 0, 0});
    point(matrices->view, {1, 2, 3}, {0, 0, -4});
    point(matrices->view, {1, 2, 2}, {1, 0, -4});
    const auto center = ndc(matrices->viewProjection, {1, 2, 3});
    near(center.x, 0); near(center.y, 0);
    check(ndc(matrices->viewProjection, {1, 3, 3}).y < 0, "Camera up direction inverted twice");
    const auto staged = matrices->projection * (matrices->view * glm::vec4(1, 3, 2, 1));
    const auto combined = matrices->viewProjection * glm::vec4(1, 3, 2, 1);
    for (int index = 0; index < 4; ++index) { near(combined[index], staged[index]); }
}

void inheritedPoseAndReparent()
{
    Scene scene;
    const auto entity = cameraEntity(scene);
    const auto parent = scene.create();
    LocalTransform parentPose;
    parentPose.translation = {10, 0, 0};
    parentPose.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    LocalTransform local;
    local.translation = {0, 0, -2};
    check(scene.setLocalTransform(parent, parentPose) && scene.setLocalTransform(entity, local) &&
        scene.setParent(entity, parent) && scene.updateWorldTransforms(), "Parent setup failed");
    const auto inherited = buildCameraMatrices(scene, entity, 1.0f);
    check(inherited.has_value(), "Inherited camera failed");
    point(inherited->view, {8, 0, 0}, {0, 0, 0});
    point(inherited->view, {5, 0, 0}, {0, 0, -3});
    parentPose.translation.x = 20;
    check(scene.setLocalTransform(parent, parentPose) && !buildCameraMatrices(scene, entity, 1),
        "Parent edit exposed stale camera matrices");
    check(scene.updateWorldTransforms(), "Parent update failed");
    const auto moved = buildCameraMatrices(scene, entity, 1);
    check(moved.has_value(), "Moved parent camera failed");
    point(moved->view, {18, 0, 0}, {0, 0, 0});
    check(scene.setParent(entity, {}) && !buildCameraMatrices(scene, entity, 1) && scene.updateWorldTransforms(),
        "Detach invalidation failed");
    const auto detached = buildCameraMatrices(scene, entity, 1);
    check(detached.has_value(), "Detached camera failed");
    point(detached->view, {0, 0, -5}, {0, 0, -3});
}

void scaleAndShear()
{
    Scene scene;
    const auto parent = scene.create();
    const auto entity = cameraEntity(scene);
    LocalTransform parentPose;
    parentPose.scale = {2, 3, -4};
    LocalTransform local;
    local.translation = {1, 2, 3};
    local.rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 0, 1));
    local.scale = {1, 2, 0.5f};
    check(scene.setParent(entity, parent) && scene.setLocalTransform(parent, parentPose) &&
        scene.setLocalTransform(entity, local) && scene.updateWorldTransforms(), "Shear setup failed");
    const auto matrices = buildCameraMatrices(scene, entity, 1);
    check(matrices.has_value(), "Invertible scaled/sheared world rejected");
    const auto worldPoint = *scene.worldTransform(entity) * glm::vec4(1, 2, -3, 1);
    point(matrices->view, glm::vec3(worldPoint), {1, 2, -3});
    const auto origin = *scene.worldTransform(entity) * glm::vec4(0, 0, 0, 1);
    point(matrices->view, glm::vec3(origin), {0, 0, 0});
}

void missingComponentsAndHandles()
{
    Scene scene;
    const auto entity = scene.create();
    check(!buildCameraMatrices(scene, {}, 1) && !buildCameraMatrices(scene, entity, 1), "Missing data accepted");
    check(scene.updateWorldTransforms() && !buildCameraMatrices(scene, entity, 1), "Missing camera accepted");
    check(scene.add<PerspectiveCamera>(entity) && buildCameraMatrices(scene, entity, 1), "Camera add failed");
    check(scene.remove<PerspectiveCamera>(entity) && !buildCameraMatrices(scene, entity, 1), "Removed camera accepted");
    check(scene.add<PerspectiveCamera>(entity) && scene.destroy(entity), "Camera cleanup failed");
    const auto replacement = cameraEntity(scene);
    check(replacement.index == entity.index && replacement != entity && scene.updateWorldTransforms(), "Reuse failed");
    check(!buildCameraMatrices(scene, entity, 1) && buildCameraMatrices(scene, replacement, 1), "Stale camera handle accepted");
}

void snapshotsAndComponentEdits()
{
    Scene scene;
    const auto entity = cameraEntity(scene);
    check(!buildCameraMatrices(scene, entity, 1) && scene.updateWorldTransforms(), "Stale world accepted");
    const auto original = buildCameraMatrices(scene, entity, 1);
    check(original.has_value(), "Original snapshot failed");
    const float oldFocal = original->projection[0][0];
    scene.each<PerspectiveCamera>([](Entity, PerspectiveCamera& camera) { camera.verticalFov = glm::radians(30.0f); });
    const Scene& readOnly = scene;
    const auto edited = buildCameraMatrices(readOnly, entity, 1);
    check(edited && edited->projection[0][0] > oldFocal && scene.worldTransformsCurrent(), "Lens edit not read");
    near(original->projection[0][0], oldFocal);
    LocalTransform moved;
    moved.translation = {1, 0, 0};
    check(scene.setLocalTransform(entity, moved) && !buildCameraMatrices(scene, entity, 1), "Stale pose accepted");
    point(original->view, {0, 0, 0}, {0, 0, 0});
    check(scene.updateWorldTransforms(), "Pose refresh failed");
    const auto refreshed = buildCameraMatrices(scene, entity, 1);
    check(refreshed.has_value(), "Refreshed camera failed");
    point(refreshed->view, {1, 0, 0}, {0, 0, 0});
}

void invalidParameters()
{
    Scene scene;
    const auto entity = cameraEntity(scene);
    check(scene.updateWorldTransforms(), "World update failed");
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float inf = std::numeric_limits<float>::infinity();
    const PerspectiveCamera invalid[] = {
        {0, 1, 2}, {-1, 1, 2}, {glm::pi<float>(), 1, 2}, {4, 1, 2}, {nan, 1, 2}, {inf, 1, 2},
        {1, 0, 2}, {1, -1, 2}, {1, nan, 2}, {1, inf, 2},
        {1, 2, 2}, {1, 3, 2}, {1, 1, -2}, {1, 1, nan}, {1, 1, inf}
    };
    for (const auto camera : invalid)
    {
        *scene.tryGet<PerspectiveCamera>(entity) = camera;
        check(!buildCameraMatrices(scene, entity, 1), "Invalid lens parameters accepted");
    }
    *scene.tryGet<PerspectiveCamera>(entity) = {};
    for (const float aspect : {0.0f, -1.0f, nan, inf})
    {
        check(!buildCameraMatrices(scene, entity, aspect), "Invalid aspect accepted");
    }
    check(buildCameraMatrices(scene, entity, 1).has_value(), "Lens did not recover");
    check(buildCameraMatrices(scene, entity, static_cast<float>(std::numeric_limits<double>::epsilon())).has_value(),
        "Small positive aspect rejected despite representable result");
}

void singularTransformsAndRecovery()
{
    Scene scene;
    const auto parent = scene.create();
    const auto entity = cameraEntity(scene);
    check(scene.setParent(entity, parent), "Parent setup failed");
    LocalTransform pose;
    pose.scale = {0, 1, 1};
    check(scene.setLocalTransform(parent, pose) && scene.updateWorldTransforms() &&
        !buildCameraMatrices(scene, entity, 1), "Singular inherited world accepted");
    pose.scale.x = std::numeric_limits<float>::denorm_min();
    check(scene.setLocalTransform(parent, pose) && scene.updateWorldTransforms() &&
        !buildCameraMatrices(scene, entity, 1), "Unrepresentable inverse accepted");
    pose.scale = {0.5f, 1, 1};
    pose.translation.x = std::numeric_limits<float>::max();
    check(scene.setLocalTransform(parent, pose) && scene.updateWorldTransforms() &&
        !buildCameraMatrices(scene, entity, 1), "Inverse translation overflow accepted");
    check(scene.setLocalTransform(parent, {}) && scene.updateWorldTransforms() && buildCameraMatrices(scene, entity, 1),
        "Camera did not recover after transform correction");
}

void projectionNumericLimits()
{
    Scene scene;
    const auto entity = cameraEntity(scene, {1, 1.0e20f, 2.0e20f});
    check(scene.updateWorldTransforms(), "World update failed");
    const auto large = buildCameraMatrices(scene, entity, 1);
    check(large.has_value(), "Finite projection rejected due to intermediate overflow");
    near(ndc(large->projection, {0, 0, -1.0e20f}).z, 0);
    near(ndc(large->projection, {0, 0, -2.0e20f}).z, 1);
    auto* camera = scene.tryGet<PerspectiveCamera>(entity);
    *camera = {};
    camera->verticalFov = std::numeric_limits<float>::denorm_min();
    check(!buildCameraMatrices(scene, entity, 1), "Projection focal overflow accepted");
    *camera = {};
    check(!buildCameraMatrices(scene, entity, std::numeric_limits<float>::denorm_min()), "Aspect overflow accepted");
    camera->nearPlane = std::numeric_limits<float>::max() * 0.75f;
    camera->farPlane = std::numeric_limits<float>::max();
    check(!buildCameraMatrices(scene, entity, 1), "Depth coefficient overflow accepted");
    *camera = {};
    camera->verticalFov = std::nextafter(glm::pi<float>(), 0.0f);
    check(!buildCameraMatrices(scene, entity, std::numeric_limits<float>::max()), "Degenerate narrowed projection accepted");
    *camera = {1, 1, 2};
    LocalTransform pose;
    pose.translation.z = std::numeric_limits<float>::max() * 0.75f;
    check(scene.setLocalTransform(entity, pose) && scene.updateWorldTransforms() &&
        !buildCameraMatrices(scene, entity, 1), "Combined matrix overflow accepted");
}
}

int main()
{
    const struct { const char* name; void (*run)(); } cases[] = {
        {"identity, forward and zero-to-one depth", identityAndDepth},
        {"frustum edges, aspect and positive-height viewport", frustumAndViewport},
        {"translation, rotation and matrix order", translatedAndRotated},
        {"inherited camera pose, edits and detach", inheritedPoseAndReparent},
        {"full inverse with scale, reflection and shear", scaleAndShear},
        {"missing components, deletion and stale handles", missingComponentsAndHandles},
        {"value snapshots and component edits", snapshotsAndComponentEdits},
        {"invalid lens and aspect parameters", invalidParameters},
        {"singular worlds, inverse overflow and recovery", singularTransformsAndRecovery},
        {"projection intermediates and float limits", projectionNumericLimits}
    };
    size_t passed = 0;
    for (const auto& test : cases)
    {
        try { test.run(); ++passed; std::printf("Passed: %s\n", test.name); }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "Camera check failed (%s): %s\n", test.name, error.what());
            return 1;
        }
    }
    std::printf("Camera checks passed: %zu cases.\n", passed);
    return 0;
}
