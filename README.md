# MadCat

使用自研 ECS、Vulkan 和 Slang 的渲染引擎，目前可绘制彩色三角形，并通过 `--gltf` 显示共享 GPU 几何的双实例 glTF／GLB 场景。支持图内深度纹理与灰度查看、基础色材质／贴图、方向光漫反射、Scene／相机矩阵、深度遮挡、单帧同步、resize／最小化恢复，以及 Shader 手动编译、SPIR-V 验证和 Vulkan 资源的 RAII 管理。

文档：[开发范围、进度与验收](docs/project-development.md) · [架构设计](docs/architecture-design.md)。

## 构建与运行

需要 Windows 11、Visual Studio 2022 的 C++ 桌面开发工具、CMake 3.25 或更新版本、Git，以及 [Vulkan SDK](https://vulkan.lunarg.com/)（头文件版本至少 1.3.239，以提供 EXT maintenance1 类型；本机验证版本为 1.4.363.0）。运行时的实例及所选设备 API 仍要求 Vulkan 1.3。清屏还要求设备的 `VK_EXT_swapchain_maintenance1` 扩展和 `swapchainMaintenance1` feature，以及实例扩展 `VK_EXT_surface_maintenance1`、`VK_KHR_get_surface_capabilities2`。

安装 SDK 后重新打开 PowerShell，确认 `$env:VULKAN_SDK` 指向 SDK 安装目录。Debug 构建要求 `VK_LAYER_KHRONOS_validation` 可用；缺少验证层时会输出诊断并退出。

在能调用 CMake 和 Git 的 PowerShell 中，从项目根目录执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
cmake --build build --config Debug --target MadCatShaders
.\build\Debug\MadCat.exe
```

首次配置通过 CMake FetchContent 获取固定提交的 GLFW 3.4、GLM 1.0.1、cgltf 1.15 和 stb_image 2.30，需要访问 GitHub；依赖源码和构建产物位于 `build` 下。GLM 用于 CPU 场景与资产变换，锁定提交 `0af55ccecd98d4e5a8d1fad7de25ba429d60e863`，不启用其测试或编译库。cgltf 使用 MIT 许可，锁定提交 `360db1a95480fe102ae9c69b27c5d101167ff5ba`；stb 锁定提交 `d14d46f6b44456eb88beac58cba833a4135d4c89`，提供 MIT／公有领域双许可。二者只作为资产模块内部的解析与图片解码依赖。[stb 源码与许可](https://github.com/nothings/stb/tree/d14d46f6b44456eb88beac58cba833a4135d4c89)

程序创建一个 1280 × 720、标题为 MadCat 的可调整大小窗口，输出设备和 Surface 能力报告，选择设备、创建一个 Graphics／Compute／Present 队列，加载两个 Shader Module，再创建 Swapchain 和三角形管线。画面为深蓝背景上的插值彩色三角形：上方红色、左下蓝色、右下绿色。背景线性 RGBA 为 `(0.04, 0.08, 0.16, 1.0)`，本机 sRGB 格式下背景像素约为 RGB `(56, 80, 111)`。Debug 启用验证层，Release 不主动启用验证层。关闭前先等待 acquisition、GPU 提交和呈现资源退役，再释放帧渲染器、管线、Swapchain、两个 Shader Module、Device、Surface、Instance 和窗口；初始化或帧执行失败返回非零状态。

正常 Debug 启动日志包含以下内容，关闭时先输出帧渲染器释放及累计帧数，再依次输出 `Triangle pipeline destroyed.`、`Swapchain destroyed.`、两次 `Shader Module destroyed.`、`Device destroyed.`、`Surface destroyed.` 和 `Instance destroyed.`：

```text
Vulkan: instance-level API support <运行时版本>; requesting 1.3.0.
Vulkan: Instance created.
Vulkan: validation enabled (VK_LAYER_KHRONOS_validation); diagnostic callback ready.
Vulkan: window Surface created.
Vulkan: found <数量> physical device(s).
<每张设备的属性、队列族、Surface 能力、扩展、feature 和加速结构限制>
Vulkan: physical device capability report complete.
<各设备的筛选结果，以及选中设备与队列族>
Vulkan: Device created; queue family <索引>, queue 0 (Graphics/Compute/Present).
Vulkan: enabled VK_KHR_swapchain, dynamicRendering and synchronization2; ray tracing features remain disabled.
Vulkan: present fences enabled (VK_EXT_swapchain_maintenance1).
Vulkan: loading Shader Module from <构建目录>/shaders/triangle/vertex.spv.
Vulkan: Shader Module created; <路径>, <大小> bytes.
Vulkan: loading Shader Module from <构建目录>/shaders/triangle/fragment.spv.
Vulkan: Shader Module created; <路径>, <大小> bytes.
MadCat: triangle Shader Modules ready (vertexMain / fragmentMain).
Vulkan: Swapchain created; <宽> x <高>, format <格式>, color space <颜色空间>, FIFO, <实际数量> images (requested <请求数量>).
Vulkan: Swapchain usage <标志>, transform <标志>, composite alpha <标志>; exclusive sharing.
Vulkan: Triangle pipeline created; vertexMain / fragmentMain, color format <格式>, dynamic viewport/scissor.
Vulkan: Clear renderer ready; one frame in flight, dynamic rendering, acquire/submit/present fences.
Vulkan: triangle DrawIndexed enabled (9 uint32 indices, 1 instance).
MadCat: presentation ready; framebuffer <宽> x <高>, Swapchain <宽> x <高>.
Vulkan: Clear frame presented (1); linear RGBA 0.040 0.080 0.160 1.000.
```

窗口使用 `GLFW_NO_API`，渲染时通过 `glfwPollEvents()` 处理事件，最小化或零像素尺寸时通过 `glfwWaitEvents()` 等待恢复。Windows 拖动窗口边框时可能暂时停留在系统模态循环，因此窗口刷新回调也调用同一渲染入口；入口防止重入并捕获 C++ 异常，回调中不处理或等待 GLFW 事件。Vulkan 函数通过 GLFW 动态获取，CMake 使用 `Vulkan::Headers` 提供头文件。[GLFW 窗口刷新](https://www.glfw.org/docs/3.4/window_guide.html#window_refresh)、[CMake FindVulkan](https://cmake.org/cmake/help/latest/module/FindVulkan.html)

Debug 同步验证可按本机 SDK 的设置方式启用，运行后移除该环境变量：

```powershell
$env:VK_LAYER_VALIDATE_SYNC = '1'
.\build\Debug\MadCat.exe
Remove-Item Env:VK_LAYER_VALIDATE_SYNC
```

能力报告包含设备名称、类型、设备级 API 与驱动信息，队列族的 Graphics／Compute／Transfer 能力及对当前窗口的 Present 支持，以及基础渲染和光追相关扩展。通过 `vkGetPhysicalDeviceFeatures2` 分别查询 `dynamicRendering`、`synchronization2`、`bufferDeviceAddress`、`accelerationStructure` 和 `rayQuery`；已提升为核心的功能会按设备 API 版本判断，不要求同名扩展仍被列出。[Vulkan feature 查询](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceFeatures2.html)

对于支持当前窗口呈现的设备，报告 Surface 格式与颜色空间、present mode、图像数量和尺寸范围、数组层数，以及 usage／transform／composite alpha 标志。`maxImageCount=0` 表示没有 Surface 给出的数量上限，`currentExtent.width=UINT32_MAX` 表示尺寸由应用选择；这些信息不代表已创建交换链。常见枚举显示名称，其余保留原始数值。设备没有呈现队列时说明原因，跳过该设备的 Surface 能力查询并继续报告其他能力；格式或模式列表为空时说明交换链配置不可用。[Surface 能力查询前提](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceCapabilitiesKHR.html)

报告还输出加速结构限制，并检查计算 Ray Query 所需的 feature、Compute 队列和扩展依赖；缺失时说明原因，仍继续报告其他设备。没有物理设备、查询函数缺失或枚举失败时，程序报告错误并退出。

设备筛选要求设备级 Vulkan 1.3、`VK_KHR_swapchain`、可查询且支持的 `dynamicRendering`／`synchronization2`，以及非空的 Surface 格式和呈现模式、颜色附件 usage 和同一队列族的 Graphics／Compute／Present 支持。合格设备中按独显、集显、其他类型排序，同级保留枚举顺序，选择第一个兼容队列族。日志说明拒绝原因和最终选择；创建失败会报告操作、`VkResult` 与设备信息。基础初始化启用上述扩展和两项 feature，并按下文条件额外启用呈现 Fence；光追能力不作为硬性条件，也不启用光追 feature。Vulkan 1.3 的两项核心 feature 仍需显式启用，不要求同名 KHR 扩展列出。[Device 创建](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateDevice.html)、[Vulkan 1.3 feature 启用](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan13Features.html)

Swapchain 优先选择 `B8G8R8A8_SRGB`／`SRGB_NONLINEAR`，否则选第一个有具体格式的返回组合；使用 FIFO 和单队列独占共享。图像数量在 Surface 最小值上加一，并受非零最大值限制；`maxImageCount=0` 表示无该上限。优先使用 Surface 的固定尺寸；允许应用选择尺寸时，将 GLFW framebuffer 像素尺寸约束到支持范围。仅请求颜色附件 usage，使用当前 Surface transform，composite alpha 按 Opaque、Pre-multiplied、Post-multiplied、Inherit 顺序选择受支持项。配置与实际图像数量会记录到日志。[Surface 限制](https://docs.vulkan.org/refpages/latest/refpages/source/VkSurfaceCapabilitiesKHR.html)、[交换链创建参数](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html)

上述基础设备筛选和排序不变；实例依赖存在且设备支持时，额外查询并启用 EXT `swapchainMaintenance1`，日志说明呈现 Fence 是否可用。缺少此能力仍可查询并创建 Device／Swapchain，但清屏器拒绝初始化；当前不会为清屏能力另行选择设备，也未接入 KHR maintenance1 别名。本机 RTX 5060 Ti 支持，AMD 集显的当前驱动不支持该清屏路径。

背景清屏通过动态渲染颜色附件的 `loadOp=CLEAR`／`storeOp=STORE` 完成，随后在同一次渲染中绑定三角形管线、设置 viewport／scissor 并 Draw。图像 usage 仍仅为颜色附件。framebuffer 尺寸变化、从最小化恢复、acquire 返回 out-of-date 或 acquire／present 返回 suboptimal 时，应用先等待旧帧的 acquire／submit／present 完成，再依次销毁帧渲染器、管线和 Swapchain，重新查询 Surface 支持并按新尺寸、格式创建一组资源。suboptimal acquire 已获得的图像先完成提交与呈现，再请求重建。日志中的 `Clear frame presented` 保留原有名称，表示这一帧已经呈现，画面包含三角形。

最小化或零像素尺寸时不创建交换链、不获取图像；窗口恢复后自动继续，也可在最小化期间关闭。重建采用同步等待和先销毁后创建，暂不通过 `oldSwapchain` 复用资源；重建失败会清理部分资源、诊断并返回非零状态，不保留旧画面继续运行。Shader Module 独立于交换链重建；当前每次重建均重新创建管线，不做同格式复用或管线缓存。当前只支持单个 Graphics／Compute／Present 队列族，Ray Query GPU 求交尚未实现。

## Entity 生命周期检查

自研 ECS 已有独立的纯 CPU 库 `MadCatECS`，提供 [Entity](src/ECS/Entity.h) 值句柄与 [Registry](src/ECS/Registry.h) 的创建、销毁、存活检查、按类型保存的组件及[组合查询](#ecs-组合查询与遍历后修改)。[Scene](#scene-层级与-transform) 在其上管理场景层级与变换，已接入专用三角形绘制，通用场景提取仍待实现。

```cpp
#include "ECS/Registry.h"

madcat::ecs::Registry registry;
const auto first = registry.create();
const bool removed = registry.destroy(first);       // true
const auto second = registry.create();              // 复用索引，更换 generation
const bool oldAlive = registry.isAlive(first);      // false
const bool newAlive = registry.isAlive(second);     // true
```

句柄仅在创建它的 Registry 中有效；复制句柄不延长实体寿命，销毁实体会使全部旧副本失效。Registry 禁止复制／移动，当前单线程使用且不检测跨 Registry 误用。完整契约见[架构设计](docs/architecture-design.md#ecs-实体生命周期当前实现)。

配置沿用上面的构建入口。以下目标只构建 CPU 库与检查程序，不启动窗口，也不运行 Shader 工具或 GPU；根项目的首次配置仍要求已列出的 SDK／GLFW 依赖。

```powershell
cmake --build build --config Debug --target MadCatEntityChecks
cmake --build build --config Release --target MadCatEntityChecks
```

[检查程序](tests/EntityChecks.cpp) 的 8 个场景覆盖无效句柄、创建与复制、销毁、槽位复用、重复删除、扩容、混合增删和多代旧句柄拒绝；Debug／Release 使用同一组显式检查，不依赖 `assert`。

## ECS 组件使用与检查

每个 Entity 可以组合多种组件，每种类型至多一份；Registry 拥有组件，销毁 Entity 时自动清理其全部组件。以下是 CPU 数据示例，不依赖 Vulkan：

```cpp
#include "ECS/Registry.h"
#include <utility>

struct ExampleComponent { int value; };

madcat::ecs::Registry registry;
const auto entity = registry.create();
auto* component = registry.add<ExampleComponent>(entity, 7);
component->value = 9;
const auto* readOnly = std::as_const(registry).tryGet<ExampleComponent>(entity);
const int value = readOnly->value;                   // 9
const bool removed = registry.remove<ExampleComponent>(entity); // true
const bool destroyed = registry.destroy(entity);   // true；也会清理剩余的其他组件
```

`add<T>` 对无效／旧句柄和重复组件返回 `nullptr`，不构造新组件或覆盖旧值；构造或分配失败抛异常，不发布新组件。`tryGet<T>` 对无效句柄或缺失组件返回 `nullptr`，const Registry 返回只读指针；`remove<T>` 在这些情况下返回 false。显式移动的构造参数遵循其自身移动语义。

组件类型必须是无 const／volatile 限定、非数组的对象类型，支持不抛异常的移动构造、移动赋值和析构；不要求默认构造或复制。组件的构造、移动和析构不得重入所属 Registry。引用只供当前访问使用：同类型组件增删、带有该类型组件的实体销毁后重新获取，Registry 销毁后全部失效；其他类型组件的修改不影响本类型引用。具体存储与异常契约见[架构设计](docs/architecture-design.md#ecs-组件存储当前实现)。

```powershell
cmake --build build --config Debug --target MadCatComponentChecks MadCatEntityChecks
cmake --build build --config Release --target MadCatComponentChecks MadCatEntityChecks
```

[组件检查](tests/ComponentChecks.cpp) 的 13 个场景覆盖增删与 const 访问、重复和无效操作、删除搬移、实体复用、资源析构、扩容与稀疏空洞、类型隔离、构造失败恢复、别名参数和混合操作模型。检查不启动窗口或 GPU。

场景对象统一使用 Entity，通过 Scene 管理层级和 Transform，并组合用户组件；MeshInstance 引用资产，由 RenderScene 提取并驱动模型绘制。Material 组件与材质链路仍待实现。

## ECS 组合查询与遍历后修改

`each<A, B>(callback)` 遍历同时拥有这些组件的实体，将 Entity 值与按模板参数顺序排列的组件引用交给回调；至少指定一种组件类型，类型约束与 `add<T>` 相同。const Registry 提供 `const` 组件引用。缺失组件池或没有交集时不调用回调；每个匹配实体访问一次，遍历顺序不作保证，回调返回值被忽略。

以下完整 CPU 示例先更新字段并收集待删除的句柄，再在遍历结束后统一删除：

```cpp
#include "ECS/Registry.h"
#include <cstdio>
#include <utility>
#include <vector>

struct Position { int value; };
struct Velocity { int value; };

int main()
{
    madcat::ecs::Registry registry;
    const auto entity = registry.create();
    if (!registry.add<Position>(entity, 8) || !registry.add<Velocity>(entity, 2)) { return 1; }

    std::vector<madcat::ecs::Entity> pending;
    registry.each<Position, Velocity>([&](auto current, Position& position, Velocity& velocity)
    {
        position.value += velocity.value;
        if (position.value >= 10) { pending.push_back(current); }
    });
    std::as_const(registry).each<Position>([](auto, const Position& position)
    {
        std::printf("%d\n", position.value); // 10
    });
    for (const auto current : pending)
    {
        if (!registry.destroy(current)) { return 1; }
    }
    return 0;
}
```

查询期间允许修改组件字段、获取组件和嵌套查询；同一 Registry 的 `create()` 抛出 `std::logic_error`，`add<T>` 返回 `nullptr`，`remove<T>`／`destroy()` 返回 false，均不执行结构性修改，包括未参与查询的类型和实体。保护覆盖 const 别名及全部嵌套层级，其他 Registry 独立。增删请求不会自动排队，调用方保存 Entity 并在最外层查询结束后处理；不要保存组件引用作为延迟操作的目标。应用时仍检查句柄，期间已销毁或复用的旧句柄会被拒绝。

回调异常会向外传播并释放对应查询层级的保护；此前字段修改保留，不自动应用已收集的操作。回调不得销毁所属 Registry。const 引用遵循 C++ 自身语义，不保证组件内部指针指向的数据只读。完整契约见[架构设计](docs/architecture-design.md#ecs-查询与遍历边界当前实现)。

```powershell
cmake --build build --config Debug --target MadCatQueryChecks MadCatComponentChecks MadCatEntityChecks
cmake --build build --config Release --target MadCatQueryChecks MadCatComponentChecks MadCatEntityChecks
```

[查询检查](tests/QueryChecks.cpp) 包含 12 组纯 CPU 场景：单／多组件交集、空池与缺失池、字段更新、const 引用、结构性修改拒绝、嵌套与异常恢复、遍历后增删、槽位复用、独立 Registry、move-only 组件／回调和 1024 步操作模型。接口未提供过滤表达式、查询缓存或自动延迟命令队列。

## Scene 层级与 Transform

纯 CPU 库 `MadCatScene` 提供 [Scene](src/Scene/Scene.h)，独占一个 Registry。每个场景对象仍是 Entity，创建时具有单位局部变换和根节点关系。Scene 内部组件保存局部 TRS 或显式矩阵、父子关系及派生世界矩阵；通过受控接口写入，以保证父节点有效、无环及删除行为一致。Scene 不暴露可写 Registry。

下面是一个可链接 `MadCatScene` 的完整 CPU 示例，子节点继承父节点的平移：

```cpp
#include "Scene/Scene.h"
#include <cstdio>

int main()
{
    madcat::scene::Scene scene;
    const auto root = scene.create();
    const auto child = scene.create();
    madcat::scene::LocalTransform parentLocal;
    parentLocal.translation = {10, 0, 0};
    madcat::scene::LocalTransform childLocal;
    childLocal.translation = {2, 0, 0};
    if (!scene.setLocalTransform(root, parentLocal) || !scene.setLocalTransform(child, childLocal) ||
        !scene.setParent(child, root) || !scene.updateWorldTransforms()) { return 1; }
    const auto* world = scene.worldTransform(child);
    if (!world) { return 1; }
    std::printf("%g\n", static_cast<double>((*world)[3].x)); // 12
    return scene.destroySubtree(root) ? 0 : 1;
}
```

数学约定采用右手系、Y 向上、米、弧度和列向量，局部矩阵为 `T * R * S`，子世界矩阵为 `parentWorld * local`。四元数构造参数是 `(w, x, y, z)`，写入时归一化；[glTF 导入器](#cpu-gltf-几何导入)已显式转换其 `(x, y, z, w)` 数组顺序。世界矩阵保留剪切，不重新分解为 TRS。当前定义 `GLM_FORCE_DEPTH_ZERO_TO_ONE`；相机投影与正高度视口约定见下节，固定矩阵上传布局与 Sandbox 用法见[场景绘制](#scene-与相机绘制)。

`setParent(child, parent)` 保留局部变换，`setParent(child, {})` 解除父节点。无效／旧句柄、自引用和循环返回 false，重复设置同一父节点不重复添加子节点。`destroy()` 仅删除无子节点的实体；`destroySubtree()` 显式删除整个子树，先准备工作列表再按子先父后清理。创建、挂接和更新可能分配内存并抛异常，失败前保留仍有效的成员关系。

`setLocalTransform()` 拒绝非有限值和零四元数，保留原局部变换；允许零或负缩放。成功修改局部变换、层级或实体后，全部世界矩阵标为未更新；`worldTransform()` 返回 nullptr，直到查询外调用 `updateWorldTransforms()` 完整成功。矩阵组合溢出返回 false，不暴露部分更新结果；修正局部变换后可重试。

`setLocalMatrix()` 保存有限仿射矩阵，不进行 TRS 分解，可保留导入节点的剪切、反射和零缩放。矩阵模式下 `localTransform()` 返回 nullptr；调用 `setLocalTransform()` 显式切回 TRS。`localMatrix()` 对两种模式均返回按值拥有的局部矩阵快照，无效实体返回空 optional。切换表示后重新获取局部借用；`setParent()` 保留当前表示，两个局部 setter 都允许在组件查询中调用。

`add<T>`／`tryGet<T>`／`remove<T>` 和可变／const `each<T...>` 用于自定义数据组件，沿用 ECS 契约；`LocalTransform` 通过专用接口管理。查询中允许修改自定义组件字段及调用 `setLocalTransform()`，拒绝创建、删除、挂接和世界矩阵更新；const 别名和嵌套查询同样受保护。系统可先查询并写局部变换，查询结束后更新世界矩阵，再提取渲染数据。组件构造、移动和析构不得重入所属 Scene；查询回调不得销毁所属 Scene。

所有句柄仅限所属 Scene；跨 Scene 误用不检测。局部变换／世界矩阵指针、子节点 span 都是借用，实体创建／删除或层级修改后应重新获取；世界矩阵也应在每次更新后重新获取。已经取得的指针不会因未更新标记自动失效，调用方不得用它读取过期结果。

```powershell
cmake --build build --config Debug --target MadCatSceneChecks MadCatQueryChecks MadCatComponentChecks MadCatEntityChecks MadCat
cmake --build build --config Release --target MadCatSceneChecks MadCatQueryChecks MadCatComponentChecks MadCatEntityChecks MadCat
```

[场景检查](tests/SceneChecks.cpp) 覆盖局部 TRS、显式矩阵与表示切换、父先子后更新、剪切、挂接与环拒绝、叶节点／子树删除、句柄复用、无效输入、溢出恢复、系统查询边界、2048 层深度及 512 步层级参考模型。Scene／相机已接入 Sandbox 的专用三角形场景；glTF 的 CPU 实例化见[模型资产与 Scene 实例化](#模型资产与-scene-实例化)，最小 RenderScene 已接通，场景版本待实现，不提供保持世界位置的重新挂接、dirty 增量更新或自动命令队列。

## 相机与视图投影

`MadCatScene` 同时提供 [PerspectiveCamera 与矩阵提取](src/Scene/Camera.h)。将组件添加到 Entity，相机位置与朝向由 Scene 世界变换决定，局部 −Z 为前方、+Y 为上方。默认垂直视野角为 60°（字段使用弧度），近远裁剪面为 0.1／1000；宽高比由调用方根据输出像素尺寸提供，尺寸变化后重新计算，零尺寸时暂停提取。

以下完整 CPU 示例把相机放在 `(0, 0, 5)`，输出世界原点的投影深度 `0.9`：

```cpp
#include "Scene/Camera.h"
#include "Scene/Scene.h"
#include <cstdio>

int main()
{
    madcat::scene::Scene scene;
    const auto entity = scene.create();
    const madcat::scene::PerspectiveCamera camera{glm::radians(60.0f), 1.0f, 9.0f};
    if (!scene.add<madcat::scene::PerspectiveCamera>(entity, camera)) { return 1; }
    madcat::scene::LocalTransform pose;
    pose.translation = {0, 0, 5};
    if (!scene.setLocalTransform(entity, pose) || !scene.updateWorldTransforms()) { return 1; }
    const auto matrices = madcat::scene::buildCameraMatrices(scene, entity, 1280.0f / 720.0f);
    if (!matrices) { return 1; }
    const auto clip = matrices->viewProjection * glm::vec4(0, 0, 0, 1);
    std::printf("%.1f\n", static_cast<double>(clip.z / clip.w)); // 0.9
    return 0;
}
```

- `buildCameraMatrices(const Scene&, Entity, aspectRatio)` 只读、无分配，返回拥有自身矩阵值的 `std::optional<CameraMatrices>`，包括 `view`、`projection` 和 `viewProjection`。不会自动更新 Scene；场景修改后先更新世界矩阵，再重新提取，旧返回值不会跟踪修改。相机参数可通过组件字段编辑，每次提取重新校验，不依赖缓存。
- `view = inverse(world)`，完整保留父级缩放与剪切的影响，不分解或剥离缩放。普通相机及祖先建议使用单位缩放，避免视野形变；零缩放等不可逆世界变换会失败。
- 投影使用右手系、有限近远平面、普通深度（近处 0、远处 1）。`projection * view * worldPosition` 得到裁剪空间位置，再除以 w。投影矩阵已翻转 Y，以配合当前 Vulkan 的正高度 viewport；后续视口或 Shader 不应重复翻转。该翻转也影响图元绕序，接入剔除时须验证。
- 缺少相机、无效／旧句柄、世界矩阵未更新、非有限参数、`FOV` 不在 `(0, pi)`、宽高比不大于 0、裁剪面不满足 `0 < near < far`，以及逆矩阵／组合矩阵溢出或缩窄为 float 后退化，均返回 `nullopt`。内部使用 double 中间结果，输出仍为 float，不保证极端病态变换的精度。

```powershell
cmake --build build --config Debug --target MadCatCameraChecks
cmake --build build --config Release --target MadCatCameraChecks
```

[相机检查](tests/CameraChecks.cpp) 共 10 组，覆盖视锥边缘、深度、宽高比、正高度视口映射、位姿与父子继承、完整逆矩阵、句柄与快照、无效输入及数值边界。相机组件继续仅保存 CPU 场景事实；Sandbox 从它派生绘制矩阵并传给 Shader，见下节。目前没有自由移动控制器、正交投影或 reversed-Z。

## Scene 与相机绘制

[TriangleScene](apps/Sandbox/TriangleScene.h) 是专用 Sandbox 示例：一个模型父节点、两个模型实体、相机父节点和相机实体都由 Scene 管理，相机组件挂在相机实体上；GPU 顶点仍由应用持有，不进入 ECS。示例显式选择模型和相机，没有实现通用 RenderScene、Mesh／Material 资产提取或场景编辑器。

窗口中的按键用于切换确定的场景姿态：

| 按键 | 画面行为 |
|---|---|
| 1 | 默认姿态，恢复模型、相机和裁剪参数 |
| 2 | 改变模型父节点的平移／旋转，子模型随之变化 |
| 3 | 改变相机父节点的平移／旋转，观察位置随之变化 |
| 4 | 近裁剪面改为 4，整个三角形位于近裁剪面之前，仅显示背景 |
| 5 | 远裁剪面改为 2，整个三角形位于远裁剪面之后，仅显示背景 |
| 6 | 绘制两个相交三角形，逐像素由深度决定可见颜色 |
| 7 | 交换两个三角形的绘制顺序，遮挡结果应与 6 相同 |

示例默认相机视野角为 60°、近远裁剪面为 0.1／20。每帧先应用姿态请求并更新过期世界矩阵，再根据实际 Swapchain 宽高比提取相机，计算 `clipFromLocal = projection * view * modelWorld`。模型局部平面 z 为 0，模型的 Y 旋转使各顶点在相机空间具有不同深度。窗口 resize 重新计算投影，不再把裁剪空间三角形按窗口比例拉伸；相机投影负责唯一一次 Y 翻转。

[DrawTransform](src/RHI/DrawTransform.h) 在 offset 0 保存列主序 16 个 float，裁剪矩阵占 64 字节，整个参数块大小 128、对齐 16；提取逐列逐元素写入，避免依赖 GLM 对象 ABI。管线声明 Vertex 阶段 offset 0／size 128 的 Push Constant（附加世界法线列、可视化模式和属性标志，见下文），每次录制在 Draw 前复制快照。Shader 使用显式 `column_major` 和 `mul(matrix, position)`；锁定 Slang 的 SPIR-V 为 `RowMajor`／MatrixStride 16 与 `VectorTimesMatrix` 的组合。该组合已通过独立投影与实际像素核对，不能仅凭装饰名称要求主机转置。[Slang 矩阵布局](https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/a1-01-matrix-layout.html)

姿态变化只更新命令中的参数，复用顶点缓冲和管线。参数按值录入命令，CPU 临时对象不必存活到 GPU 完成；原有单帧等待继续保护命令资源复用。非法宽高比／提取失败会报告错误，非有限矩阵在调用 Vulkan 录制函数前拒绝；没有可变 GPU 上传内存或 Descriptor。[Push Constants 录制方式](https://docs.vulkan.org/guide/latest/push_constants.html)

```powershell
cmake --build build --config Debug --target MadCatSceneDrawChecks MadCatTriangleChecks MadCatShaders MadCat
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario scene
```

[CPU 场景绘制检查](tests/TriangleSceneChecks.cpp) 用独立逐点解析变换验证七种姿态、不同宽高比、矩阵打包、近远裁剪、快照、错误恢复和相反绘制顺序。`drawTransforms()` 返回拥有至多两个矩阵值的快照，renderer 在同一次动态渲染中按顺序 Draw；两个模型共享同一顶点缓冲。窗口脚本独立计算投影、透视校正颜色及屏幕空间深度，检查重复切换和姿态改变不重建管线。尚无自由相机控制；实际绘制已接入下文最小 Pass／Render Graph，glTF 静态预览见下节。

## 深度附件与遮挡

[TextureRef](src/RHI/Texture.h) 是图像、视图和内存的强引用；当前仅有 [纯深度纹理创建入口](src/RHI/Vulkan/VulkanDepthTexture.h)。它查询实际格式特性以及 2D／optimal tiling／depth attachment 用途的图像尺寸与单采样能力，优先 D32_SFLOAT，回退 D16_UNORM，再按 memoryTypeBits 选择 DEVICE_LOCAL 内存进行专用分配。创建后布局为 UNDEFINED；失败返回空引用并清理部分资源，CPU 分配异常可传播。

每份呈现对象拥有与 Swapchain 实际像素尺寸一致的深度纹理，帧上下文也保留引用。当前只有一个在途帧，因此各交换链图像可以共享这一份深度附件；增加在途帧前须重做分配与同步安排。重建时先等待 GPU／WSI、释放命令与管线，再释放深度视图、图像、内存，随后重建匹配新尺寸的资源；Device 覆盖所有引用的寿命。

每帧深度清除为 1，启用深度写入和 LESS 比较，沿用近处 0／远处 1 的投影。首次使用从 UNDEFINED 转为 DEPTH_STENCIL_ATTACHMENT_OPTIMAL；后续显式同步 Early／Late Fragment Tests 中前次深度写入与本次读写。动态渲染绑定深度视图并声明与管线一致的格式，使用 CLEAR／DONT_CARE；此附件只服务本次遮挡，不保留可供后续查看的深度内容。[Khronos 深度机制](https://docs.vulkan.org/guide/latest/depth.html)

```powershell
cmake --build build --config Debug --target MadCatDepthChecks MadCatTriangleChecks MadCatSceneDrawChecks MadCat
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario depth
```

`MadCatDepthChecks` 包含 26 项模拟检查，覆盖 D16 回退、图像能力／内存类型、缺失函数、查询／创建／分配／绑定／视图失败、重试、引用和释放顺序。深度窗口场景在重叠区寻找两个采样点，分别由不同三角形位于前方，按最近 NDC 深度计算颜色，检查顺序反转、逐帧清除、resize 和恢复。实际 Debug／Release 场景通过，本机为 RTX 5060 Ti／D32_SFLOAT；D16 回退尚未在真实驱动场景单独验收。当前无 stencil、MSAA 或深度采样／查看器；图已接管颜色／深度附件的同步，见下文最小图说明。

## CPU glTF 几何导入

`MadCatAssets` 提供 [importGltfGeometry](src/Assets/Gltf.h)，将 glTF 2.0／GLB 复制为独立拥有的 CPU 几何与静态节点数据。单独的[命令行工具](apps/GltfInspect/main.cpp)可以检查资产，无需启动窗口。先按上文配置项目，再执行：

```powershell
cmake --build build --config Debug --target MadCatGltfChecks MadCatGltfInspect
cmake --build build --config Release --target MadCatGltfChecks MadCatGltfInspect
.\build\Debug\MadCatGltfInspect.exe build\verification\gltf\fixtures\index-5123\model.gltf
.\build\Release\MadCatGltfInspect.exe build\_deps\cgltf-src\fuzz\data\Box.glb
.\build\Debug\MadCatGltfInspect.exe assets\models\Suzanne\Suzanne.gltf
```

第一条检查目标生成自己的测试资产。前两个命令行示例分别输出 3 个顶点／3 个索引，以及 Box 的 24 个顶点／36 个索引／两个节点。工具另行输出法线和 TEXCOORD_0 的值数量：Box 为 24／0，Suzanne 为 11808／11808。工具接受一个文件路径；成功返回 0，文件、格式或导入失败返回 1 并输出路径及诊断。Windows 入口支持中文路径。CPU 目标不链接 Vulkan、GLFW、ECS 或 Scene，但根项目首次配置仍需要上文列出的 SDK。

| 数据 | 当前支持与行为 |
|---|---|
| 文件与缓冲 | `.gltf`、GLB BIN、本地相对外部缓冲、base64 data URI；支持交错顶点、中文路径与 URI 百分号解码；不加载远程 URI |
| 几何 | 静态 TRIANGLES，FLOAT VEC3 POSITION；可选 u8／u16／u32 索引，统一复制成 uint32；无索引时顺序生成；保留多个 Mesh 和 Primitive |
| 法线 | 可选非 normalized FLOAT VEC3 NORMAL，复制至 `MeshPrimitive::normals`；要求有限值且长度与 1 的差小于 `1e-4`，保留源值及 Mesh 局部空间，不自动归一化或应用节点矩阵 |
| UV | 可选 VEC2 TEXCOORD_0，支持非 normalized FLOAT 或 normalized u8／u16，统一复制至 `MeshPrimitive::texcoords0` 的 float2；保留方向及范围，不翻转或裁剪 |
| 可选属性 | 缺失法线／UV 时相应向量为空，不自动生成；存在时数量须与 POSITION 一致；拒绝重复属性、错误格式、非有限值和不满足四字节顶点对齐的布局 |
| 节点 | 名称、父子索引、共享 Mesh 索引、局部及世界矩阵；右手／Y 向上／米／列向量，`world = parent * local`，TRS 为 `T * R * S`，显式转换 glTF 四元数 xyzw 顺序 |
| 场景 | 导入全部 Mesh／节点并保留原顺序；`sceneRoots` 选默认场景，否则首个场景，没有场景时取所有无父节点 |
| 错误 | `std::expected<GltfGeometry, GltfError>`，失败不返回部分模型；诊断包含文件与失败上下文；CPU 内存分配失败可能抛异常 |

返回的向量与矩阵在解析器释放、输入文件关闭后仍然有效，不含 cgltf 或 Vulkan 对象。位置保留在 Mesh 局部空间，多节点引用同一 Mesh，不复制实例顶点。TRS 要求有限值与单位四元数，允许零／负缩放；显式矩阵要求有限仿射矩阵，保留完整世界矩阵及继承剪切，不做 TRS 分解或完整 glTF 合规验证。

属性格式依据 [glTF 2.0 网格规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes)。当前忽略其他顶点属性（含额外 UV 集）、相机、灯光和 extras；基础材质、PNG／JPEG 与采样器支持见下文；明确拒绝必需扩展、稀疏 accessor、压缩、蒙皮、动画、形变目标与 GPU instancing。[19 组 CPU 检查](tests/GltfChecks.cpp)覆盖三种索引、无索引、法线／三种 UV 编码、分离／交错布局、缺失属性、GLB／base64、所有权与重试、属性格式／数量／有限值／法线长度／对齐错误，以及既有节点、层级和不支持项；Debug／Release 均已通过。真实 Suzanne、Box.glb 和不含可选属性的 TransformProbe 导入通过。接口边界见[资产导入契约](docs/architecture-design.md#cpu-gltf-几何当前实现)。

CPU 导入结果通过下节接口创建保留层级的 Scene 实例，再由 RenderScene 提取并共享 GPU 几何；资产模块本身继续只负责 CPU 数据。法线／UV 已保存到 CPU 模型，GPU 法线／UV 可视化及基础色纹理采样见下文。

## 模型资产与 Scene 实例化

纯 CPU 库 `MadCatSceneModels` 连接 [ModelAssets](src/Assets/ModelAssets.h) 与 [Scene 模型实例化](src/Scene/Model.h)。调用方导入一次资产，再创建多个实例；Mesh 几何留在资产库，每个 Entity 只保存 `ModelNode` 源节点标识和可选的 `MeshInstance` 模型 ID／Mesh 索引，不保存 Vulkan 对象。

```cpp
#include "Scene/Model.h"

madcat::assets::ModelAssets models;
auto imported = madcat::assets::importGltfGeometry("assets/models/Suzanne/Suzanne.gltf");
if (!imported) { return 1; }
const auto id = models.add(std::move(*imported));
madcat::scene::Scene scene;
auto first = madcat::scene::instantiateModel(scene, models, id);
auto second = madcat::scene::instantiateModel(scene, models, id);
if (!first || !second) { return 1; }
madcat::scene::LocalTransform placement;
placement.translation = {2, 0, 0};
if (!scene.setLocalTransform(first->root, placement) || !scene.updateWorldTransforms()) { return 1; }
// Two independent Entity hierarchies now reference the same immutable Mesh.
if (!scene.destroySubtree(first->root)) { return 1; }
// second remains live; update worlds again before reading it.
if (!scene.updateWorldTransforms()) { return 1; }
```

`ModelAssets::add` 按值接收 CPU 资产，左值复制、右值转移；`find(id)` 只返回 const 借用。首版仅追加，不卸载、不替换、不按路径去重；ID 和借用指针在后续添加时保持有效，资产库销毁后失效。ID 只限所属库，跨库误用不检测；资产库应覆盖组件 ID 的解析使用期。添加本身不重复校验全部几何，通常传入已成功导入的结果。

`instantiateModel(scene, models, id, parent = {})` 返回 expected。每个实例增加一个单位变换的分组根 Entity，源场景根挂在它下面；可将分组根挂到已有父 Entity，并通过根的 TRS 独立放置实例。返回的 `nodes` 向量按源节点索引映射，未选中节点为默认 Entity。只实例化选中场景可达节点，保留父子顺序和完整局部矩阵，世界矩阵由 Scene 重新派生，不复制导入时的世界缓存；不会居中、缩放或修改资产。

无效资产／父实体、活动查询、空选中场景、重复节点／环、多父关系、不一致链接、越界 Mesh 或非法局部矩阵返回错误；分配异常在清理本次新建节点后传播。已有实体与层级保留，但世界缓存可能失效，结构变化后的借用应重新获取。成功后也须在查询外调用 `updateWorldTransforms()`。返回值只持有句柄，不自动删除实例；`destroySubtree(root)` 删除当前仍在根下面的节点，重挂到根外的节点遵循普通 Scene 生命周期，映射不会自动更新。

```powershell
cmake --build build --config Debug --target MadCatModelSceneChecks MadCatSceneChecks
cmake --build build --config Release --target MadCatModelSceneChecks MadCatSceneChecks
```

[模型实例检查](tests/ModelSceneChecks.cpp) 使用原样下载的 Suzanne 验证两个实例独立变换、删除隔离及几何共享，用 TransformProbe 验证多 Primitive、父子继承与未选中隔离，并覆盖多根、外部父节点、矩阵剪切／零缩放、查询保护、错误恢复及 2048 层遍历。MSVC Debug 运行 8 组行为检查，Release 另运行完整分配失败检查（共 9 组、30 个分配失败位置）；Debug 的 STL `vector` 移动会分配调试代理，故不在该模式注入全局分配失败。既有 Scene／Camera／场景矩阵绘制／glTF 导入和静态转换检查一并回归。

窗口 `--gltf` 已消费这些实例及 MeshInstance，见 [RenderScene 与共享几何](#renderscene-与共享几何)；法线／UV 和基础色材质贴图已接入 GPU，光照继续分步接入。

## glTF 静态模型预览

早期 [StaticModel](apps/Sandbox/StaticModel.h) 将节点矩阵烘焙到顶点并复制实例几何；它及独立 CPU 检查保留作历史验证工具，当前窗口已不再调用。原始实现与验收记录见[架构契约](docs/architecture-design.md#gltf-静态预览当前实现)，当前运行方式见下节。

## RenderScene 与共享几何

`--gltf` 加载静态 glTF／GLB，创建两个独立模型实例；每个选中节点都是 Scene Entity，MeshInstance 保存资产引用。无参数仍运行原三角形示例。首次运行先准备匹配当前顶点／常量布局的 Shader；从旧版本更新后应先关闭程序并手动重新编译。

```powershell
cmake --build build --config Debug --target MadCat MadCatShaders MadCatRenderSceneChecks
.\build\Debug\MadCat.exe --gltf assets\models\Suzanne\Suzanne.gltf
```

[RenderScene](src/RenderScene/RenderScene.h) 从 MeshInstance 和已更新世界矩阵生成逐 Primitive 的值快照，再按资产／Mesh／Primitive 标识解析共享索引范围。每份 Primitive 只打包一次并保留 Mesh 局部坐标；多个节点和模型实例分别提交矩阵与 DrawIndexed，共享同一 GPU 几何。这是共享几何的多次绘制，尚未合并为硬件实例化 Draw。

[ModelScene](apps/Sandbox/ModelScene.h) 负责示例组装、相机和按键。启动时流式计算选中节点的世界 AABB，居中／最大边缩放到 1.2 的矩阵放在实例根；外层父节点各缩放 0.8 并移到 x = ±0.75，不烘焙或复制顶点。模型模式按键如下：

| 按键 | 行为 |
|---|---|
| 1 | 默认父节点／相机姿态；已删除实例保持删除 |
| 2 | 移动并旋转第一个实例，第二个保持原样 |
| 3 | 移动相机父节点 |
| 4 | 删除第一个实例子树 |
| 5 | 删除第二个实例子树；全删后仅清屏 |
| 6 | 恢复缺失实例并重置姿态，复用已上传几何 |
| 7 | 默认姿态，反向提交现存绘制 |
| 8 | 移动并旋转第二个实例，第一个保持原样 |
| 9 | 第一个实例旋转 35°，相对默认缩放为 `(1.6, .6, 1.2)`，第二个保持原样 |

上传使用 44 字节 ColoredVertex 和 uint32 索引，位置／诊断色／局部法线／UV 的 offset 为 0／12／24／36。诊断色来自每份 Primitive 的局部 AABB；模型默认显示基础色材质与贴图，C 切诊断色，N／U 使用真实法线／UV。唯一几何总顶点数最多 0x01000000，索引数须可由 uint32 表示，上传另受设备限制。TransformProbe 两个模型实例共 8 次绘制，上传仅 6 顶点／6 索引；两个 Suzanne 共 2 次绘制，上传仅 11808 顶点／11808 索引。

CPU 导入、实例化、边界和打包在窗口／Vulkan 创建前检查。每帧先应用编辑、更新世界矩阵，再提取快照；过期矩阵、非法相机／资产、矩阵溢出或未上传几何引用会失败并诊断。当前几何集合在启动时固定；新增几何需显式重建批次和上传，尚无运行期资产卸载／替换。

应用、管线和帧上下文保留不可变缓冲；删除 Entity 只移除后续绘制，已有 GPU 使用保持有效。resize 复用几何；帧上下文等待 acquire／submit／present 完成并释放命令后才放开引用。范围和有限矩阵在获取交换链图像前校验，空绘制列表合法。

```powershell
cmake --build build --config Release --target MadCat MadCatRenderSceneChecks MadCatTriangleChecks
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario model
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Release -Scenario model -ModelPath assets/models/Suzanne/Suzanne.gltf
```

[CPU 检查](tests/RenderSceneChecks.cpp) 的 8 组覆盖层级、快照、失效／溢出、几何校验、资产标识隔离，以及真实 Suzanne／TransformProbe 的独立编辑、删除与恢复。管线模拟新增索引子范围与越界检查。两种配置的模型窗口检查覆盖两种资产、独立变换、删除至空场景、恢复、反序、resize、最小化恢复和安全退出；TransformProbe 每帧最多 38 个独立解析像素，Suzanne 为左右实例可见性冒烟。Debug 开启同步验证，无 warning／error；Release 未主动启用验证层。

基础色材质／贴图、方向光与最小 Pass／Render Graph 已实现；通用管线准备和运行期 Shader 更新仍待实现，F03 尚未整体验收。

## 法线与 UV 可视化

在模型窗口按 `M` 显示基础色材质（模型默认）、`C` 显示位置诊断色、`N` 显示世界法线、`U` 显示 UV；按 `9` 检查第一个实例的非均匀缩放与旋转。按键只更改后续 Draw 常量，不重建管线或重传几何。

世界法线使用 `transpose(inverse(mat3(world)))`，在片元透视插值后归一化并以 `normal * .5 + .5` 映射到 RGB，结果不依赖相机方向。UV 使用 `RG = frac(uv)`、B = 0；负值与大于 1 的坐标在显示时重复，CPU／GPU 原始 UV 不裁剪或翻转。缺失所选属性、奇异世界变换导致法线不可用，或片元法线长度为零时显示洋红色；诊断色与 UV 模式仍可绘制奇异变换下的有效几何。N／U 是诊断显示；M 模式的纹理采样见下一节，当前仍无自动法线生成。

[DrawTransform](src/RHI/DrawTransform.h) 是 128 字节值快照：裁剪矩阵占 offset 0～63，三个法线列按 float4 放在 64／80／96，模式与标志在 112／116，三个法线列的 w 分量存储基础色 RGB，基础色 alpha／裁剪阈值在 120／124。法线矩阵统一除以一个正的最大绝对分量，避免直接上传极大逆缩放；这一公共比例在片元归一化后消失。RenderScene 按 Primitive 传递法线／UV 存在标志；renderer 在 acquire 前检查全部浮点常量、模式及标志。[Vulkan Push Constant 范围](https://docs.vulkan.org/refpages/latest/refpages/source/VkPushConstantRange.html)

```powershell
cmake --build build --config Debug --target MadCatShaders MadCat MadCatRenderSceneChecks MadCatTriangleChecks
.\build\Debug\MadCat.exe --gltf assets/models/AttributeProbe.gltf
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario attributes
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Release -Scenario attributes
```

[AttributeProbe](assets/models/README.md#法线与-uv-参考资产) 是原创内嵌资产，一个 Primitive 有法线／UV，另一个两者均缺失。[像素参考](tests/AttributePixelReference.ps1) 独立计算投影、透视插值、逆缩放／旋转、片元归一化与 sRGB；每帧 22 点同时覆盖有效属性和缺失提示。Debug／Release 检查均通过，包括模式切换、旋转／非均匀缩放、相机变化、反序、resize／最小化恢复、单次上传与安全退出；Debug 同步验证无 warning／error。另通过 Debug Suzanne 可视化冒烟、原深度场景和 Release TransformProbe 生命周期回归。Suzanne 只做可见性冒烟并查看截图，不是独立逐像素材质验收。

CPU 检查另覆盖剪切、反射、奇异与极小缩放、相机独立性、属性打包和坏数据；GPU 精确像素范围限上述参考资产，不承诺完整 glTF、照明或材质支持。

## 基础材质、图片与纹理采样

`--gltf` 模型默认显示 `M` 基础色材质：线性 `baseColorFactor × baseColorTexture`，暂不计算光照。`C`／`N`／`U` 保留诊断视图；`0` 将第一个实例的 X 缩放取负，检查绕序与单面材质。无参数仍默认彩色三角形。更新后先关闭旧程序并重新构建 Shader。

```powershell
cmake --build build --config Debug --target MadCatShaders MadCat MadCatMaterialChecks
.\build\Debug\MadCat.exe --gltf assets/models/Suzanne/Suzanne.gltf
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario materials
cmake --build build --config Release --target MadCat MadCatMaterialChecks MadCatGltfChecks
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Release -Scenario materials
```

| 能力 | 当前行为 |
|---|---|
| 材质 | Primitive 保存可选材质索引；缺省为白色。导入基础色 RGBA、基础色纹理、金属度／粗糙度标量、alphaMode／alphaCutoff 和 doubleSided；金属度／粗糙度尚不参与着色 |
| 图片 | stb_image 解码 PNG／JPEG，支持外部相对 URI（含中文及百分号解码）、base64 data URI 和 glTF／GLB bufferView；CPU 拥有顶行优先、非预乘 RGBA8，不做方向或 ICC 色彩变换 |
| 图片边界 | 每边最多 16384，编码／解码存储各不超过 256 MiB；拒绝不匹配 MIME、坏数据、远程 URI 和其他格式，失败无部分模型 |
| 采样 | 基础色使用 TEXCOORD_0，无 UV 的有贴图 Primitive 在窗口创建前拒绝。支持 nearest／linear、四种 mip 过滤、repeat／clamp／mirrored repeat；缺省放大 linear、缩小 trilinear、repeat |
| 色彩 | RGBA8 sRGB 图像硬件解码 RGB 后过滤，再乘线性因子；alpha 保持线性，交换链负责显示编码。缺省材质或无贴图材质绑定共享白色纹理 |
| 覆盖与面向 | OPAQUE 忽略 alpha；MASK 在相乘后按阈值裁剪。遵守 doubleSided，负世界行列式修正面向；BLEND 可保存为 CPU 数据，但当前不透明预览明确拒绝 |

资产导入分为 [图片来源读取](src/Assets/GltfImages.cpp)、[stb_image 解码适配](src/Assets/Image.cpp)和[材质／采样器转换](src/Assets/GltfMaterials.cpp)。URI 百分号与 base64 处理复用 cgltf，PNG／JPEG 解码交给开源 [stb_image 2.30](https://github.com/nothings/stb/blob/d14d46f6b44456eb88beac58cba833a4135d4c89/stb_image.h)，其实现仅在一个私有 C 翻译单元编译，并关闭其他格式及文件 I/O。完整 glTF、额外 UV 集、纹理变换、压缩纹理、法线／金属粗糙度贴图着色、透明混合及 PBR 待后续实现。[glTF 材质规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials)

[采样纹理上传](src/RHI/Vulkan/VulkanSampledTexture.h)使用 coherent staging、专用 device-local 图像和完整 mip 链；单次启动提交完成后才释放上传内存。返回时处于同一队列的 `SHADER_READ_ONLY_OPTIMAL`／Fragment sampled-read 状态，当前图导入显式交接这一状态。set 0 的 binding 0 是图像、binding 1 是采样器；具名 C++ 槽位与 Shader 对齐。`TextureBindingRef` 拥有只读 Descriptor、采样器并保留纹理；应用、管线和帧上下文保活，resize 不重传，GPU 完成后释放。

Draw 常量仍为 128 字节：三个法线列的 xyz 不变，其 w 保存基础色 RGB；offset 120／124 保存 alpha／alphaCutoff。Shader 裁剪使用 `DemoteToHelperInvocation`，Sandbox 会查询并选择支持该 Vulkan 1.3 feature 的设备，Device 仅在查询支持时启用，不凭显卡型号假定可用。

[3 组 CPU 材质检查](tests/MaterialChecks.cpp)保留 PNG／JPEG、三类图片来源、所有权／重试、材质／采样器、默认白色与实例共享，以及截断图片、BLEND 渲染和缺失 UV 拒绝。Debug／Release 均通过，原有 glTF 19 组检查也通过。删去逐字段坏值组合和重复资产导入，避免为上游解码器重复维护测试。

原创 [MaterialProbe](assets/models/README.md#材质与采样参考资产)有四种材质，[独立像素参考](tests/MaterialPixelReference.ps1)每帧 54 点覆盖 sRGB 后过滤、因子、MASK、寻址及 mip。材质窗口检查保留默认、镜像、反序、resize 和最小化恢复五次采样；stb_image 替换后的 Debug 参考场景与 Suzanne 外部贴图冒烟均通过，同步验证无 warning／error，上传共享与退出释放通过。未重跑 Release GPU 场景。Suzanne 无独立逐像素参考，不据此宣称完整外观或 PBR 验收。

当前 GPU 材质／几何在启动时固定；没有运行期图片替换、资产卸载或通用绑定系统。绘制已接入下文的最小 Render Graph；M 保留基础色预览，光照使用下节的 L 模式，F03 尚未整体验收。

## 基础方向光

加载模型后按 `L` 显示方向光 Lambert 漫反射，按 `J` 循环默认方向、旋转方向和关灯；`M` 返回不受光的基础色模式，`C`／`N`／`U` 保留诊断显示。默认仍是 M。灯光和模型变换无需重启或重建管线；按 `9` 检查非均匀缩放，`0` 检查镜像。

```powershell
cmake --build build --config Debug --target MadCatShaders MadCat MadCatRenderSceneChecks MadCatTriangleChecks
.\build\Debug\MadCat.exe --gltf assets/models/Suzanne/Suzanne.gltf
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario lighting
cmake --build build --config Release --target MadCat MadCatRenderSceneChecks MadCatTriangleChecks
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Release -Scenario lighting
```

[DirectionalLight](src/Scene/Light.h) 是 Entity 上的 CPU 组件，保存线性 RGB 和辐照度缩放；局部 −Z 为光传播方向，继承父子世界变换。[RenderScene](src/RenderScene/RenderScene.h)提取指向光源的单位方向和 RGB 辐照度；本版支持零或一盏方向光，多盏返回诊断。Sandbox 的光源颜色为 `(1,.8,.6)`、辐照度缩放 π，父节点绕 Y 为 −35°／55°，子节点绕 X 为 −25°。

L 模式在线性空间计算 `基础色 × 辐照度 × max(dot(N,L),0) / π`，交换链执行 sRGB 显示编码。法线按世界逆转置传递、插值后归一化；双面材质有效背面的法线翻转，镜像补偿绕序，MASK 保留透明裁剪。缺失法线、奇异法线变换或零插值法线显示洋红色。无环境光，关灯后的有效表面为黑色。本版没有阴影、HDR／色调映射、PBR 或更多灯型；输出超出 SDR 范围时截断。

光照数据使用独立的 [32 字节帧 Uniform Buffer](src/RHI/FrameLighting.h)，set 1／binding 0、仅片元可见；Draw 常量仍为 128 字节。每个帧上下文独立拥有 UBO 和不可变 Descriptor，等待上一帧提交完成后才更新，GPU／呈现退役并释放命令后销毁。C++ 与 Shader 必须一起更新，重新编译前先关闭旧程序。

Debug／Release 的 RenderScene 10 组、管线 66 项与材质 3 组检查通过，Slang 编译／SPIR-V 验证及 12 项编译失败恢复通过。原创 [LightingProbe](assets/models/README.md#方向光参考资产)的八次采样每次 54 个独立解析像素均通过，覆盖灯光旋转／关闭、非均匀缩放、镜像、反序、resize 和最小化恢复；Debug 同步验证无 warning／error，Release 未主动启用验证层。另通过 Debug 原材质五次采样及 Suzanne 光照八次可见性冒烟；Suzanne 没有逐像素参考。最小图执行见下节，后续补图内纹理与深度采样，再接方向光阴影。

## 最小 C++ Pass 与 Render Graph

现有窗口绘制经过 `Clear → Forward → Present`。[建图工厂](src/Passes/ForwardPass.h)由 Sandbox 显式传入，普通 Pass 通过 [PassContext](src/RenderGraph/RenderGraph.h) 和 [RHI 图形命令](src/RHI/GraphicsCommands.h)记录工作，不包含 Vulkan 类型。后端适配器只导入实际资源、执行图计划和记录同步；提交与 WSI 呈现仍由渲染器统一组织。

```powershell
cmake --build build --config Debug --target MadCat MadCatRenderGraphChecks MadCatTriangleChecks
cmake --build build --config Release --target MadCat MadCatRenderGraphChecks MadCatTriangleChecks
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario lighting
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Release -Scenario lighting
```

Clear 清除完整颜色／深度附件并返回新版本；Forward 加载这两个版本，声明顶点、索引、帧光照 Uniform 和所有可绑定基础色图像，绘制后再次返回新版本；Present 声明读取最终颜色，图将其转换为呈现布局。实际 `vkQueuePresentKHR` 在统一提交之后执行。写后写也需要同步，深度在两个动态渲染实例之间保存并加载；相关机制见 [Vulkan 同步示例](https://docs.vulkan.org/guide/latest/synchronization_examples.html)。

[图编译器](src/RenderGraph/RenderGraph.cpp)校验资源种类、初始化、写入后继和图身份，按生产者／读取者及旧版本读取到后继写入的依赖排序。每次写入产生新版本，同版本分叉写入会报错；重复导入同一底层图像／缓冲会合并身份。有环时诊断包含节点、资源和版本。仅有旧句柄不能保留被覆盖前的像素。

图保存每个节点的 `Declared` 和 Pass 值；`Parameters` 在记录开始时复制固定。编译计划复用，记录时结合本帧显式外部状态生成屏障。`PassContext` 拒绝未声明资源／版本／用途、错误附件加载方式及未结束的渲染，即使 Pass 忽略错误返回也会取消整图。记录结果返回预期最终状态；记录失败不推进外部状态，整图成功记录后才统一提交。

[Vulkan 执行适配](src/RHI/Vulkan/VulkanFrameGraph.cpp)为每个交换链图像建立独立计划，导入颜色、共享深度、顶点／索引、帧 UBO 和去重后的材质图像。图外上传交接 HostWrite 或 Fragment sampled-read；每帧 UBO 更新后交接 HostWrite，深度和交换链状态由成功提交／呈现记录提供。RHI 在上一帧完成后复用缓冲，持有实际资源至命令退役。模型／灯光参数变化不重新编图，resize 在等待完成后重建。

当前支持单队列、整资源访问、已有资源导入和下节的图内深度纹理；管线与 Descriptor 仍沿用预先准备的固定接口。尚无子资源范围、Compute／Dispatch、通用 `prepare`／任意管线切换、运行期图切换、稳定提交标识或 Shader 更新；F04／F05 尚未整体验收。

2026-11-06 首轮图增量的 Debug／Release 主程序、七组图检查和 66 项原管线检查通过，无编译警告。两种配置的光照八次采样每次 54 点通过；Debug 另通过模型删除至空／恢复、深度遮挡及反序、resize／最小化恢复和最小化退出。检查还核对每个交换链图像只建立一份三节点计划，材质探针共导入八个实际资源；Debug 同步验证无 warning／error，Release 未主动启用验证层。该轮未修改或重编 Shader，未测性能；后续深度采样增量见下节。

## 图内深度纹理与查看

`--depth-view` 选择 [C++ 深度查看图](src/Passes/DepthViewPass.h)：`Clear → Forward → DepthView → Present`。Forward 将几何深度写入图创建的纹理，保留当前材质、MASK 与面向规则；DepthView 在只绑定颜色附件的全屏 Pass 中采样它。显示值为 `1 - deviceDepth`，经 sRGB 编码；清除区为黑色，近处更亮，数值不是线性距离。可与 `--gltf` 组合，原有姿态按键继续有效。

```powershell
cmake --build build --config Debug --target MadCatShaders MadCat MadCatRenderGraphChecks MadCatDepthChecks
./build/Debug/MadCat.exe --depth-view
./build/Debug/MadCat.exe --gltf assets/models/MaterialProbe.gltf --depth-view
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario depth -DepthView
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario materials -DepthView
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario model -DepthView
```

`Graph::createDepthTexture(name, {width, height, true})` 返回未初始化版本，必须先清除／写入才能读取；`sampled=false` 禁止声明采样。相同名字不合并资源。编译计划保存描述，[RHI 适配器](src/RHI/Vulkan/VulkanFrameGraph.cpp)为每个交换链图像的计划分别分配并保活纹理及不可变采样绑定。尺寸随输出重建，格式查询同时覆盖深度附件和采样用途；只支持一个 mip／层、nearest 采样和与当前 Forward 管线相容的格式／输出尺寸。当前不做内存别名或临时资源池。

图自动处理深度写入到 Fragment sampled-read 的依赖，以及下一次使用该纹理时采样到深度写入的依赖。内部状态仅在队列提交成功后登记；每帧 UBO 的 HostWrite 仍显式交接。销毁前等待 GPU／呈现退役并释放命令，再释放图内纹理、Descriptor 和借用的管线。

Debug／Release 各九组图检查、32 项深度与 66 项管线检查通过；Shader 两个包各 12 项编译失败恢复检查通过。两种配置均通过八次三角形深度参考采样；Debug 另通过材质五次每次 54 点、模型十一次（含空场景／恢复）、最小化退出及原方向光八次参考回归。所有 Debug 场景的同步验证无 warning／error；Release 未主动启用验证层。截图和日志在 `build/verification/triangle/`，深度查看器文件名含 `depth-view`。

该链路仍复用写颜色和深度的 Forward 管线，之后覆盖颜色显示深度；尚无专用只写深度的几何管线或光源视角阴影。下一步实现方向光阴影，并补齐所需的专用深度绘制与绑定。

## 手动编译 Shader

当前固定 `slangc -version` 为 `2026.17-25-gc7954ebd1`（本机 Vulkan SDK 1.4.363.0 所带版本），构建时检查完整版本字符串。验证工具为 `spirv-val`，本机版本为 `SPIRV-Tools v2026.4 v2026.4.rc2-0-gef96ed76`。两个程序优先从 `$env:VULKAN_SDK/Bin` 查找，其次使用系统搜索路径；可在 CMake 配置时通过 `-DMADCAT_SLANGC=完整路径`、`-DMADCAT_SPIRV_VAL=完整路径` 指定。普通 `MadCat` 构建不依赖这些程序；缺少工具或编译器版本不符时，显式 Shader 构建失败并给出诊断。

从根目录执行：

```powershell
cmake --build build --config Debug --target MadCatShaders
cmake --build build --config Debug --target MadCatShaderChecks
```

`MadCatShaders` 每次手动调用依次编译 triangle 与 depth-view 两个包，每包含顶点和片元两个阶段，Debug／Release 使用同一套参数和产物目录。源码为 [triangle.slang](shaders/triangle.slang)，顶点入口为 `vertexMain`，片元入口为 `fragmentMain`；保留入口名称，直接生成 SPIR-V 1.6，使用 `-O0`，将警告视为错误，再执行 `spirv-val --target-env vulkan1.3`。当前顶点输入为 location 0／1／2 的 float3 位置／颜色／法线及 location 3 的 float2 UV；片元使用 set 0 的基础色图像／采样器，并声明 `Shader` 和 `DemoteToHelperInvocation` capability。布局与设备要求见前述材质工作流。[Slang 命令行参考](https://docs.shader-slang.org/en/stable/external/slang/docs/command-line-slangc-reference.html)

triangle 产物位于 `build/shaders/triangle/`；depth-view 产物位于同级 `depth-view/`，文件结构相同，源码快照为 `depth-view.slang`：

| 文件 | 内容 |
|---|---|
| `vertex.spv`、`fragment.spv` | 通过验证的两个阶段 |
| `triangle.slang` | 本次编译的源文件快照，两个阶段使用同一份内容 |
| `manifest.txt` | 工具版本与路径、入口、编译／验证参数、源码和二进制 SHA256 |

[编译脚本](cmake/CompileShaders.cmake) 先在候选目录编译并验证，两个阶段全部成功后才替换整组产物；编译／验证失败时保留上一组，清理候选文件并返回非零状态。目录发布中断时，下一次构建会先恢复已备份的上一组；同一输出目录使用进程锁避免并发发布。该流程属于离线构建，不是运行期热更新协议。

[检查目标](tests/ShaderBuildChecks.cmake) 对两个包分别运行 12 个真实编译与故障场景，使用独立的 `build/verification/shader-build/` 与 `build/verification/depth-view-build/`，核对失败前后的四个文件哈希。当前仅处理一个自包含 `.slang` 文件，尚未建立 include／import 依赖记录或反射。

## Ray Query 编译探针

从根目录执行下面的独立手动目标，无需 GPU：

```powershell
cmake --build build --config Debug --target MadCatRayQueryProbe
cmake --build build --config Debug --target MadCatRayQueryChecks
```

[ray-query.slang](shaders/ray-query.slang) 使用固定射线遍历不透明三角形 TLAS，将命中／未命中写为一个 `uint`。当前只编译和检查产物，程序尚不加载或执行它。入口为 `computeMain`，工作组为 `1 × 1 × 1`；将来 GPU 探针须绑定已构建的 TLAS 和至少 4 字节的 Storage Buffer，并只 Dispatch `(1, 1, 1)`。

该目标沿用上述编译／验证参数和锁定的 Slang 版本，额外指定 `-capability spvRayQueryKHR`。本机实测省略此参数时，Slang 诊断 profile 隐式升级，被 `-warnings-as-errors all` 拒绝。[Slang TraceRayInline 要求](https://docs.shader-slang.org/en/stable/external/core-module-reference/types/rayquery-03/tracerayinline-058.html)

额外工具 `spirv-dis` 通过 SDK／系统路径查找，或在配置时指定 `-DMADCAT_SPIRV_DIS=完整路径`。普通 C++ 构建与三角形 Shader 目标不要求此工具。成功产物发布至 `<构建目录>/shaders/ray-query/`，包含 `compute.spv`、`compute.spvasm`、源码快照 `ray-query.slang` 与 `manifest.txt`；清单记录工具版本、参数、SHA256 和检查结果。共享离线发布流程保证编译、验证或接口检查失败时保留上一组产物。

[固定探针检查](cmake/CheckRayQuery.cmake) 在 `spirv-val --target-env vulkan1.3` 成功后，检查反汇编的计算入口、工作组、query 指令、资源存储类别及 set／binding：

| 资源 | set／binding | 后续 Vulkan 绑定类型 |
|---|---|---|
| `scene` | 0／0 | `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR` |
| `results` | 0／1 | `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER`，一个 32 位无符号整数 |

锁定版本实测只生成 `Shader`、`RayQueryKHR` capability，`Logical GLSL450` 内存模型，以及 `SPV_KHR_ray_query`、`SPV_KHR_storage_buffer_storage_class` 扩展声明。后者已被当前 Vulkan 1.3 基线覆盖；前者对应设备 Ray Query 支持。检查会拒绝 capability／扩展集合变化，促使重新核对设备要求。这是固定 Shader 的检查，不是通用反射或运行时绑定校验。

在项目的 Vulkan 1.3 基线下，后续 GPU 实现至少需要：

- 查询并启用设备扩展 `VK_KHR_acceleration_structure`、`VK_KHR_ray_query`、`VK_KHR_deferred_host_operations`。其他核心版本依赖已由 Vulkan 1.3 覆盖。[加速结构扩展依赖](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_acceleration_structure.html)、[Ray Query 扩展依赖](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_query.html)
- 查询并在 Device 创建链中启用 `bufferDeviceAddress`、`accelerationStructure`、`rayQuery`。查询为 true 不等于已启用；当前 `VulkanDevice` 尚未启用这些 feature 或三个扩展。[Ray Query feature 启用](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceRayQueryFeaturesKHR.html)、[Vulkan 1.2 feature](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan12Features.html)
- 使用 Compute 队列，补齐缓冲分配、地址、BLAS／TLAS 构建、Descriptor、计算管线、提交及回读，并同步构建写入 → 计算阶段加速结构读取、Shader 写入 → 主机读取，完成后才回收资源。

本探针采用固定 Descriptor 和普通 Storage Buffer，不要求 Ray Tracing Pipeline、Shader Binding Table、Descriptor 数组或 update-after-bind。后续新增资源机制仍须按实际用途核对所需 feature。

`MadCatRayQueryChecks` 使用 `<构建目录>/verification/ray-query-build/` 独立目录，包含 18 项真实工具及失败恢复检查，包括 capability 省略诊断、查询移除、绑定／工作组变化及缺少反汇编器。2026-10-26 Debug／Release 均通过；三角形原有 12 项构建检查通过。编译成功不表示 GPU 求交或路径追踪已经实现。

## 加载 Shader Module

启动时从 CMake 配置所记录的绝对路径 `<构建目录>/shaders/triangle/` 读取 `vertex.spv` 和 `fragment.spv`，不依赖当前工作目录。首次运行前须显式执行 `MadCatShaders`；缺少或无法读取产物时，日志包含文件路径和原因，程序清理已创建对象并返回非零状态。普通 C++ 构建仍不调用 Shader 编译器，不自动生成或复制 Shader。当前使用构建目录中的开发产物，移动构建目录后须重新配置并构建程序，尚无可分发安装包的资源定位方案。

[`loadSpirv`](src/Shader/Spirv.h) 将文件读入对齐的 `uint32_t` 数组，检查非空、字节数为 4 的倍数、完整头部、魔数、版本 1.0～1.6、bound／schema 和指令长度，按魔数识别并转换字节序。它只检查基本结构，不检查完整语义、入口、资源布局、Device capability 或 manifest 哈希，不能替代离线 `spirv-val`。管线固定使用顶点入口 `vertexMain` 和片元入口 `fragmentMain`，已通过当前 Shader 的管线创建、Validation Layers 和像素检查；加载器不自行解析入口或反射。[SPIR-V 二进制格式](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#_physical_layout_of_a_spir_v_module_and_instruction)

[`VulkanShaderModule`](src/RHI/Vulkan/VulkanShaderModule.h) 拥有模块并借用 Device，禁止复制／移动，先解析销毁函数再创建模块。创建失败不接管失败输出；重复初始化拒绝替换已有模块。Sandbox 的两个模块在 Device 之前释放，第二个加载失败时第一个也会释放。模块销毁要求相关主机操作结束；管线创建完成后，模块可先于使用该管线的 GPU 工作释放，不为模块单独增加 GPU 等待。[Vulkan Shader Module 销毁规则](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyShaderModule.html)

当前仅在启动时读取一次；重新编译前先关闭程序，编译成功后重新启动。离线目录发布不保证两个独立文件在并发读取时来自同一版本；运行期安全更新和失败保留旧管线尚未实现。

不需要 GPU 或 Shader 编译工具的文件／生命周期检查：

```powershell
cmake --build build --config Debug --target MadCatShaderModuleChecks
```

该目标包含 35 项 CPU 与模拟 Vulkan 场景，覆盖路径、字节序、损坏文件、缺失函数、创建失败、重试、重复初始化和部分创建清理；预期失败诊断保存在 `build/verification/shader-module/Debug/`。它不替代真实驱动上的 Validation Layers 和渲染场景验证。

## 缓冲资源与顶点上传

[BufferRef](src/RHI/Buffer.h) 是不可变 GPU 缓冲的强引用；复制共享同一份资源，移动转移引用，空引用的 `sizeBytes()` 为 0。内部共享记录同时拥有 VkBuffer 和专用 VkDeviceMemory，原生访问集中在 [Vulkan 后端](src/RHI/Vulkan/VulkanBuffer.h)。`createVertexBuffer(device, bytes)` 接收非空字节；`createIndexBuffer(device, uint32Span)` 接收非空且数量可由 uint32 表示的索引。两者要求已初始化的 Device，创建后不提供修改或映射接口。

以下片段使用已初始化的 `device`，完整设备／管线组装见 [Sandbox](apps/Sandbox/main.cpp)：

```cpp
const madcat::rhi::ColoredVertex vertices[] = {
    {{0, -0.6f, 0}, {1, 0, 0}}, {{0.6f, 0.6f, 0}, {0, 1, 0}}, {{-0.6f, 0.6f, 0}, {0, 0, 1}}
};
auto buffer = madcat::rhi::createVertexBuffer(device, std::as_bytes(std::span(vertices)));
const uint32_t order[] = {2, 0, 1};
auto indices = madcat::rhi::createIndexBuffer(device, order);
if (!buffer || !indices) { return EXIT_FAILURE; }
// CPU inputs can now be released. Pass both BufferRef values to the pipeline.
```

上传会按实际 memoryTypeBits 选择兼容的 `HOST_VISIBLE | HOST_COHERENT` 内存，按 Vulkan 返回的内存需求分配，绑定后 map／复制／unmap；失败清理部分资源并返回空引用，CPU 分配失败仍可能抛异常。写入在发布引用、记录和同线程提交之前完成；coherent 内存不需要 flush，提交建立主机写入到设备读取的依赖。[Khronos 主机写入规则](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-submission-host-writes)

索引上传还记录不可变的最大索引值，区分 Vertex／Index 用途。当前 Device 未启用 `fullDrawIndexUint32`，因此上传拒绝超过设备 `maxDrawIndexedIndexValue` 与 `0x00ffffff` 较小值的索引，不假定物理设备支持就已启用完整范围。[Khronos 索引范围](https://docs.vulkan.org/spec/latest/chapters/features.html#features-fullDrawIndexUint32)

无参数 Sandbox 首次绘制前上传四个三维顶点共 96 字节和九个 uint32 索引共 36 字节。顶点顺序为中心、绿、红、蓝，索引为 `{2,3,0, 3,1,0, 1,2,0}`，三个小三角形共享中心与边界顶点；中心颜色为 RGB 各 1/3，使外轮廓及原有渐变不变。应用、管线和帧上下文分别保留两份引用；resize 只重建呈现资源，复用两个缓冲。帧上下文析构先等待 acquisition／GPU／呈现完成并释放命令，再放开引用；最后一份引用销毁 Buffer 后释放内存。Device 必须覆盖全部引用寿命，引用计数本身不代表 GPU 已完成。

```powershell
cmake --build build --config Debug --target MadCatBufferChecks MadCatTriangleChecks MadCatShaders MadCat
cmake --build build --config Release --target MadCatBufferChecks MadCatTriangleChecks MadCat
```

`MadCatBufferChecks` 包含 40 项模拟 Vulkan 检查，覆盖两个用途的缺失函数、内存类型筛选、创建／分配／绑定／映射失败、清理、重试、源数据复制、引用释放与索引值上限。当前没有 staging／设备本地复制、非 coherent 回退、uint16 索引、Storage 缓冲、可变上传区或通用提交回收系统。当前图导入分别声明 Vertex Attribute Input／Vertex Attribute Read 与 Index Input／Index Read，不能因引用存在而省略访问声明。

## 三角形绘制与检查

[`VulkanTrianglePipeline`](src/RHI/Vulkan/VulkanTrianglePipeline.h) 是当前后端验证场景的专用封装：一个 128 字节 Vertex Push Constant 范围、Triangle List、一个与 Swapchain 格式匹配的颜色附件和匹配实际深度纹理的附件格式、单采样、填充且不剔除，启用 LESS 深度测试／写入，不启用混合。它通过 `VkPipelineRenderingCreateInfo` 接入动态渲染，采用 set 0 的基础色图像／采样器绑定。`ColoredVertex` 固定为 44 字节，binding 0、逐顶点，位置 location 0／float3／offset 0，颜色 location 1／float3／offset 12，法线 location 2／float3／offset 24，UV location 3／float2／offset 36。

初始化接收同 Device 的 Vertex／Index 两个 BufferRef，拒绝用途混用、不完整顶点步长、少于三个或超出 uint32 数量的顶点、非三角形索引数及越界索引；失败不创建管线对象。`recordDraw` 绑定管线、正高度 viewport／scissor、顶点及 uint32 索引缓冲，录入有限的 DrawTransform，再记录 `vkCmdDrawIndexed(indexCount, 1, 0, 0, 0)`，索引偏移与 vertexOffset 均为零，primitive restart 关闭。矩阵仍由 Scene／相机提取。[动态渲染格式](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineRenderingCreateInfo.html)、[索引绑定](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBindIndexBuffer.html)、[索引绘制](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexed.html)

管线必须活到引用它的 GPU 提交完成；`Presentation` 按 Swapchain、管线、帧渲染器的顺序声明成员，保证反向释放时先等待 GPU／WSI 并释放命令，再释放管线与交换链。管线创建失败会清理已创建的 Layout；该类型不自行等待整个 Device，也不提供通用材质／Pass 接口。[Pipeline 销毁要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyPipeline.html)

```powershell
cmake --build build --config Debug --target MadCatTriangleChecks
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug
```

`MadCatTriangleChecks` 复用模拟 Vulkan 测试程序，包含 66 项管线创建、失败清理、颜色／深度格式、深度比较与写入、顶点／索引布局与绑定、Push Constant 范围与字节内容、非有限输入、动态尺寸、不同索引数量、错误用途／越界拒绝及双缓冲保活检查，不依赖 GPU 或 Shader 工具。窗口脚本会启动程序、改变窗口状态、采集截图并正常关闭，要求桌面可见且无其他窗口遮挡；参考颜色适用于 SDR 的 R8／B8 sRGB 交换链和 Sandbox 的固定场景。普通场景检查十个像素样本，双模型场景增加两个有不同深度胜者的重叠样本；同时核对顶点／索引各上传一次、每个上下文的九索引绘制、颜色／深度尺寸和释放顺序。Debug／Release 遮挡、Debug 场景／裁剪、最小化关闭和连续尺寸变化已通过；Debug 同步验证无 warning／error。

脚本支持 `-Configuration Release`、`-BuildDirectory <构建目录>`，以及 `-Scenario depth`（相交遮挡／反向绘制／附件重建）、`scene`（场景姿态／裁剪／重复切换）、`modal`（原生缩放循环）、`stress`（连续尺寸变化与快速恢复）、`close-minimized`（最小化关闭）。实际鼠标拖动的人工验收仍需单独进行。日志、模拟检查结果和截图均位于 `<构建目录>/verification/triangle/`。

## Vulkan 后端封装

| 类型 | 当前职责 |
|---|---|
| [`VulkanInstance`](src/RHI/Vulkan/VulkanInstance.h) | 拥有 Instance 和诊断回调，提供后端使用的只读句柄访问 |
| [`VulkanSurface`](src/RHI/Vulkan/VulkanSurface.h) | 独立拥有 Surface，通过 RAII 清理；借用 Instance 和窗口，禁止复制与移动 |
| [`PhysicalDeviceInfo`](src/RHI/Vulkan/VulkanPhysicalDevice.h) | 保存可复制的设备属性、队列族、扩展、feature 和加速结构限制；借用物理设备句柄，不销毁设备 |
| `SurfaceSupport` | 保存一次设备／Surface 组合查询的呈现支持和能力快照 |
| `PhysicalDeviceCandidate` | 配对保存设备信息与该设备的 Surface 支持快照，供设备筛选使用 |
| [`VulkanDevice`](src/RHI/Vulkan/VulkanDevice.h) | 从候选快照中选择设备，拥有逻辑设备及所选物理设备的数据副本，提供借用的句柄、队列族索引和只读查询数据 |
| [`VulkanSwapchain`](src/RHI/Vulkan/VulkanSwapchain.h) | 拥有交换链，创建前刷新 Surface 支持，提供实际格式／颜色空间、尺寸和借用的图像句柄 |
| [`VulkanShaderModule`](src/RHI/Vulkan/VulkanShaderModule.h) | 加载离线验证的 SPIR-V，拥有 Shader Module，借用 Device；不负责管线创建和 Shader 执行 |
| [`BufferRef`](src/RHI/Buffer.h)／[Vulkan 创建入口](src/RHI/Vulkan/VulkanBuffer.h) | 不可变顶点数据的强引用，共享记录拥有 Buffer 与专用内存，创建时上传；Device 和 GPU 完成边界由调用路径保证 |
| [`TextureRef`](src/RHI/Texture.h)／[深度创建入口](src/RHI/Vulkan/VulkanDepthTexture.h) | 深度图像、视图与专用内存的强引用，查询能力后创建；帧上下文按 GPU 完成条件保活 |
| [`VulkanTrianglePipeline`](src/RHI/Vulkan/VulkanTrianglePipeline.h) | 拥有最小三角形管线及矩阵 Push Constant Layout，保留顶点引用，借用 Device，封装管线／顶点绑定、viewport／scissor、矩阵快照与 Draw |
| [`VulkanClearRenderer`](src/RHI/Vulkan/VulkanClearRenderer.h) | 借用同一 Device 的 Swapchain 和可选三角形管线，保留固定顶点引用直到 GPU 完成，拥有图像视图、一个命令池／命令缓冲、两个二进制信号量及 acquire／submit／present Fence，记录清屏和可选 Draw 并提交 |

`queryPhysicalDevices(instance, devices)` 不要求先创建 Surface。`querySurfaceSupport(device, surface, support)` 检查两者属于同一个 Instance，然后查询实际窗口支持；`printPhysicalDeviceReport` 仅打印数据，不再查询 Vulkan。两种查询都在成功时整体替换输出，失败时保留原结果；保存的数据不包含指向临时查询结构的 `pNext` 指针。

`VulkanDevice::initialize(candidates)` 要求所有快照属于同一个 Instance 和 Surface，复制所选物理设备的数据，不保留候选容器引用或 Surface 支持快照。`physicalDeviceInfo()` 的只读引用在 Device 对象销毁前有效，其中的 Instance 和物理设备句柄仍是借用的；初始化前查询数据为空。创建失败时保持空状态并释放部分创建的 Device；重复初始化被拒绝且保留现有 Device。队列不单独销毁，其句柄在 Device 释放时失效。Device 命令通过 `vkGetDeviceProcAddr` 获取；创建前已解析核心销毁函数，确保命令加载失败仍能清理。

`VulkanSwapchain::initialize(device, surface, framebufferExtent)` 要求 Device 和 Surface 已初始化且属于同一个 Instance，尺寸以像素为单位。刷新 Surface 支持后再次确认所用队列能够呈现，并创建交换链、枚举图像；`VK_INCOMPLETE` 会重试枚举。失败及图像枚举期间的异常会清理部分创建的交换链，所有者保持空状态，支持重试；重复初始化被拒绝且保留原交换链。`images()` 返回的 span 和其中的图像句柄在交换链销毁后失效；图像由交换链管理，不单独销毁。[图像枚举](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainImagesKHR.html)

组装顺序见 [`Sandbox`](apps/Sandbox/main.cpp)：Instance、Surface、Device、Shader Module 先创建，`SandboxApplication` 随后拥有可替换的 `Presentation`，后者按 Swapchain、管线、帧渲染器的顺序声明成员，保证先完成等待并释放命令资源。重建策略与窗口回调在应用层，Vulkan 资源和实际等待仍由后端执行。帧渲染器的 `waitIdle()` 和析构在释放自身资源前等待 acquisition、提交和呈现资源退役；管线与 Swapchain 析构不自动等待 GPU。应用销毁前注销回调并清除窗口用户指针。窗口须活到 Surface 释放后，Instance 须活到 Device／Surface 释放后；Device 最后的析构等待不能保护此前已释放的子资源。[Device 销毁要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyDevice.html)、[Swapchain 销毁要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroySwapchainKHR.html)

每帧等待上一帧三个完成条件后复用资源；acquire 使用 50 ms 超时，无可用图像时返回 `Retry`，不重置提交 Fence。成功后记录 `UNDEFINED`／`PRESENT_SRC_KHR` → `COLOR_ATTACHMENT_OPTIMAL` → `PRESENT_SRC_KHR` 转换；提交等待阶段为颜色附件输出，完成信号覆盖全部命令。只等待确实已发起的工作；录制／提交失败仍等待已发起的 acquisition，present 的 out-of-date／surface-lost 与内存不足排队语义分开处理。[acquire 要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkAcquireNextImageKHR.html)、[present 错误语义](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html)

呈现 Fence 确认呈现资源可退役，不能用提交 Fence 或单独的 `vkDeviceWaitIdle` 替代。本轮等待呈现 Fence 后再复用唯一的 render-finished 信号量，保持一个在途帧，不以此宣称性能结果。清屏器现在保留帧生命周期与呈现职责，Pass 绘制和资源同步已由最小图执行接管。帧错误后停止后续提交；完成等待失败会诊断，析构重试仍失败时终止进程，避免释放无法确认完成的资源；设备丢失恢复未实现。[呈现信号量与退出问题](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)、[呈现 Fence 语义](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html)

## 常见初始化问题

| 诊断 | 检查方法 |
|---|---|
| CMake 找不到 Vulkan | 确认 SDK 已安装、`VULKAN_SDK` 指向正确目录，重新配置 |
| 找不到可用的 loader／driver，或缺少窗口 Surface 扩展 | 检查显卡驱动安装，以及 `VK_DRIVER_FILES` 等驱动搜索设置 |
| 实例级 API 低于 Vulkan 1.3 | 检查运行时 loader 和驱动；安装较新 SDK 不代表运行时已更新 |
| Debug 缺少 `VK_LAYER_KHRONOS_validation` | 确认 SDK 验证层已安装，检查 `VK_LAYER_PATH` 是否覆盖了正常搜索路径 |
| Vulkan API 调用失败 | 日志包含操作名称与数值 `VkResult`；同时检查此前的验证消息 |
| 没有物理设备或设备查询失败 | 检查 Vulkan 驱动及搜索配置，并查看失败操作名称和设备标识 |
| Surface 创建或能力查询失败 | 检查此前的 GLFW／Vulkan 诊断；日志保留失败操作、`VkResult` 以及查询涉及的设备／队列信息 |
| 没有合适的物理设备 | 查看每张设备的拒绝原因；当前要求设备级 Vulkan 1.3、基础渲染 feature 和单个 Graphics／Compute／Present 队列族 |
| Device 创建或队列获取失败 | 查看 `vkCreateDevice` 的 `VkResult`、选中设备及队列族，或缺失函数／空队列诊断；部分创建的 Device 会释放 |
| Swapchain 创建或图像枚举失败 | 查看配置、缺失函数或 `VkResult` 诊断；确认 Surface 支持、非零像素尺寸及所选队列的呈现支持；创建后的失败会释放交换链 |
| 清屏器要求 maintenance1 | 检查 EXT 扩展、feature 及实例依赖；基础初始化可用不代表清屏可用 |
| 最小化时停止更新 | 当前预期行为；等待窗口恢复后重建，也可直接关闭 |
| 重建失败退出 | 查看 `presentation creation/rebuild failed` 及此前的 Vulkan 诊断；当前不实现失败后的重试或设备丢失恢复 |
| Shader 初始化失败 | 查看具体文件路径和诊断；在对应构建目录执行 `MadCatShaders`，确认成功后重启程序，不直接修改 `.spv` 文件 |
| 三角形管线创建失败 | 查看 `vkCreatePipelineLayout`／`vkCreateGraphicsPipelines`、入口名、颜色格式及此前的验证层诊断；当前只接受仓库三角形 Shader 的固定接口 |

完整需求和后续计划见 [项目开发文档](docs/project-development.md)，协作规则见 [AGENTS.md](AGENTS.md)。
