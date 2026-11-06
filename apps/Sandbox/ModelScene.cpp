#include "ModelScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace madcat::sandbox
{
ModelScene::ModelScene(const assets::ModelAssets& assets, assets::ModelAssetId model)
    : assets_(assets), model_(model)
{
    for (size_t index = 0; index < parents_.size(); ++index)
    {
        parents_[index] = scene_.create();
        spawn(index);
    }
    // Stream the world-space bounds; no baked/duplicated vertex arrays. Both
    // instances still have identity parents and roots at this startup boundary.
    if (!scene_.updateWorldTransforms()) { throw std::runtime_error("Model bounds world update failed"); }
    const auto infinity = std::numeric_limits<double>::infinity();
    glm::dvec3 minimum(infinity), maximum(-infinity);
    scene_.each<scene::MeshInstance>([&](ecs::Entity entity, const auto& instance) {
        const auto world = glm::dmat4(*scene_.worldTransform(entity));
        for (const auto& primitive : assets_.find(instance.model)->meshes[instance.meshIndex].primitives)
        {
            for (const auto position : primitive.positions)
            {
                const auto point = glm::dvec3(world * glm::dvec4(position, 1));
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!std::isfinite(point[axis])) { throw std::runtime_error("Nonfinite model bounds"); }
                }
                minimum = glm::min(minimum, point); maximum = glm::max(maximum, point);
            }
        }
    });
    const auto extent = maximum - minimum;
    const auto largest = std::max({extent.x, extent.y, extent.z});
    if (!std::isfinite(largest) || largest <= 0) { throw std::runtime_error("Model requires finite nonzero bounds"); }
    const auto scale = 1.2 / largest;
    const auto center = minimum + extent * .5;
    fit_ = glm::mat4(static_cast<float>(scale));
    fit_[3] = glm::vec4(glm::vec3(-center * scale), 1);
    for (const auto root : roots_)
    {
        if (!scene_.setLocalMatrix(root, fit_)) { throw std::runtime_error("Invalid model fit matrix"); }
    }
    std::fprintf(stderr, "MadCat: model instance fit; center %.9g %.9g %.9g, scale %.9g.\n", center.x, center.y, center.z, scale);
    cameraParent_ = scene_.create();
    camera_ = scene_.create();
    lightParent_ = scene_.create();
    light_ = scene_.create();
    if (!scene_.setParent(light_, lightParent_) || !scene_.add<scene::DirectionalLight>(light_) || !selectLighting(0))
    {
        throw std::runtime_error("Could not initialize model light");
    }
    if (!scene_.setParent(camera_, cameraParent_) || !scene_.add<scene::PerspectiveCamera>(camera_) || !selectPose(0))
    {
        throw std::runtime_error("Could not initialize model camera");
    }
}

void ModelScene::spawn(size_t index)
{
    auto instance = scene::instantiateModel(scene_, assets_, model_, parents_[index]);
    if (!instance) { throw std::runtime_error(instance.error()); }
    roots_[index] = instance->root;
    if (!scene_.setLocalMatrix(roots_[index], fit_)) { throw std::runtime_error("Invalid model fit matrix"); }
}

bool ModelScene::selectPose(int pose)
{
    if (pose < 0 || pose > 9) { return false; }
    if (pose == 5)
    {
        for (size_t index = 0; index < roots_.size(); ++index)
        {
            if (!scene_.isAlive(roots_[index])) { spawn(index); }
        }
    }
    if (pose == 3 && scene_.isAlive(roots_[0]) && !scene_.destroySubtree(roots_[0])) { return false; }
    if (pose == 4 && scene_.isAlive(roots_[1]) && !scene_.destroySubtree(roots_[1])) { return false; }
    for (size_t index = 0; index < parents_.size(); ++index)
    {
        scene::LocalTransform parent;
        parent.translation.x = index == 0 ? -.75f : .75f;
        parent.scale = glm::vec3(.8f);
        if (pose == 9 && index == 0) { parent.scale.x = -parent.scale.x; }
        if (pose == 8 && index == 0)
        {
            parent.scale *= glm::vec3(1.6f, .6f, 1.2f);
            parent.rotation = glm::angleAxis(glm::radians(35.f), glm::vec3(0, 1, 0));
        }
        if ((pose == 1 && index == 0) || (pose == 7 && index == 1))
        {
            parent.translation += glm::vec3(index == 0 ? .15f : -.15f, .25f, -.25f);
            parent.rotation = glm::angleAxis(glm::radians(index == 0 ? 35.f : -35.f), glm::vec3(0, 1, 0));
        }
        if (!scene_.setLocalTransform(parents_[index], parent)) { return false; }
    }
    scene::LocalTransform rig, camera;
    if (pose == 2) { rig.translation = {.25f, .15f, .2f}; }
    camera.translation.z = 3;
    if (!scene_.setLocalTransform(cameraParent_, rig) || !scene_.setLocalTransform(camera_, camera)) { return false; }
    pose_ = pose;
    return true;
}

std::expected<std::vector<render::MeshDraw>, std::string> ModelScene::draws(float aspect)
{
    if (!scene_.worldTransformsCurrent() && !scene_.updateWorldTransforms()) { return std::unexpected("Model world update failed"); }
    auto result = render::extractDraws(scene_, assets_, camera_, aspect);
    if (result && pose_ == 6) { std::ranges::reverse(*result); }
    return result;
}

bool ModelScene::selectLighting(int preset)
{
    if (preset < 0 || preset > 2) { return false; }
    scene::LocalTransform parent, light;
    parent.rotation = glm::angleAxis(glm::radians(preset == 1 ? 55.f : -35.f), glm::vec3(0,1,0));
    light.rotation = glm::angleAxis(glm::radians(-25.f), glm::vec3(1,0,0));
    if (!scene_.setLocalTransform(lightParent_, parent) || !scene_.setLocalTransform(light_, light)) { return false; }
    auto* component = scene_.tryGet<scene::DirectionalLight>(light_);
    component->color = {1, .8f, .6f};
    component->irradiance = preset == 2 ? 0.f : glm::pi<float>();
    return true;
}

std::expected<rhi::FrameLighting, std::string> ModelScene::lighting()
{
    if (!scene_.worldTransformsCurrent() && !scene_.updateWorldTransforms()) { return std::unexpected("Light world update failed"); }
    return render::extractLighting(scene_);
}
}
