# MadCat

使用自研 ECS、Vulkan 和 Slang 的渲染引擎，目前可显示 Slang Shader 绘制的彩色三角形，支持单帧同步、resize／最小化恢复时的 Swapchain 与管线重建，以及 Shader 手动编译、SPIR-V 验证和 Vulkan 资源的 RAII 管理。

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

首次配置通过 CMake FetchContent 获取固定提交的 GLFW 3.4 和 GLM 1.0.1，需要访问 GitHub；依赖源码和构建产物位于 `build` 下。GLM 用于纯 CPU 场景变换，锁定提交 `0af55ccecd98d4e5a8d1fad7de25ba429d60e863`，不启用其测试或编译库。

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
Vulkan: triangle Draw enabled (3 vertices, 1 instance).
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

自研 ECS 已有独立的纯 CPU 库 `MadCatECS`，提供 [Entity](src/ECS/Entity.h) 值句柄与 [Registry](src/ECS/Registry.h) 的创建、销毁、存活检查、按类型保存的组件及[组合查询](#ecs-组合查询与遍历后修改)。[Scene](#scene-层级与-transform) 在其上管理场景层级与变换，渲染窗口接入仍未实现。

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

场景对象统一使用 Entity，通过 Scene 管理层级和 Transform，并组合用户组件；后续 RenderScene 从场景提取渲染数据。Mesh／Material 资产组件与自动渲染链路尚未实现。

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

纯 CPU 库 `MadCatScene` 提供 [Scene](src/Scene/Scene.h)，独占一个 Registry。每个场景对象仍是 Entity，创建时具有单位局部变换和根节点关系。Scene 内部组件保存局部 TRS、父子关系及派生世界矩阵；通过受控接口写入，以保证父节点有效、无环及删除行为一致。Scene 不暴露可写 Registry。

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

数学约定采用右手系、Y 向上、米、弧度和列向量，局部矩阵为 `T * R * S`，子世界矩阵为 `parentWorld * local`。四元数构造参数是 `(w, x, y, z)`，写入时归一化；glTF 的数组顺序需在未来导入器中显式转换。世界矩阵保留剪切，不重新分解为 TRS。当前定义 `GLM_FORCE_DEPTH_ZERO_TO_ONE`，但尚未实现或验证相机投影、视口翻转和 Shader 上传布局。

`setParent(child, parent)` 保留局部变换，`setParent(child, {})` 解除父节点。无效／旧句柄、自引用和循环返回 false，重复设置同一父节点不重复添加子节点。`destroy()` 仅删除无子节点的实体；`destroySubtree()` 显式删除整个子树，先准备工作列表再按子先父后清理。创建、挂接和更新可能分配内存并抛异常，失败前保留仍有效的成员关系。

`setLocalTransform()` 拒绝非有限值和零四元数，保留原局部变换；允许零或负缩放。成功修改局部变换、层级或实体后，全部世界矩阵标为未更新；`worldTransform()` 返回 nullptr，直到查询外调用 `updateWorldTransforms()` 完整成功。矩阵组合溢出返回 false，不暴露部分更新结果；修正局部变换后可重试。

`add<T>`／`tryGet<T>`／`remove<T>` 和可变／const `each<T...>` 用于自定义数据组件，沿用 ECS 契约；`LocalTransform` 通过专用接口管理。查询中允许修改自定义组件字段及调用 `setLocalTransform()`，拒绝创建、删除、挂接和世界矩阵更新；const 别名和嵌套查询同样受保护。系统可先查询并写局部变换，查询结束后更新世界矩阵，再提取渲染数据。组件构造、移动和析构不得重入所属 Scene；查询回调不得销毁所属 Scene。

所有句柄仅限所属 Scene；跨 Scene 误用不检测。局部变换／世界矩阵指针、子节点 span 都是借用，实体创建／删除或层级修改后应重新获取；世界矩阵也应在每次更新后重新获取。已经取得的指针不会因未更新标记自动失效，调用方不得用它读取过期结果。

```powershell
cmake --build build --config Debug --target MadCatSceneChecks MadCatQueryChecks MadCatComponentChecks MadCatEntityChecks MadCat
cmake --build build --config Release --target MadCatSceneChecks MadCatQueryChecks MadCatComponentChecks MadCatEntityChecks MadCat
```

[场景检查](tests/SceneChecks.cpp) 覆盖局部 TRS、父先子后更新、非均匀缩放产生的剪切、挂接与环拒绝、叶节点／子树删除、句柄复用、无效输入、溢出恢复、系统查询边界、2048 层深度及 512 步层级参考模型。尚未接入 Sandbox、相机、glTF、RenderScene 或场景版本；不提供保持世界位置的重新挂接、dirty 增量更新或自动命令队列。

## 手动编译 Shader

当前固定 `slangc -version` 为 `2026.17-25-gc7954ebd1`（本机 Vulkan SDK 1.4.363.0 所带版本），构建时检查完整版本字符串。验证工具为 `spirv-val`，本机版本为 `SPIRV-Tools v2026.4 v2026.4.rc2-0-gef96ed76`。两个程序优先从 `$env:VULKAN_SDK/Bin` 查找，其次使用系统搜索路径；可在 CMake 配置时通过 `-DMADCAT_SLANGC=完整路径`、`-DMADCAT_SPIRV_VAL=完整路径` 指定。普通 `MadCat` 构建不依赖这些程序；缺少工具或编译器版本不符时，显式 Shader 构建失败并给出诊断。

从根目录执行：

```powershell
cmake --build build --config Debug --target MadCatShaders
cmake --build build --config Debug --target MadCatShaderChecks
```

`MadCatShaders` 每次手动调用都会编译两个阶段，Debug／Release 使用同一套参数和产物目录。源码为 [triangle.slang](shaders/triangle.slang)，顶点入口为 `vertexMain`，片元入口为 `fragmentMain`；保留入口名称，直接生成 SPIR-V 1.6，使用 `-O0`，将警告视为错误，再执行 `spirv-val --target-env vulkan1.3`。顶点使用 `SV_VulkanVertexID`，没有顶点缓冲或 Descriptor；实测两个模块只声明 `Shader` capability，颜色接口位于 location 0。[Slang 命令行参考](https://docs.shader-slang.org/en/stable/external/slang/docs/command-line-slangc-reference.html)、[SPIR-V 系统语义](https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/a2-01-spirv-target-specific.html)

成功产物位于 `build/shaders/triangle/`：

| 文件 | 内容 |
|---|---|
| `vertex.spv`、`fragment.spv` | 通过验证的两个阶段 |
| `triangle.slang` | 本次编译的源文件快照，两个阶段使用同一份内容 |
| `manifest.txt` | 工具版本与路径、入口、编译／验证参数、源码和二进制 SHA256 |

[编译脚本](cmake/CompileShaders.cmake) 先在候选目录编译并验证，两个阶段全部成功后才替换整组产物；编译／验证失败时保留上一组，清理候选文件并返回非零状态。目录发布中断时，下一次构建会先恢复已备份的上一组；同一输出目录使用进程锁避免并发发布。该流程属于离线构建，不是运行期热更新协议。

[检查目标](tests/ShaderBuildChecks.cmake) 包含 12 个真实编译与故障场景，使用独立的 `build/verification/shader-build/`，核对失败前后的四个文件哈希。当前仅处理一个自包含 `.slang` 文件，尚未建立 include／import 依赖记录或反射。

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

## 三角形绘制与检查

[`VulkanTrianglePipeline`](src/RHI/Vulkan/VulkanTrianglePipeline.h) 是当前后端验证场景的专用封装：空 Pipeline Layout、Triangle List、一个与 Swapchain 格式匹配的颜色附件、单采样、填充且不剔除、不启用深度或混合。它通过 `VkPipelineRenderingCreateInfo` 接入动态渲染；顶点位置和颜色来自 Shader，不使用顶点缓冲、索引、Descriptor 或 Push Constant。`recordDraw` 绑定管线，设置覆盖当前像素尺寸的正高度 viewport 与 scissor，再记录 `vkCmdDraw(3, 1, 0, 0)`。三角形采用固定裁剪空间坐标，随窗口比例伸缩；尚无相机或场景坐标系统。[动态渲染格式](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineRenderingCreateInfo.html)

管线必须活到引用它的 GPU 提交完成；`Presentation` 按 Swapchain、管线、帧渲染器的顺序声明成员，保证反向释放时先等待 GPU／WSI 并释放命令，再释放管线与交换链。管线创建失败会清理已创建的 Layout；该类型不自行等待整个 Device，也不提供通用材质／Pass 接口。[Pipeline 销毁要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyPipeline.html)

```powershell
cmake --build build --config Debug --target MadCatTriangleChecks
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug
```

`MadCatTriangleChecks` 复用模拟 Vulkan 测试程序，包含 25 项管线创建、失败清理、格式、动态尺寸和 Draw 检查，不依赖 GPU 或 Shader 工具。窗口脚本会启动程序、改变窗口状态、采集截图并正常关闭，要求桌面可见且无其他窗口遮挡；当前参考颜色适用于 SDR 的 R8／B8 sRGB 交换链和仓库原始 `triangle.slang`。它采样六个背景点和四个三角形内部点，按重心坐标与 sRGB 转换检查插值颜色。

脚本支持 `-Configuration Release`、`-BuildDirectory <构建目录>`，以及 `-Scenario modal`（原生缩放循环）、`stress`（连续尺寸变化与快速恢复）、`close-minimized`（最小化关闭）。实际鼠标拖动的人工验收仍需单独进行。日志、模拟检查结果和截图均位于 `<构建目录>/verification/triangle/`。

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
| [`VulkanTrianglePipeline`](src/RHI/Vulkan/VulkanTrianglePipeline.h) | 拥有最小三角形管线与空 Layout，借用 Device，封装绑定、viewport／scissor 与 Draw |
| [`VulkanClearRenderer`](src/RHI/Vulkan/VulkanClearRenderer.h) | 借用同一 Device 的 Swapchain 和可选三角形管线，拥有图像视图、一个命令池／命令缓冲、两个二进制信号量及 acquire／submit／present Fence，记录清屏和可选 Draw 并提交 |

`queryPhysicalDevices(instance, devices)` 不要求先创建 Surface。`querySurfaceSupport(device, surface, support)` 检查两者属于同一个 Instance，然后查询实际窗口支持；`printPhysicalDeviceReport` 仅打印数据，不再查询 Vulkan。两种查询都在成功时整体替换输出，失败时保留原结果；保存的数据不包含指向临时查询结构的 `pNext` 指针。

`VulkanDevice::initialize(candidates)` 要求所有快照属于同一个 Instance 和 Surface，复制所选物理设备的数据，不保留候选容器引用或 Surface 支持快照。`physicalDeviceInfo()` 的只读引用在 Device 对象销毁前有效，其中的 Instance 和物理设备句柄仍是借用的；初始化前查询数据为空。创建失败时保持空状态并释放部分创建的 Device；重复初始化被拒绝且保留现有 Device。队列不单独销毁，其句柄在 Device 释放时失效。Device 命令通过 `vkGetDeviceProcAddr` 获取；创建前已解析核心销毁函数，确保命令加载失败仍能清理。

`VulkanSwapchain::initialize(device, surface, framebufferExtent)` 要求 Device 和 Surface 已初始化且属于同一个 Instance，尺寸以像素为单位。刷新 Surface 支持后再次确认所用队列能够呈现，并创建交换链、枚举图像；`VK_INCOMPLETE` 会重试枚举。失败及图像枚举期间的异常会清理部分创建的交换链，所有者保持空状态，支持重试；重复初始化被拒绝且保留原交换链。`images()` 返回的 span 和其中的图像句柄在交换链销毁后失效；图像由交换链管理，不单独销毁。[图像枚举](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainImagesKHR.html)

组装顺序见 [`Sandbox`](apps/Sandbox/main.cpp)：Instance、Surface、Device、Shader Module 先创建，`SandboxApplication` 随后拥有可替换的 `Presentation`，后者按 Swapchain、管线、帧渲染器的顺序声明成员，保证先完成等待并释放命令资源。重建策略与窗口回调在应用层，Vulkan 资源和实际等待仍由后端执行。帧渲染器的 `waitIdle()` 和析构在释放自身资源前等待 acquisition、提交和呈现资源退役；管线与 Swapchain 析构不自动等待 GPU。应用销毁前注销回调并清除窗口用户指针。窗口须活到 Surface 释放后，Instance 须活到 Device／Surface 释放后；Device 最后的析构等待不能保护此前已释放的子资源。[Device 销毁要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyDevice.html)、[Swapchain 销毁要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroySwapchainKHR.html)

每帧等待上一帧三个完成条件后复用资源；acquire 使用 50 ms 超时，无可用图像时返回 `Retry`，不重置提交 Fence。成功后记录 `UNDEFINED`／`PRESENT_SRC_KHR` → `COLOR_ATTACHMENT_OPTIMAL` → `PRESENT_SRC_KHR` 转换；提交等待阶段为颜色附件输出，完成信号覆盖全部命令。只等待确实已发起的工作；录制／提交失败仍等待已发起的 acquisition，present 的 out-of-date／surface-lost 与内存不足排队语义分开处理。[acquire 要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkAcquireNextImageKHR.html)、[present 错误语义](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html)

呈现 Fence 确认呈现资源可退役，不能用提交 Fence 或单独的 `vkDeviceWaitIdle` 替代。本轮等待呈现 Fence 后再复用唯一的 render-finished 信号量，保持一个在途帧，不以此宣称性能结果。清屏器属于后端基础验证路径，未来由 Pass／Render Graph 执行替代。帧错误后停止后续提交；完成等待失败会诊断，析构重试仍失败时终止进程，避免释放无法确认完成的资源；设备丢失恢复未实现。[呈现信号量与退出问题](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)、[呈现 Fence 语义](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainPresentFenceInfoKHR.html)

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
