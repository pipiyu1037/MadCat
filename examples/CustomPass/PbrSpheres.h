#pragma once
#include "Assets/Gltf.h"
#include <cmath>
#include <numbers>

namespace experiment
{
// Original procedural reference scene: upper row dielectric, lower row metal;
// perceptual roughness increases from left to right. No external asset/license.
inline madcat::assets::GltfGeometry makePbrSpheres()
{
    using namespace madcat::assets;
    GltfGeometry model;
    constexpr uint32_t rings=48, segments=96;
    constexpr float roughness[]{0,.2f,.4f,.7f,1};
    for (uint32_t row=0; row<2; ++row) { for (uint32_t column=0; column<5; ++column) {
        Material material;
        material.name="metal="+std::to_string(row)+", rough="+std::to_string(roughness[column]);
        material.baseColor={.7f,.3f,.08f,1}; material.metallic=float(row); material.roughness=roughness[column];
        MeshPrimitive sphere;
        sphere.material=model.materials.size();
        for (uint32_t ring=0; ring<=rings; ++ring) { for (uint32_t segment=0; segment<=segments; ++segment) {
            const float theta=std::numbers::pi_v<float>*float(ring)/rings;
            const float phi=2*std::numbers::pi_v<float>*float(segment)/segments;
            const glm::vec3 normal{std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi)};
            sphere.normals.push_back(normal);
            sphere.positions.push_back(.22f*normal+glm::vec3((float(column)-2)*.52f,row==0 ? 0.f : -.6f,0));
        } }
        for (uint32_t ring=0; ring<rings; ++ring) { for (uint32_t segment=0; segment<segments; ++segment) {
            const uint32_t a=ring*(segments+1)+segment, b=a+segments+1;
            if (ring>0) { sphere.indices.insert(sphere.indices.end(),{a,a+1,b}); }
            if (ring+1<rings) { sphere.indices.insert(sphere.indices.end(),{a+1,b+1,b}); }
        } }
        ModelNode node; node.mesh=model.meshes.size();
        model.sceneRoots.push_back(model.nodes.size()); model.nodes.push_back(std::move(node));
        model.meshes.push_back({material.name,{std::move(sphere)}}); model.materials.push_back(std::move(material));
    } }
    return model;
}
}
