# MadCat

使用自研 ECS、Vulkan 和 Slang 的渲染引擎，目前实现了 GLFW 窗口、Vulkan Instance 与 Surface、Debug 验证层诊断、设备和窗口呈现能力报告，以及物理设备选择、逻辑设备、单队列和 Swapchain 创建。

## 构建与运行

需要 Windows 11、Visual Studio 2022 的 C++ 桌面开发工具、CMake 3.25 或更新版本、Git，以及 [Vulkan SDK](https://vulkan.lunarg.com/)（头文件版本至少 1.3，本机验证版本为 1.4.363.0）。运行时需要 Vulkan loader 和显卡驱动，实例及所选物理设备的 API 必须支持 Vulkan 1.3 或更新版本；设备还需满足下文的基础渲染条件。

安装 SDK 后重新打开 PowerShell，确认 `$env:VULKAN_SDK` 指向 SDK 安装目录。Debug 构建要求 `VK_LAYER_KHRONOS_validation` 可用；缺少验证层时会输出诊断并退出。

在能调用 CMake 和 Git 的 PowerShell 中，从项目根目录执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\MadCat.exe
```

首次配置通过 CMake FetchContent 获取固定提交的 GLFW 3.4，需要访问 GitHub；依赖源码和构建产物位于 `build` 下。

程序创建一个 1280 × 720、标题为 MadCat 的可调整大小窗口，查询实例级 API 版本和所需扩展，创建 Vulkan Instance 与该窗口的 Surface，并输出全部物理设备的能力报告。随后选择物理设备、创建逻辑设备并获取一个 Graphics／Compute／Present 队列，再刷新所选设备的 Surface 支持并创建 Swapchain、获取图像。Debug 构建启用验证层，输出 warning／error 诊断及消息标识；Release 构建关闭应用主动启用的验证层。点击关闭按钮后依次销毁 Swapchain、Device、Surface、诊断回调、Instance、窗口与 GLFW。查询、选择或创建失败时返回非零状态，已创建资源仍由各自所有者释放。

正常 Debug 启动日志包含以下内容，关闭窗口后依次输出 `Swapchain destroyed.`、`Device destroyed.`、`Surface destroyed.` 和 `Instance destroyed.`，各行均以 `Vulkan:` 开头：

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
Vulkan: Swapchain created; <宽> x <高>, format <格式>, color space <颜色空间>, FIFO, <实际数量> images (requested <请求数量>).
Vulkan: Swapchain usage <标志>, transform <标志>, composite alpha <标志>; exclusive sharing.
```

窗口使用 `GLFW_NO_API`，目前使用 `glfwWaitEvents()` 等待事件。Vulkan 函数通过 GLFW 动态获取，CMake 使用 `Vulkan::Headers` 提供头文件。[GLFW Vulkan 指南](https://www.glfw.org/docs/3.4/vulkan_guide.html)、[CMake FindVulkan](https://cmake.org/cmake/help/latest/module/FindVulkan.html)

能力报告包含设备名称、类型、设备级 API 与驱动信息，队列族的 Graphics／Compute／Transfer 能力及对当前窗口的 Present 支持，以及基础渲染和光追相关扩展。通过 `vkGetPhysicalDeviceFeatures2` 分别查询 `dynamicRendering`、`synchronization2`、`bufferDeviceAddress`、`accelerationStructure` 和 `rayQuery`；已提升为核心的功能会按设备 API 版本判断，不要求同名扩展仍被列出。[Vulkan feature 查询](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceFeatures2.html)

对于支持当前窗口呈现的设备，报告 Surface 格式与颜色空间、present mode、图像数量和尺寸范围、数组层数，以及 usage／transform／composite alpha 标志。`maxImageCount=0` 表示没有 Surface 给出的数量上限，`currentExtent.width=UINT32_MAX` 表示尺寸由应用选择；这些信息不代表已创建交换链。常见枚举显示名称，其余保留原始数值。设备没有呈现队列时说明原因，跳过该设备的 Surface 能力查询并继续报告其他能力；格式或模式列表为空时说明交换链配置不可用。[Surface 能力查询前提](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceCapabilitiesKHR.html)

报告还输出加速结构限制，并检查计算 Ray Query 所需的 feature、Compute 队列和扩展依赖；缺失时说明原因，仍继续报告其他设备。没有物理设备、查询函数缺失或枚举失败时，程序报告错误并退出。

设备筛选要求设备级 Vulkan 1.3、`VK_KHR_swapchain`、可查询且支持的 `dynamicRendering`／`synchronization2`，以及非空的 Surface 格式和呈现模式、颜色附件 usage 和同一队列族的 Graphics／Compute／Present 支持。合格设备中按独显、集显、其他类型排序，同级保留枚举顺序，选择第一个兼容队列族。日志说明拒绝原因和最终选择；创建失败会报告操作、`VkResult` 与设备信息。当前仅启用上述扩展和两项 feature，光追能力不作为本轮硬性条件，也不启用光追 feature。Vulkan 1.3 的两项核心 feature 仍需显式启用，不要求同名 KHR 扩展列出。[Device 创建](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateDevice.html)、[Vulkan 1.3 feature 启用](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan13Features.html)

Swapchain 优先选择 `B8G8R8A8_SRGB`／`SRGB_NONLINEAR`，否则选第一个有具体格式的返回组合；使用 FIFO 和单队列独占共享。图像数量在 Surface 最小值上加一，并受非零最大值限制；`maxImageCount=0` 表示无该上限。优先使用 Surface 的固定尺寸；允许应用选择尺寸时，将 GLFW framebuffer 像素尺寸约束到支持范围。仅请求颜色附件 usage，使用当前 Surface transform，composite alpha 按 Opaque、Pre-multiplied、Post-multiplied、Inherit 顺序选择受支持项。配置与实际图像数量会记录到日志。[Surface 限制](https://docs.vulkan.org/refpages/latest/refpages/source/VkSurfaceCapabilitiesKHR.html)、[交换链创建参数](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html)

当前尚未 acquire 图像、提交 GPU 工作或呈现画面，也未创建图像视图。创建前已刷新所选设备的 Surface 支持；零像素尺寸会报告失败并退出，resize／最小化后的交换链重建和恢复尚未实现。Ray Query 前置条件可用仍需 Slang 编译与 GPU 求交探针验证。当前仅支持一个队列族同时承担渲染、计算和呈现，不支持分离队列配置。

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

`queryPhysicalDevices(instance, devices)` 不要求先创建 Surface。`querySurfaceSupport(device, surface, support)` 检查两者属于同一个 Instance，然后查询实际窗口支持；`printPhysicalDeviceReport` 仅打印数据，不再查询 Vulkan。两种查询都在成功时整体替换输出，失败时保留原结果；保存的数据不包含指向临时查询结构的 `pNext` 指针。

`VulkanDevice::initialize(candidates)` 要求所有快照属于同一个 Instance 和 Surface，复制所选物理设备的数据，不保留候选容器引用或 Surface 支持快照。`physicalDeviceInfo()` 的只读引用在 Device 对象销毁前有效，其中的 Instance 和物理设备句柄仍是借用的；初始化前查询数据为空。创建失败时保持空状态并释放部分创建的 Device；重复初始化被拒绝且保留现有 Device。队列不单独销毁，其句柄在 Device 释放时失效。Device 命令通过 `vkGetDeviceProcAddr` 获取；创建前已解析核心销毁函数，确保命令加载失败仍能清理。

`VulkanSwapchain::initialize(device, surface, framebufferExtent)` 要求 Device 和 Surface 已初始化且属于同一个 Instance，尺寸以像素为单位。刷新 Surface 支持后再次确认所用队列能够呈现，并创建交换链、枚举图像；`VK_INCOMPLETE` 会重试枚举。失败及图像枚举期间的异常会清理部分创建的交换链，所有者保持空状态，支持重试；重复初始化被拒绝且保留原交换链。`images()` 返回的 span 和其中的图像句柄在交换链销毁后失效；图像由交换链管理，不单独销毁。[图像枚举](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainImagesKHR.html)

组装顺序见 [`Sandbox`](apps/Sandbox/main.cpp)：依次声明 Instance、Surface、Device 和 Swapchain，保证反向销毁；窗口和 Instance 须活到 Surface 释放后，Instance 也须活到 Device 释放后。Swapchain 必须先于 Device／Surface 释放；其析构不自动等待 GPU，调用方须先完成图像使用并释放依赖对象（如图像视图）。本轮没有 acquire、提交或呈现操作。Device 析构时等待设备空闲再销毁，等待失败仍报告诊断并销毁 Device。GPU 子资源必须在相关 GPU 工作完成后、Device 销毁前释放，不能依赖最后的析构等待保护更早发生的子资源销毁。[Device 销毁要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyDevice.html)、[Swapchain 销毁要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroySwapchainKHR.html) 查询数据的复制不会延长 Instance 生命周期。上述类型和原始句柄仅用于 Vulkan 后端及程序组装。

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

完整需求和后续计划见 [项目开发文档](docs/project-development.md)，协作规则见 [AGENTS.md](AGENTS.md)。
