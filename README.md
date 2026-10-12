# MadCat

使用自研 ECS、Vulkan 和 Slang 的渲染引擎，目前实现了 GLFW 窗口、Vulkan Instance 初始化和 Debug 验证层诊断。

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

程序创建一个 1280 × 720、标题为 MadCat 的可调整大小窗口，查询实例级 API 版本和所需扩展，创建 Vulkan Instance。Debug 构建启用验证层，输出 warning／error 诊断及消息标识；Release 构建关闭应用主动启用的验证层。点击关闭按钮后先销毁诊断回调和 Instance，再释放窗口与 GLFW。初始化失败时返回非零状态。

正常 Debug 启动日志包含以下内容，关闭窗口后输出 `Vulkan: Instance destroyed.`：

```text
Vulkan: instance-level API support <运行时版本>; requesting 1.3.0.
Vulkan: Instance created.
Vulkan: validation enabled (VK_LAYER_KHRONOS_validation); diagnostic callback ready.
```

窗口使用 `GLFW_NO_API`，目前使用 `glfwWaitEvents()` 等待事件。Vulkan 函数通过 GLFW 动态获取，CMake 使用 `Vulkan::Headers` 提供头文件。[GLFW Vulkan 指南](https://www.glfw.org/docs/3.4/vulkan_guide.html)、[CMake FindVulkan](https://cmake.org/cmake/help/latest/module/FindVulkan.html)

当前尚未枚举或选择物理设备，未创建 Surface、Device 或 Swapchain，也没有提交 GPU 工作或绘制内容。实例级 API 版本不能替代后续设备 feature 查询。

## 常见初始化问题

| 诊断 | 检查方法 |
|---|---|
| CMake 找不到 Vulkan | 确认 SDK 已安装、`VULKAN_SDK` 指向正确目录，重新配置 |
| 找不到可用的 loader／driver，或缺少窗口 Surface 扩展 | 检查显卡驱动安装，以及 `VK_DRIVER_FILES` 等驱动搜索设置 |
| 实例级 API 低于 Vulkan 1.3 | 检查运行时 loader 和驱动；安装较新 SDK 不代表运行时已更新 |
| Debug 缺少 `VK_LAYER_KHRONOS_validation` | 确认 SDK 验证层已安装，检查 `VK_LAYER_PATH` 是否覆盖了正常搜索路径 |
| Vulkan API 调用失败 | 日志包含操作名称与数值 `VkResult`；同时检查此前的验证消息 |

完整需求和后续计划见 [项目开发文档](docs/project-development.md)，协作规则见 [AGENTS.md](AGENTS.md)。
