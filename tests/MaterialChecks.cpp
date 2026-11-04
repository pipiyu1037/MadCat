#include "Assets/Gltf.h"
#include "RenderScene/RenderScene.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace
{
using namespace madcat;
using Bytes = std::vector<std::byte>;
std::filesystem::path output, models;
void check(bool value, const char* why) { if (!value) { throw std::runtime_error(why); } }
void near(float actual, float expected) { check(std::isfinite(actual) && std::abs(actual-expected) < .0001f,"Unexpected material value"); }
void replace(std::string& text, std::string_view from, std::string_view to)
{
    const auto at = text.find(from); check(at != text.npos,"Fixture replacement failed"); text.replace(at,from.size(),to);
}
void append(Bytes& bytes, uint32_t value) { for (int index=0; index<4; ++index) { bytes.push_back(static_cast<std::byte>((value>>(index*8))&255)); } }
Bytes png()
{
    constexpr std::string_view hex="89504e470d0a1a0a0000000d494844520000000200000002080600000072b60d240000001b49444154789c63706838f0ff8043030343c30187ff0a090bfe03004b5e089ef54cca780000000049454e44ae426082";
    Bytes bytes;
    constexpr std::string_view digits="0123456789abcdef";
    for (size_t i=0;i<hex.size();i+=2) { bytes.push_back(static_cast<std::byte>(digits.find(hex[i])*16+digits.find(hex[i+1]))); }
    return bytes;
}
void write(const std::filesystem::path& file, const Bytes& bytes)
{
    std::ofstream stream(file,std::ios::binary|std::ios::trunc); stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())); check(bool(stream),"Binary fixture write failed");
}
void write(const std::filesystem::path& file, const std::string& text)
{
    std::ofstream stream(file,std::ios::binary|std::ios::trunc); stream<<text; check(bool(stream),"JSON fixture write failed");
}
struct Fixture { std::string json; Bytes data; };
Fixture fixture()
{
    Fixture f;
    for (const float v : {0.f,0.f,0.f, 1.f,0.f,0.f, 0.f,1.f,0.f, 0.f,0.f, 1.f,0.f, 0.f,1.f}) { append(f.data,std::bit_cast<uint32_t>(v)); }
    const auto image=png(); f.data.insert(f.data.end(),image.begin(),image.end());
    f.json=R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":)"+std::to_string(f.data.size())+R"(}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":)"+std::to_string(image.size())+R"(}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"}],"images":[{"bufferView":2,"mimeType":"image/png"}],"samplers":[{"magFilter":9728,"minFilter":9985,"wrapS":33071,"wrapT":33648}],"textures":[{"source":0,"sampler":0}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.5,0.75,1,1],"baseColorTexture":{"index":0},"metallicFactor":0.2,"roughnessFactor":0.7},"alphaMode":"MASK","alphaCutoff":0.4,"doubleSided":true}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}]})";
    return f;
}
std::expected<assets::GltfGeometry,assets::GltfError> load(std::string_view name,const Fixture& f)
{
    const auto folder=output/name; std::filesystem::create_directories(folder);
    write(folder/"mesh.bin",f.data); write(folder/"model.gltf",f.json);
    return assets::importGltfGeometry(folder/"model.gltf");
}
void verifyImage(const assets::Image& image)
{
    const std::vector<uint8_t> expected{64,128,192,255,192,64,128,0,128,192,64,255,32,96,160,255};
    check(image.width==2 && image.height==2 && image.rgba==expected,"RGBA8 origin, alpha or encoding changed");
}
void embeddedAndOwned()
{
    const auto f=fixture(); auto result=load("owned",f);
    check(result.has_value(),"Embedded PNG import failed"); verifyImage(result->images.at(0));
    check(result->materials.size()==1 && result->textures.size()==1 && result->meshes[0].primitives[0].material==0,"Material/image references lost");
    const auto& m=result->materials[0]; near(m.baseColor[0],.5f); near(m.metallic,.2f); near(m.roughness,.7f); near(m.alphaCutoff,.4f);
    check(m.alphaMode==assets::AlphaMode::Mask && m.doubleSided && m.baseColorTexture==0,"Material properties lost");
    const auto& s=result->textures[0].sampler;
    check(s.magFilter==9728 && s.minFilter==9985 && s.wrapS==33071 && s.wrapT==33648,"Sampler properties lost");
    write(output/"owned"/"mesh.bin",Bytes{}); check(!assets::importGltfGeometry(output/"owned"/"model.gltf"),"Broken buffer accepted"); verifyImage(result->images[0]);
    check(load("owned",f).has_value(),"Retry failed");
    auto truncated=png(); truncated.resize(truncated.size()/2);
    check(!assets::decodeImage(truncated),"Truncated image was accepted");
}
void externalAndGlb()
{
    auto f=fixture(); replace(f.json,R"({"bufferView":2,"mimeType":"image/png"})",R"({"uri":"%E5%9B%BE%20%E7%89%87.png"})");
    const auto dir=output/"external"; std::filesystem::create_directories(dir); write(dir/std::filesystem::path(u8"图 片.png"),png());
    auto result=load("external",f); check(result.has_value(),"External Unicode PNG failed"); verifyImage(result->images[0]);
    f=fixture(); replace(f.json,R"("uri":"mesh.bin",)","");
    while(f.json.size()%4) { f.json+=' '; } while(f.data.size()%4) { f.data.push_back(std::byte{}); }
    Bytes glb; append(glb,0x46546c67); append(glb,2); append(glb,static_cast<uint32_t>(28+f.json.size()+f.data.size()));
    append(glb,static_cast<uint32_t>(f.json.size())); append(glb,0x4e4f534a); for(char c:f.json) { glb.push_back(static_cast<std::byte>(c)); }
    append(glb,static_cast<uint32_t>(f.data.size())); append(glb,0x004e4942); glb.insert(glb.end(),f.data.begin(),f.data.end());
    write(output/"image.glb",glb); result=assets::importGltfGeometry(output/"image.glb"); check(result.has_value(),"GLB image failed"); verifyImage(result->images[0]);
}
void defaultsAndBatch()
{
    auto f=fixture(); replace(f.json,R"("material":0)",R"("extras":{})");
    auto result=load("default-material",f); check(result && !result->meshes[0].primitives[0].material,"Default material was fabricated as source index");
    assets::ModelAssets assets; const auto id=assets.add(std::move(*result));
    const render::MeshDraw draw{{},{id,0,0},{}};
    const auto batch=render::prepareGeometry(assets,std::span(&draw,1)); check(batch && batch->materials.size()==1,"Default material packing failed");
    auto resolved=render::resolveDraws(*batch,std::span(&draw,1));
    check(resolved && (*resolved)[0].transform.baseColorAlpha==1 && (*resolved)[0].transform.normalFromLocal[3]==1 && (*resolved)[0].material==0,"Default white factor not packed");
    auto probe=assets::importGltfGeometry(models/"MaterialProbe.gltf");
    check(probe && probe->materials.size()==4 && probe->textures.size()==3 && probe->images.size()==3,"Material/image data URI import failed");
    verifyImage(probe->images[0]);
    const auto& jpeg=probe->images[2];
    check(jpeg.width==2 && jpeg.height==2 && jpeg.rgba.size()==16 &&
        std::abs(int(jpeg.rgba[0])-128)<=8 && std::abs(int(jpeg.rgba[1])-64)<=8 &&
        std::abs(int(jpeg.rgba[2])-32)<=8 && jpeg.rgba[3]==255,"JPEG RGB/alpha incorrect");
    for (bool missingUv : {false,true})
    {
        auto invalid=*probe;
        if (missingUv) { invalid.meshes[0].primitives[0].texcoords0.clear(); }
        else { invalid.materials[0].alphaMode=assets::AlphaMode::Blend; }
        const auto invalidId=assets.add(std::move(invalid));
        const render::MeshDraw badDraw{{},{invalidId,0,0},{}};
        check(!render::prepareGeometry(assets,std::span(&badDraw,1)),"Unsupported render material silently accepted");
    }
    const auto probeId=assets.add(std::move(*probe));
    std::vector<render::MeshDraw> draws;
    for(size_t p=0;p<4;++p) { draws.push_back({{}, {probeId,0,p},{}}); draws.push_back(draws.back()); }
    const auto packed=render::prepareGeometry(assets,draws); check(packed && packed->materials.size()==4 && packed->vertices.size()==12,"Materials or geometry duplicated by instances");
    resolved=render::resolveDraws(*packed,draws);
    check(resolved && (*resolved)[0].transform.attributes==(rhi::NormalTransformValid|rhi::HasTexcoord|rhi::AlphaMask|rhi::HasTexture),"Textured mask flags lost");
    near((*resolved)[0].transform.normalFromLocal[3],.5f); near((*resolved)[0].transform.normalFromLocal[7],.75f);
    check((*resolved)[6].transform.attributes==(rhi::NormalTransformValid|rhi::DoubleSided),"Untextured material requires UV or texture");
}

}

#ifdef _WIN32
int wmain(int argc,wchar_t** argv)
#else
int main(int argc,char** argv)
#endif
{
    try
    {
        check(argc==3,"Expected fixture output and model directory"); output=argv[1]; models=argv[2]; std::filesystem::create_directories(output);
        embeddedAndOwned(); externalAndGlb(); defaultsAndBatch();
        std::printf("Material/image checks: 3 groups passed.\n"); return 0;
    }
    catch(const std::exception& error) { std::fprintf(stderr,"Material/image checks failed: %s\n",error.what()); return 1; }
}
