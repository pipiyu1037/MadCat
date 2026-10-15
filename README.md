# MadCat

使用自研 ECS、Vulkan 和 Slang 的渲染引擎，目前实现了 GLFW 窗口、Vulkan Instance 与 Surface、Debug 验证层诊断，以及设备和窗口呈现能力报告。

## 构建与运行

需要 Windows 11、Visual Studio 2022 的 C++ 桌面开发工具、CMake 3.25 或更新版本、Git，以及 [Vulkan SDK](https://vulkan.lunarg.com/)（头文件版本至少 1.3，本机验证版本为 1.4.363.0）。运行时需要 Vulkan loader 和显卡驱动，实例级 API 必须支持 Vulkan 1.3 或更新版本。

安装 SDK 后重新打开 PowerShell，确认 `$env:VULKAN_SDK` 指向 SDK 安装目录。Debug 构建要求 `VK_LAYER_KHRONOS_validation` 可用；缺少验证层时会输出诊断并退出。

在能调用 CMake 和 Git 的 PowerShell 中，从项目根目录执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\MadCat.exe
```

首次配置通过 CMake FetchContent 获取固定提交的 GLFW 3.4，需要访问 GitHub；依赖源码和构建产物位于 `build` 下。

程序创建一个 1280 × 720、标题为 MadCat 的可调整大小窗口，查询实例级 API 版本和所需扩展，创建 Vulkan Instance 与该窗口的 Surface，并输出全部物理设备的能力报告。Debug 构建启用验证层，输出 warning／error 诊断及消息标识；Release 构建关闭应用主动启用的验证层。点击关闭按钮后依次销毁 Surface、诊断回调、Instance、窗口与 GLFW。初始化、Surface 创建或设备查询失败时返回非零状态，已创建资源仍按此顺序释放。

正常 Debug 启动日志包含以下内容，关闭窗口后依次输出 `Vulkan: Surface destroyed.` 和 `Vulkan: Instance destroyed.`：

```text
Vulkan: instance-level API support <运行时版本>; requesting 1.3.0.
Vulkan: Instance created.
Vulkan: validation enabled (VK_LAYER_KHRONOS_validation); diagnostic callback ready.
Vulkan: window Surface created.
Vulkan: found <数量> physical device(s).
<每张设备的属性、队列族、Surface 能力、扩展、feature 和加速结构限制>
Vulkan: physical device capability report complete; no device selected or features enabled.
```

窗口使用 `GLFW_NO_API`，目前使用 `glfwWaitEvents()` 等待事件。Vulkan 函数通过 GLFW 动态获取，CMake 使用 `Vulkan::Headers` 提供头文件。[GLFW Vulkan 指南](https://www.glfw.org/docs/3.4/vulkan_guide.html)、[CMake FindVulkan](https://cmake.org/cmake/help/latest/module/FindVulkan.html)

能力报告包含设备名称、类型、设备级 API 与驱动信息，队列族的 Graphics／Compute／Transfer 能力及对当前窗口的 Present 支持，以及基础渲染和光追相关扩展。通过 `vkGetPhysicalDeviceFeatures2` 分别查询 `dynamicRendering`、`synchronization2`、`bufferDeviceAddress`、`accelerationStructure` 和 `rayQuery`；已提升为核心的功能会按设备 API 版本判断，不要求同名扩展仍被列出。[Vulkan feature 查询](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceFeatures2.html)

对于支持当前窗口呈现的设备，报告 Surface 格式与颜色空间、present mode、图像数量和尺寸范围、数组层数，以及 usage／transform／composite alpha 标志。`maxImageCount=0` 表示没有 Surface 给出的数量上限，`currentExtent.width=UINT32_MAX` 表示尺寸由应用选择；这些信息不代表已创建交换链。常见枚举显示名称，其余保留原始数值。设备没有呈现队列时说明原因，跳过该设备的 Surface 能力查询并继续报告其他能力；格式或模式列表为空时说明交换链配置不可用。[Surface 能力查询前提](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceCapabilitiesKHR.html)

报告还输出加速结构限制，并检查计算 Ray Query 所需的 feature、Compute 队列和扩展依赖；缺失时说明原因，仍继续报告其他设备。没有物理设备、查询函数缺失或枚举失败时，程序报告错误并退出。

当前已创建 Surface 并查询呈现支持，尚未选择设备、启用 feature 或创建 Device、Swapchain，也没有提交 GPU 工作或绘制内容。Surface 报告是启动时的查询结果，窗口尺寸变化后需重新查询才能用于交换链配置；Ray Query 前置条件可用仍需 Slang 编译与 GPU 求交探针验证。

## Vulkan 后端封装

| 类型 | 当前职责 |
|---|---|
| [`VulkanInstance`](src/RHI/Vulkan/VulkanInstance.h) | 拥有 Instance 和诊断回调，提供后端使用的只读句柄访问 |
| [`VulkanSurface`](src/RHI/Vulkan/VulkanSurface.h) | 独立拥有 Surface，通过 RAII 清理；借用 Instance 和窗口，禁止复制与移动 |
| [`PhysicalDeviceInfo`](src/RHI/Vulkan/VulkanPhysicalDevice.h) | 保存可复制的设备属性、队列族、扩展、feature 和加速结构限制；借用物理设备句柄，不销毁设备 |
| `SurfaceSupport` | 保存一次设备／Surface 组合查询的呈现支持和能力快照 |

`queryPhysicalDevices(instance, devices)` 不要求先创建 Surface。`querySurfaceSupport(device, surface, support)` 检查两者属于同一个 Instance，然后查询实际窗口支持；`printPhysicalDeviceReport` 仅打印数据，不再查询 Vulkan。两种查询都在成功时整体替换输出，失败时保留原结果；保存的数据不包含指向临时查询结构的 `pNext` 指针。

组装顺序见 [`Sandbox`](apps/Sandbox/main.cpp)：Surface 所有者在 Instance 所有者之后声明，保证先销毁 Surface。窗口和 Instance 须活到 Surface 释放后；设备信息的复制不会延长 Instance 生命周期。未来的 Swapchain 必须在关联 Surface 之前销毁。上述类型和原始句柄仅用于 Vulkan 后端及程序组装。

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

完整需求和后续计划见 [项目开发文档](docs/project-development.md)，协作规则见 [AGENTS.md](AGENTS.md)。
