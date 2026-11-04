#include "Assets/Gltf.h"

#include <cstdio>
#include <exception>

namespace
{
int inspect(const std::filesystem::path& path)
{
    try
    {
        const auto imported = madcat::assets::importGltfGeometry(path);
        if (!imported)
        {
            const auto file = imported.error().file.u8string();
            std::fprintf(stderr, "glTF import failed: %s: %s\n", reinterpret_cast<const char*>(file.c_str()), imported.error().message.c_str());
            return 1;
        }
        size_t primitives = 0, vertices = 0, indices = 0, normals = 0, texcoords0 = 0;
        for (const auto& mesh : imported->meshes)
        {
            primitives += mesh.primitives.size();
            for (const auto& primitive : mesh.primitives)
            {
                vertices += primitive.positions.size(); indices += primitive.indices.size();
                normals += primitive.normals.size(); texcoords0 += primitive.texcoords0.size();
            }
        }
        std::printf("glTF geometry: %zu meshes, %zu primitives, %zu vertices, %zu indices, %zu nodes, %zu scene roots.\n",
            imported->meshes.size(), primitives, vertices, indices, imported->nodes.size(), imported->sceneRoots.size());
        std::printf("Vertex attributes: %zu normals, %zu TEXCOORD_0 values.\n", normals, texcoords0);
        std::printf("Materials/images: %zu materials, %zu images, %zu textures.\n", imported->materials.size(), imported->images.size(), imported->textures.size());
        for (size_t index = 0; index < imported->images.size(); ++index)
        {
            const auto& image = imported->images[index];
            std::printf("Image %zu: %u x %u, %zu RGBA8 bytes.\n", index, image.width, image.height, image.rgba.size());
        }
        return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "glTF import failed: %s\n", error.what()); return 1; }
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    if (argc != 2) { std::fprintf(stderr, "Usage: MadCatGltfInspect <model.gltf|model.glb>\n"); return 1; }
    return inspect(std::filesystem::path(argv[1]));
}
