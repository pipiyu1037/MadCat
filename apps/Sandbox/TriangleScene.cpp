#include "TriangleScene.h"

#include "Scene/Camera.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace madcat::sandbox
{
TriangleScene::TriangleScene()
{
    modelParent_ = scene_.create();
    model_ = scene_.create();
    secondModel_ = scene_.create();
    cameraParent_ = scene_.create();
    camera_ = scene_.create();
    if (!scene_.setParent(model_, modelParent_) || !scene_.setParent(secondModel_, modelParent_) || !scene_.setParent(camera_, cameraParent_) ||
        !scene_.add<scene::PerspectiveCamera>(camera_) || !selectPose(TrianglePose::Default))
    {
        throw std::runtime_error("Could not initialize triangle Scene");
    }
}

bool TriangleScene::selectPose(TrianglePose pose)
{
    if (pose < TrianglePose::Default || pose > TrianglePose::DepthReverse) { return false; }
    scene::LocalTransform modelParent;
    modelParent.translation = {0.25f, 0.1f, 0};
    modelParent.rotation = glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 0, 1));
    if (pose == TrianglePose::ModelParent)
    {
        modelParent.translation += glm::vec3(0.6f, 0.15f, -0.3f);
        modelParent.rotation = glm::angleAxis(glm::radians(40.0f), glm::vec3(0, 0, 1));
    }
    scene::LocalTransform model;
    model.translation = {-0.1f, -0.05f, 0};
    model.rotation = glm::angleAxis(glm::radians(25.0f), glm::vec3(0, 1, 0));
    model.scale = {1.1f, 0.9f, 1};
    scene::LocalTransform secondModel;
    secondModel.translation = {0.2f, 0.05f, -0.1f};
    secondModel.rotation = glm::angleAxis(glm::radians(-35.0f), glm::vec3(0, 1, 0));
    secondModel.scale = model.scale;
    scene::LocalTransform cameraParent;
    cameraParent.translation = {-0.15f, 0.1f, 0};
    cameraParent.rotation = glm::angleAxis(glm::radians(5.0f), glm::vec3(0, 1, 0));
    if (pose == TrianglePose::CameraParent)
    {
        cameraParent.translation = {0.4f, 0.2f, 0.25f};
        cameraParent.rotation = glm::angleAxis(glm::radians(-8.0f), glm::vec3(0, 1, 0));
    }
    scene::LocalTransform camera;
    camera.translation = {0, 0, 3};
    auto* lens = scene_.tryGet<scene::PerspectiveCamera>(camera_);
    *lens = {glm::radians(60.0f), pose == TrianglePose::NearClipped ? 4.0f : 0.1f,
        pose == TrianglePose::FarClipped ? 2.0f : 20.0f};
    if (!scene_.setLocalTransform(modelParent_, modelParent) || !scene_.setLocalTransform(model_, model) ||
        !scene_.setLocalTransform(secondModel_, secondModel) || !scene_.setLocalTransform(cameraParent_, cameraParent) ||
        !scene_.setLocalTransform(camera_, camera)) { return false; }
    pose_ = pose;
    return true;
}

std::optional<TriangleDraws> TriangleScene::drawTransforms(float aspectRatio)
{
    if (!scene_.worldTransformsCurrent() && !scene_.updateWorldTransforms()) { return std::nullopt; }
    const auto camera = scene::buildCameraMatrices(scene_, camera_, aspectRatio);
    if (!camera) { return std::nullopt; }
    TriangleDraws result;
    result.count = pose_ == TrianglePose::DepthForward || pose_ == TrianglePose::DepthReverse ? 2 : 1;
    std::array models{model_, secondModel_};
    if (pose_ == TrianglePose::DepthReverse) { std::swap(models[0], models[1]); }
    for (size_t index = 0; index < result.count; ++index)
    {
        const auto* world = scene_.worldTransform(models[index]);
        if (!world) { return std::nullopt; }
        const auto matrix = glm::dmat4(camera->viewProjection) * glm::dmat4(*world);
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                const auto value = matrix[column][row];
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) { return std::nullopt; }
                result.transforms[index].clipFromLocal[column * 4 + row] = static_cast<float>(value);
            }
        }
    }
    return result;
}
}
