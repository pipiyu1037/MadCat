# MadCat

使用自研 ECS、Vulkan 和 Slang 的渲染引擎，目前实现了最小 GLFW 窗口入口。

## 构建与运行

需要 Windows 11、Visual Studio 2022 的 C++ 桌面开发工具、CMake 3.25 或更新版本及 Git。

在能调用 CMake 和 Git 的 PowerShell 中，从项目根目录执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\MadCat.exe
```

首次配置通过 CMake FetchContent 获取固定提交的 GLFW 3.4，需要访问 GitHub；依赖源码和构建产物位于 `build` 下。

程序创建一个 1280 × 720、标题为 MadCat 的可调整大小窗口，等待窗口事件，点击关闭按钮后正常退出。当前尚未绘制内容，也不需要 Vulkan SDK。

窗口使用 `GLFW_NO_API`，为后续 Vulkan 接入准备；目前使用 `glfwWaitEvents()` 避免没有渲染工作时空转。[GLFW 窗口指南](https://www.glfw.org/docs/3.4/window_guide.html)

完整需求和后续计划见 [项目开发文档](docs/project-development.md)，协作规则见 [AGENTS.md](AGENTS.md)。
