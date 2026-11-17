# 用 C++ 编写 HDR 显示 Pass

[CustomPass/main.cpp](../examples/CustomPass/main.cpp) 是完整用户程序；场景创建方式沿用 [Hello World](hello-world.md)。本例把场景渲染到浮点纹理，再用实验自己的 [ToneMapPass.h](../examples/CustomPass/ToneMapPass.h) 和 [display.slang](../examples/CustomPass/display.slang) 完成曝光与色调映射。

## 构建与运行

先完成 [README 的环境准备与配置](../README.md#构建与运行)，在仓库根目录执行：

```powershell
cmake --build build --config Debug --target CustomPass MadCatShaders CustomPassShaders
.\build\Debug\CustomPass.exe
```

默认显示 Suzanne；也可传入一个 glTF／GLB 文件路径。Shader 编译是手动目标，修改 Slang 后关闭程序、构建 `CustomPassShaders`，再重新启动；修改 C++ 后构建 `CustomPass`。编译失败保留磁盘上上一份已验证产物，当前没有运行期替换。

| 按键 | 效果 |
|---|---|
| 1 | 默认曝光、灯光和模型位置 |
| 2 | 曝光 −2 EV，亮度乘以 1/4 |
| 3 | 曝光 +2 EV，亮度乘以 4 |
| 4 | 关闭方向光，恢复默认曝光和位置 |
| 5 | 移动模型，恢复默认曝光和灯光 |

曝光变化沿用已有图与管线，只更新本帧参数。

## 从场景颜色到显示结果

本例连接 `ClearHDR → ForwardHDR → ToneMap → Present`。`ApplicationOptions::forwardColorFormat` 设为 `Rgba16Float`，让现有 Lambert 前向管线输出线性 HDR。图创建一张同窗口大小、单 mip／层的 RGBA16F 纹理：

```cpp
const auto hdr=graph.createColorTexture("SceneHDR",{input.width,input.height});
```

Clear 初始化 HDR 与深度，Forward 通过资源新版本保留并写入这些附件。ToneMap 显式声明采样 Forward 的颜色输出，并覆盖写入交换链颜色。Present 接收 ToneMap 的输出版本，指定最终显示结果；实际提交和呈现由运行层执行。完整连接代码见 [main.cpp](../examples/CustomPass/main.cpp)。

HDR 纹理能保存大于 1 的颜色值。本例将方向光辐照度设为 `4π`；参考平面的线性光照颜色为 `(1, 1.6, 1.8)`，在显示转换前保持这些值。Shader 先乘 `exp2(exposureEV)`，再计算 Reinhard 映射 `x/(1+x)`。输出仍为线性颜色，sRGB 交换链附件只执行一次显示编码。实验检查实际呈现附件是否为 sRGB；不兼容时给出诊断，避免静默输出错误亮度。这是 SDR 显示转换，没有启用 HDR 显示器输出模式。

## 声明、准备与执行

[ToneMapPass](../examples/CustomPass/ToneMapPass.h) 使用普通结构体表达三个阶段：

- `declare` 返回本图的输入和输出句柄，声明 HDR 采样读取与显示颜色覆盖写入。
- `prepare` 通过建图入口提供的 `prepareGraphics` 服务创建 Shader／布局／管线，返回 `Prepared`。描述指定两个 SPIR-V 路径、采样布局和 4 字节曝光参数。
- `execute` 显式接收 `Declared`、`Prepared` 和本帧 `Parameters`，绑定并绘制。

关键执行代码为：

```cpp
return context.beginRendering(declared.outputs.color,{},madcat::rhi::AttachmentLoad::Clear,{}) &&
    context.bindPipeline(prepared.pipeline) && context.bindSampledTexture(HdrTextureSet,declared.inputs.hdr) &&
    context.pushConstants(std::as_bytes(std::span(&parameters,1))) && context.draw(3) && context.endRendering();
```

`HdrTextureSet` 为 0，图像 binding 0、采样器 binding 1，与 Slang 的显式槽位对应。单个全屏三角形通过顶点 ID 生成位置；普通 Pass 无需使用 Vulkan 类型。曝光必须有限且处于 −16～16 EV，执行前验证。

带 `Prepared` 的 `graph.addPass` 按值保存准备版本和声明。调用方之后修改原来的准备值，不会改变图里的版本。逐帧参数工厂产生每节点参数，记录开始时再复制为本帧快照；执行中不读取“最新”管线或直接读取用户的可变参数。

绑定资源前仍须声明对应访问，旧版本或未声明资源会被拒绝。图处理 HDR 的颜色写入→片元采样，以及再次使用该交换链图像时的采样→颜色写入依赖。采样 Descriptor 创建后不更新，图内纹理和准备版本随帧图保存；运行层等待 GPU／呈现完成并释放命令后才销毁。`prepareGraphics` 只在建图调用中有效，不能保存到运行期外；它返回的 GPU 引用也不能越过本次 `run` 的 Device 生命周期。

## 添加自己的实验和 Shader

参考 [CustomPass/CMakeLists.txt](../examples/CustomPass/CMakeLists.txt)：新建可执行目标并链接 `MadCatRuntime`，使用 `madcat_add_graphics_shaders(MyShaders my-display "${CMAKE_CURRENT_SOURCE_DIR}/display.slang")` 登记独立手动编译目标。此函数在根 CMake 的 Shader 工具配置之后加载，因此实验目录也应登记在其后。

产物进入 `build/shaders/my-display/`，包含 `vertex.spv`、`fragment.spv`、源快照和 manifest。包名只允许字母／数字、下划线和连字符，不能与已有包重名；入口为 `vertexMain`／`fragmentMain`，Shader 当前须为单个自包含文件。C++ 通过构建定义把该目录传给准备阶段。现有单文件编译、验证和失败恢复流程适用于新增图形 Shader 包，无需增加包名白名单。

在已支持的 RHI 能力范围内，修改或新增普通 Pass 只涉及用户 C++、Slang 和构建登记。新增 GPU 能力仍需扩展 RHI 和图的访问模型。

## 当前范围与验证

可编程图形管线目前限一个颜色附件、由顶点 ID 生成的三角形列表，无深度测试或混合；可选一个片元采样 set（image 0／sampler 1），以及按 4 字节对齐、不超过 128 字节的 Vertex／Fragment Push Constants。该范围足以完成本例；现有场景几何绘制仍使用固定 Forward ABI。自定义采样绑定当前覆盖图内纹理，材质贴图仍由 Forward 绑定。图内颜色纹理限 RGBA16F／单 mip／层，采样使用 nearest。没有 Compute、任意 Descriptor 布局、Shader 反射验证、运行期图／Shader 更新或参数面板。

Debug／Release 已通过构建、各 13 组图检查、32 项深度和 68 项既有管线检查；自定义 Shader 编译／SPIR-V 验证和 12 项编译失败恢复通过。两种配置各八个状态、每状态 35 个解析像素通过；两种配置的六项准备／绑定／参数／绘制失败均在提交前拒绝并正确退役。Debug 默认 Suzanne 为可见性冒烟，已查看截图；最小化直接退出也通过。GPU 环境为 RTX 5060 Ti／NVIDIA 617.42，RGBA16F、sRGB 格式 50、D32_SFLOAT。Debug 开启同步验证，无 warning／error；Release 未主动启用验证层。未验证其他 GPU、实际提交失败或性能。

可重复执行：

```powershell
cmake --build build --config Debug --target CustomPass CustomPassChecks CustomPassShaders CustomPassShaderChecks MadCatRenderGraphChecks
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario reference
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario failures
```

窗口脚本需要桌面会话，截图检查应串行运行。日志和截图保存在 `build/verification/custom-pass/`。
