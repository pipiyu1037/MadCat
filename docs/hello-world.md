# Hello World：用 C++ 写第一个 MadCat 渲染实验

这个示例展示用户如何通过引擎接口创建场景并得到一幅渲染结果：导入一个 glTF 模型，实例化为 Entity，创建相机与方向光，选择前向渲染图，交给引擎执行。

完整且已构建验证的程序位于 [examples/BasicScene/main.cpp](../examples/BasicScene/main.cpp)，对应的 [CMakeLists.txt](../examples/BasicScene/CMakeLists.txt)只登记示例及其资产路径并链接运行库。示例不依赖 Sandbox，也不包含 Vulkan 或 GLFW 初始化代码。

## 完整用户代码

下面是当前 BasicScene 的完整源码。先关注第 1、2、3 段以及最后的 `runtime::run` 调用；第 4 段是可选的场景更新示例。

```cpp
#include "Runtime/Application.h"
#include "Passes/ForwardPass.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    try
    {
        // 1. Load a CPU asset and instantiate its nodes as Scene entities.
        if (argc > 2) { throw std::runtime_error("Usage: BasicScene [model.gltf|model.glb]"); }
        const auto path = argc == 2 ? std::filesystem::path(argv[1]) :
            std::filesystem::path(u8"" MADCAT_EXAMPLE_ASSETS)/"models/Suzanne/Suzanne.gltf";
        auto imported = madcat::assets::importGltfGeometry(path);
        if (!imported) { throw std::runtime_error(imported.error().message); }
        madcat::assets::ModelAssets assets;
        const auto model = assets.add(std::move(*imported));
        madcat::scene::Scene scene;
        const auto instance = madcat::scene::instantiateModel(scene,assets,model);
        if (!instance) { throw std::runtime_error(instance.error()); }

        // 2. Set the model pose, then explicitly create the active camera/light.
        madcat::scene::LocalTransform modelPose;
        modelPose.scale = glm::vec3(.7f);
        const auto camera = scene.create();
        const auto light = scene.create();
        madcat::scene::LocalTransform cameraPose;
        cameraPose.translation.z = 3;
        if (!scene.setLocalTransform(instance->root,modelPose) ||
            !scene.add<madcat::scene::PerspectiveCamera>(camera) ||
            !scene.setLocalTransform(camera,cameraPose))
        { throw std::runtime_error("Model/camera setup failed"); }
        auto* sunlight = scene.add<madcat::scene::DirectionalLight>(light);
        if (!sunlight) { throw std::runtime_error("Light setup failed"); }
        sunlight->color = {1,.8f,.6f};

        // 3. The experiment chooses the C++ rendering graph. The existing
        // factory connects Clear -> Forward -> Present, including output.
        madcat::runtime::ApplicationOptions application;
        application.title = "MadCat BasicScene";
        application.graphFactory = madcat::passes::buildForwardGraph;

        // 4. Optional user updates: keys 1/2/3/4 restore, move the model,
        // move the camera, or turn off the light. Edits precede extraction.
        int pose = 0, applied = -1, logged = -1;
        madcat::runtime::SceneCallbacks callbacks;
        callbacks.keyPressed = [&](char key) {
            if (key >= '1' && key <= '4') { pose = key-'1'; }
        };
        callbacks.update = [&](const madcat::runtime::FrameInfo&) {
            if (pose == applied) { return; }
            modelPose.translation.x = pose == 1 ? .45f : 0.f;
            cameraPose.translation.x = pose == 2 ? .45f : 0.f;
            if (!scene.setLocalTransform(instance->root,modelPose) || !scene.setLocalTransform(camera,cameraPose))
            { throw std::runtime_error("Scene edit failed"); }
            scene.tryGet<madcat::scene::DirectionalLight>(light)->irradiance = pose == 3 ? 0.f : 3.14159265f;
            applied = pose;
        };
        callbacks.presented = [&] {
            if (logged != pose) { std::fprintf(stderr,"BasicScene: pose %d presented.\n",pose); logged = pose; }
        };

        // Blocking engine entry: world update, RenderScene extraction, GPU
        // preparation, window loop and retirement are shared engine work.
        return madcat::runtime::run(application,scene,assets,camera,callbacks);
    }
    catch (const std::exception& error) { std::fprintf(stderr,"BasicScene failed: %s\n",error.what()); }
    return EXIT_FAILURE;
}

```

## 1. 资产与实例分别创建

`importGltfGeometry(path)` 将文件转换为 CPU 模型；`assets.add(...)` 将其保存到资产库并返回 ModelAssetId。

`instantiateModel(scene, assets, model)` 在 Scene 中创建模型的 Entity 层级。返回的 `instance->root` 是整个实例的分组根，修改它的 Transform 就会影响整份模型。再次实例化同一资产可以得到独立实例，底层几何与材质通过资产标识共享。

因此，你创建的是自己的 Scene，模型是通过这个 Scene 的实体进入渲染流程的。替换模型时修改 `path`；修改大小和位置时编辑 `modelPose` 并调用 `setLocalTransform`。

## 2. 明确创建相机和灯光

`scene.create()` 创建 Entity，`scene.add<PerspectiveCamera>(camera)` 让它成为相机。示例把相机放在 `(0,0,3)`，默认朝局部 −Z 观察原点附近的模型。相机句柄在最后传给 run，明确指定本次实验的观察视角。

方向光也是一个 Entity。添加 `DirectionalLight` 后，组件保存颜色和辐照度，Entity 的世界朝向决定光照方向。示例保持其默认朝向，设置暖色光；没有环境光，所以关闭这个方向光后模型会变黑。

场景组件保存 CPU 数据。世界变换更新、绘制提取和 GPU 上传由引擎在运行入口内组织。

## 3. 选择渲染图并交出执行权

`application.graphFactory = passes::buildForwardGraph` 选择现有的前向渲染流程。该 [C++ 建图函数](../src/Passes/ForwardPass.h)连接 Clear → Forward → Present，并以 Present 的输入指定显示结果。

`runtime::run(application, scene, assets, camera, callbacks)` 开始阻塞运行：引擎创建窗口及 GPU 资源，按图绘制，并在关闭时完成资源退役后返回。返回 0 表示正常退出，非零表示失败。

Scene、资产库和回调捕获的变量都由用户代码持有，须在整个 run 调用期间有效。示例把它们声明在 main 中，生命周期覆盖这次调用。

## 4. 在用户更新入口中修改场景

`callbacks.keyPressed` 接收数字或大写字母按键。这里的 1～4 只改变用户变量 `pose`；`callbacks.update` 据此修改模型、相机和灯光。运行层随后更新世界 Transform、提取渲染数据并记录本帧工作。

你可以将这段更新改为自己的输入响应或动画逻辑。普通 Transform 和光照数值变化会进入下一帧，沿用已有的图和管线。

`callbacks.presented` 在呈现请求成功后打印状态，仅用于示例反馈；它不表示 GPU 已执行完成。静态实验可以使用不带 callbacks 参数的 run 调用，场景仍会正常绘制。

## 把它变成自己的实验

复制 `examples/BasicScene` 为自己的实验目录，修改目标名、用户源码和资产路径，并在根 CMakeLists 中登记该目录。核心构建关系只有：

```cmake
add_executable(MyExperiment main.cpp)
target_link_libraries(MyExperiment PRIVATE MadCatRuntime)
target_compile_definitions(MyExperiment PRIVATE MADCAT_EXAMPLE_ASSETS="${PROJECT_SOURCE_DIR}/assets")
```

`MADCAT_EXAMPLE_ASSETS` 为上面用户代码中的默认路径提供根目录。你也可以在用户代码中直接选择自己的资产路径。新的实验目标会继承运行库需要的 C++23 和公共依赖；生成器、输出目录及编译选项可参考示例的完整 CMakeLists。

完成 [README 的环境准备与 CMake 配置](../README.md#构建与运行)后，现有示例的配套检查命令为：

```powershell
cmake --build build --config Debug --target BasicScene MadCatShaders
.\build\Debug\BasicScene.exe
```

默认应看到一个使用材质贴图和方向光的 Suzanne 模型。也可将模型路径作为 BasicScene 的位置参数。源码中的按键 1／2／3／4 分别恢复场景、移动模型、移动相机和关灯。

## 本例的边界

当前 Scene 入口使用启动时固定的几何和材质，支持场景 Transform、相机和光照更新；加入启动集合之外的几何或更改资产材质需要重新启动。本例选择无投射阴影的 Lambert 前向渲染。下一篇 [CustomPass](custom-pass.md)已实现独立 Shader／受限图形管线准备、图内 HDR 与自定义显示 Pass；运行期 Shader 更新仍待实现。

本例已通过 Debug／Release 构建和解析平面场景的模型／相机／光照变化、resize／最小化恢复检查；默认 Suzanne 已通过可见性冒烟及截图检查。另一个复制示例的独立消费目标也完成了构建和相关窗口验证。具体范围与限制见[当前实现进度](project-development.md#当前实现进度)。
