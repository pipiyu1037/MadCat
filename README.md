# MadCat

使用自研 ECS、Vulkan 和 Slang 的渲染引擎，目前实现了 GLFW 窗口、Vulkan 初始化与能力报告、一个在途帧的持续清屏与同步，以及窗口 resize／最小化恢复时的 Swapchain 重建。

## 构建与运行

需要 Windows 11、Visual Studio 2022 的 C++ 桌面开发工具、CMake 3.25 或更新版本、Git，以及 [Vulkan SDK](https://vulkan.lunarg.com/)（头文件版本至少 1.3.239，以提供 EXT maintenance1 类型；本机验证版本为 1.4.363.0）。运行时的实例及所选设备 API 仍要求 Vulkan 1.3。清屏还要求设备的 `VK_EXT_swapchain_maintenance1` 扩展和 `swapchainMaintenance1` feature，以及实例扩展 `VK_EXT_surface_maintenance1`、`VK_KHR_get_surface_capabilities2`。

安装 SDK 后重新打开 PowerShell，确认 `$env:VULKAN_SDK` 指向 SDK 安装目录。Debug 构建要求 `VK_LAYER_KHRONOS_validation` 可用；缺少验证层时会输出诊断并退出。

在能调用 CMake 和 Git 的 PowerShell 中，从项目根目录执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\MadCat.exe
```

首次配置通过 CMake FetchContent 获取固定提交的 GLFW 3.4，需要访问 GitHub；依赖源码和构建产物位于 `build` 下。

程序创建一个 1280 × 720、标题为 MadCat 的可调整大小窗口，输出设备和 Surface 能力报告，选择设备、创建一个 Graphics／Compute／Present 队列及 Swapchain，然后持续呈现深蓝色清屏画面。线性 RGBA 为 `(0.04, 0.08, 0.16, 1.0)`，使用实际交换链格式；本机 sRGB 格式下屏幕像素约为 RGB `(56, 80, 111)`。Debug 启用验证层，Release 不主动启用验证层。关闭前先等待 acquisition、GPU 提交和呈现资源退役，再释放清屏器、Swapchain、Device、Surface、Instance 和窗口；初始化或帧执行失败返回非零状态。

正常 Debug 启动日志包含以下内容，关闭时先输出清屏器释放及累计帧数，再依次输出 `Swapchain destroyed.`、`Device destroyed.`、`Surface destroyed.` 和 `Instance destroyed.`：

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
Vulkan: Swapchain created; <宽> x <高>, format <格式>, color space <颜色空间>, FIFO, <实际数量> images (requested <请求数量>).
Vulkan: Swapchain usage <标志>, transform <标志>, composite alpha <标志>; exclusive sharing.
Vulkan: Clear renderer ready; one frame in flight, dynamic rendering, acquire/submit/present fences.
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

清屏通过动态渲染的颜色附件 `loadOp=CLEAR`／`storeOp=STORE` 完成，无需 Shader 或管线，usage 仍仅为颜色附件。framebuffer 尺寸变化、从最小化恢复、acquire 返回 out-of-date 或 acquire／present 返回 suboptimal 时，应用先等待旧清屏器的 acquire／submit／present 完成，再依次销毁清屏器和 Swapchain，重新查询 Surface 支持并按新尺寸创建一组资源，随后恢复清屏。suboptimal acquire 已获得的图像先完成提交与呈现，再请求重建。

最小化或零像素尺寸时不创建交换链、不获取图像；窗口恢复后自动继续，也可在最小化期间关闭。重建采用同步等待和先销毁后创建，暂不通过 `oldSwapchain` 复用资源；重建失败会清理部分资源、诊断并返回非零状态，不保留旧画面继续运行。Slang、三角形和 Ray Query GPU 求交尚未实现，当前只支持单个 Graphics／Compute／Present 队列族。

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
| [`VulkanClearRenderer`](src/RHI/Vulkan/VulkanClearRenderer.h) | 借用同一 Device 的 Swapchain，拥有图像视图、一个命令池／命令缓冲、两个二进制信号量及 acquire／submit／present Fence，记录并提交清屏 |

`queryPhysicalDevices(instance, devices)` 不要求先创建 Surface。`querySurfaceSupport(device, surface, support)` 检查两者属于同一个 Instance，然后查询实际窗口支持；`printPhysicalDeviceReport` 仅打印数据，不再查询 Vulkan。两种查询都在成功时整体替换输出，失败时保留原结果；保存的数据不包含指向临时查询结构的 `pNext` 指针。

`VulkanDevice::initialize(candidates)` 要求所有快照属于同一个 Instance 和 Surface，复制所选物理设备的数据，不保留候选容器引用或 Surface 支持快照。`physicalDeviceInfo()` 的只读引用在 Device 对象销毁前有效，其中的 Instance 和物理设备句柄仍是借用的；初始化前查询数据为空。创建失败时保持空状态并释放部分创建的 Device；重复初始化被拒绝且保留现有 Device。队列不单独销毁，其句柄在 Device 释放时失效。Device 命令通过 `vkGetDeviceProcAddr` 获取；创建前已解析核心销毁函数，确保命令加载失败仍能清理。

`VulkanSwapchain::initialize(device, surface, framebufferExtent)` 要求 Device 和 Surface 已初始化且属于同一个 Instance，尺寸以像素为单位。刷新 Surface 支持后再次确认所用队列能够呈现，并创建交换链、枚举图像；`VK_INCOMPLETE` 会重试枚举。失败及图像枚举期间的异常会清理部分创建的交换链，所有者保持空状态，支持重试；重复初始化被拒绝且保留原交换链。`images()` 返回的 span 和其中的图像句柄在交换链销毁后失效；图像由交换链管理，不单独销毁。[图像枚举](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainImagesKHR.html)

组装顺序见 [`Sandbox`](apps/Sandbox/main.cpp)：Instance、Surface、Device 先创建，`SandboxApplication` 随后拥有可替换的 `Presentation`，后者按 Swapchain、清屏器的顺序声明成员，保证先释放清屏资源。重建策略与窗口回调在应用层，Vulkan 资源和实际等待仍由后端执行。清屏器的 `waitIdle()` 和析构在释放自身资源前等待 acquisition、提交和呈现资源退役；Swapchain 析构不自动等待 GPU。应用销毁前注销回调并清除窗口用户指针。窗口须活到 Surface 释放后，Instance 须活到 Device／Surface 释放后；Device 最后的析构等待不能保护此前已释放的子资源。[Device 销毁要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyDevice.html)、[Swapchain 销毁要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroySwapchainKHR.html)

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

完整需求和后续计划见 [项目开发文档](docs/project-development.md)，协作规则见 [AGENTS.md](AGENTS.md)。
