# 用 C++ 编写 HDR 显示 Pass

[CustomPass/main.cpp](../examples/CustomPass/main.cpp) 是完整用户程序；场景创建方式沿用 [Hello World](hello-world.md)。本例把场景渲染到浮点纹理，再用实验自己的 [ToneMapPass.h](../examples/CustomPass/ToneMapPass.h) 和 [display.slang](../examples/CustomPass/display.slang) 完成曝光与色调映射。

## 构建与运行

先完成 [README 的环境准备与配置](../README.md#构建与运行)，在仓库根目录执行：

```powershell
cmake --build build --config Debug --target CustomPass MadCatShaders CustomPassShaders CustomPassHdrShaders
.\build\Debug\CustomPass.exe
```

默认显示 Suzanne；也可传入一个 glTF／GLB 文件路径。Shader 编译是手动目标，修改 Slang 后关闭程序、构建对应目标，再重新启动：`CustomPassShaders` 编译最终显示，`CustomPassHdrShaders` 编译 HDR 范围查看；修改 C++ 后构建 `CustomPass`。编译失败保留磁盘上上一份已验证产物，当前没有运行期 Shader 包更新流程。

仓库另包含 [Lantern](../assets/models/Lantern/README.md) 和 [Box](../assets/models/Box/README.md)。`CustomPass.exe --fit assets/models/Lantern/Lantern.gltf` 会按选中场景的节点世界空间 AABB 居中、等比缩放；不带 `--fit` 时保留原有 .7 模型缩放。源文件、节点和贴图不作转换。

窗口中的参数面板支持曝光 −8～8 EV、方向光线性 RGB（各通道 0～1）、辐照度 0～32 和 Restore defaults。滑条支持拖动或 Ctrl＋单击输入；颜色可直接输入浮点数。无效、非有限或越界的请求保留上一份有效参数并显示诊断。面板占用键盘焦点时，数字键用于编辑；单击场景区域后可使用下列快捷键。

| 按键 | 效果 |
|---|---|
| 1 | 默认曝光、灯光和模型位置 |
| 2 | 曝光 −2 EV，亮度乘以 1/4 |
| 3 | 曝光 +2 EV，亮度乘以 4 |
| 4 | 关闭方向光，恢复默认曝光和位置 |
| 5 | 移动模型，恢复默认曝光和灯光 |

曝光变化沿用已有图与管线，只更新本帧参数。

## 从场景颜色到显示结果

本例连接 `ClearHDR → ForwardHDR → ToneMap → UI → Present`。`ApplicationOptions::forwardColorFormat` 设为 `Rgba16Float`，让现有 Lambert 前向管线输出线性 HDR。图创建一张同窗口大小、单 mip／层的 RGBA16F 纹理：

```cpp
const auto hdr=graph.createColorTexture("SceneHDR",{input.width,input.height});
```

Clear 初始化 HDR 与深度，Forward 通过资源新版本保留并写入这些附件。ToneMap 显式声明采样 Forward 的颜色输出，并覆盖写入交换链颜色。UI 保留并叠加显示颜色，Present 接收 UI 的输出版本；实际提交和呈现由运行层执行。完整连接代码见 [main.cpp](../examples/CustomPass/main.cpp)。

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

## 编写参数面板

实验目标链接 `MadCatRuntime` 与 `MadCatImGui`，用户代码包含 `imgui.h`。设置 `SceneCallbacks::ui`（或进阶入口的 `GraphicsCallbacks::ui`）后，运行层在每次可渲染帧中启动 ImGui 帧、调用面板，再执行场景更新／提取和参数固定。用户回调只描述控件，不调用 ImGui 的 NewFrame／Render，也不自行提交或呈现。

```cpp
callbacks.ui = [&] {
    auto draft = displayParameters;
    ImGui::Begin("Display");
    const bool edited = ImGui::SliderFloat("Exposure (EV)", &draft.exposureEV, -8, 8);
    ImGui::End();
    if (edited && std::isfinite(draft.exposureEV) && std::abs(draft.exposureEV) <= 8)
        displayParameters = draft;
};
```

这是回调写法示例；完整程序中的 `request` 同时校验曝光、颜色和辐照度，并在无效时保持有效配置。UI 不持有图节点的可写引用；图参数仍在记录边界复制。普通调参不重新准备 GPU 资源。

开启回调时，建图代码必须加入一次 [UiPass](../src/Passes/UiPass.h)，将 `input.uiFont` 作为采样输入，并让 Present 使用 UI 的输出：

```cpp
const auto ui=graph.addPass("UI",passes::UiPass{},
    passes::UiPass::Inputs{toneMap.outputs.color,input.uiFont});
const auto present=graph.addPass("Present",passes::PresentPass{},
    passes::PresentPass::Inputs{ui.outputs.color});
```

参数工厂须为 UI 节点提供 `passes::UiPass::Parameters{}`。该 Pass 声明保留颜色及字体采样；普通 Pass 不接触 Vulkan。UI 的 Vulkan 适配使用官方后端，字体经 RHI 上传完成后以明确 Sampled 状态导入，私有动态顶点／索引缓冲在前帧完成后才写入。窗口重建和退出先等待并释放命令，再回收 UI 管线、Descriptor、Sampler 和字体。当前只接受该固定字体，未声明的用户图片和任意绘制回调会被拒绝；主相机深度通过独立的图节点转换后在主视口显示，见[主相机深度查看](#主相机深度查看)。

UI 采用内置 Latin 字体和单窗口。暗色样式的 RGB 转换为线性值后打包，alpha 保持覆盖率，由 sRGB 附件编码一次；自定义绘制颜色也须按线性值提供。运行时更改字体、字号、DPI 字体重建、Docking 和多窗口尚未接入。

## 运行期 Forward 开关

面板的 Forward 复选框提交一次图切换请求，Active graph 显示实际生效状态。最终画面模式关闭 Forward 时，[DisplayGraph.h](../examples/CustomPass/DisplayGraph.h) 使用 ClearHDR 的颜色直接连接 ToneMap，图为 `ClearHDR → ToneMap → UI → Present`；开启时恢复 ForwardHDR。Clear 保证输入已初始化，ToneMap 与必要的 sRGB 呈现仍执行。Restore defaults 同时恢复 Forward 开启和 Final image。

普通数值修改继续更新帧参数；改变 Pass 连接则创建一个新的工厂。工厂把 `DisplaySettings{forwardEnabled, view}` 按值捕获，为每张交换链图像生成同一配置。回调和捕获的 CPU 状态覆盖整个 run，GPU 引用继续按运行层寿命约束持有。

```cpp
std::optional<madcat::runtime::GraphRequest> pendingGraph;
uint64_t graphRequestId = 0;
auto requestForward = [&](bool enabled) {
    pendingGraph = madcat::runtime::GraphRequest{++graphRequestId,
        experiment::makeDisplayGraph(displayParameters, shaderRoot,
            experiment::DisplaySettings{enabled, experiment::DisplayView::Final})};
};
callbacks.graphRequest = [&] { return std::exchange(pendingGraph, std::nullopt); };
callbacks.graphUpdated = [&](const madcat::runtime::GraphUpdate& update) {
    // update.applied 为 true 时更新生效配置；否则显示 update.diagnostic。
};
```

这是围绕现有变量的接入片段，完整程序见 [main.cpp](../examples/CustomPass/main.cpp)。`shaderRoot` 指向配置的 `build/shaders`，工厂按所选模式使用其中的 `custom-display` 或 `custom-hdr-range` 包。请求 ID 必须非零并递增；每次请求只返回一次，失败不自动重试。运行层在 UI 完成后、场景提取前消费请求并通知结果。资源相关参数可在成功通知时一起更新，执行阶段仍固定本帧数据。

RHI 先为所有交换链图像准备候选图和中间资源。任一图编译／准备失败或准备回调抛异常，全部候选回收，当前图保持有效。全部成功后，等待前次 acquire／submit／present 完成，再重置命令池并统一替换图引用。交换链呈现状态及共享深度状态保留，新中间资源从未初始化状态开始。切换不重建交换链、UI 后端、场景几何或材质；当前会重新准备各图的显示管线和 HDR 中间资源，没有缓存或性能承诺。

窗口 resize／恢复使用最后成功的工厂。首次启动、尺寸重建、GPU 完成等待或命令重置失败仍可能停绘；当前没有旧交换链保留或设备丢失恢复。候选发布验证编译、准备和最终 Present 交接，不能证明任意用户 C++ execute 都会成功；执行／提交失败仍停止渲染。Shader 包一致性、运行期编译与手动应用流程尚未接入，修改 Shader 仍按前文关闭程序后重编。

专项检查：

```powershell
cmake --build build --config Debug --target CustomPass GraphSwitchChecks
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario graph
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario graph-failures
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario graph -ModelPath assets/models/Lantern/Lantern.gltf
```

`graph` 点击真实复选框连续切换 12 次，并验证关闭状态的 resize／最小化恢复和再次开启；解析平面使用 35 个独立参考像素，外部资源使用排除 UI 的密集采样检查可见性／背景。`graph-failures` 覆盖 9 个请求：第二张图像准备失败、准备异常、缺失 Present、无效 ID、空工厂及后续恢复；每个图的参数工厂检查当前有效配置，防止部分发布或执行旧计划。另用 `graph-close-minimized` 检查关闭 Forward 后最小化直接退出。

本增量 Debug／Release 构建及各 14 组图检查通过；`graph` 各 17 状态 × 35 点与 `graph-failures` 各 9 请求通过。Debug Lantern／Box 通过连续开关和窗口生命周期，Release 两份资源通过显示／恢复；Debug 关闭状态最小化退出通过。两种配置参数面板与既有 CustomPass 失败回归、Debug 原 HDR、UI 失败、BasicScene 和应用失败回归通过。实际为 RTX 5060 Ti／NVIDIA 617.42，Debug 同步验证无 warning／error，Release 未主动启用验证层；外部资源仅为可见性检查，未测性能或其他设备。用户 Shader 未修改／重编。

## 主相机深度查看

2026-11-19 在同一 CustomPass 参数面板加入 View 下拉框。Final image 显示原 HDR／ToneMap 结果；Camera depth 改为 `ClearHDR → ForwardHDR → DepthView → UI → Present`。关闭 Forward 时移除该节点，深度清除值为 1，因此主视口为黑色，UI 仍可操作。Active view 显示实际生效模式，候选准备失败时保留旧画面与生效设置。

深度使用 `1 - deviceDepth` 灰度：近处较亮，远处较暗，背景为黑色。这不是线性距离，也不应用曝光、灯光或 Reinhard。该模式在主视口显示主相机深度，没有 UI 内纹理缩略图或像素拾取；HDR 范围查看见下一节。

示例启动时设置 `application.prepareDepthView=true`，准备现有深度显示管线和 `depth-view` Shader 包。图通过 `graph.createDepthTexture("SceneDepth", {input.width,input.height})` 声明单 mip／层的可采样深度；Clear 和 Forward 写入，已有 [DepthViewPass](../src/Passes/DepthViewPass.h) 读取当前输出版本并覆盖显示颜色，UI 随后保留颜色叠加。图安排 Depth→Sampled 和下一帧 Sampled→Depth，状态只在提交成功后登记。

两种模式都使用图内深度，按交换链图像各分配一份；HDR 中间附件仍保留。切换捕获 Forward／View 的完整配置，所有候选成功且旧命令退役后统一发布；resize 使用最后成功的配置。最终模式准备 ToneMap，深度模式复用运行层的深度显示管线。曝光与灯光参数保留，切回最终画面后继续生效；Restore defaults 同时恢复默认参数、模型位置、Final image 和 Forward 开启。没有新增依赖或 Vulkan 接口。

可重复执行（`Release` 可替换 `Debug`）：

```powershell
cmake --build build --config Debug --target CustomPass GraphSwitchChecks MadCatRenderGraphChecks
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario viewer
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario viewer-close-minimized
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario graph-failures
```

`viewer` 使用独立平面、相机 near=.1／far=1000／距离 3 的解析参考，验证六次模式切换、深度与曝光／光照独立、模型移动、Forward 开关、resize、最小化恢复和默认恢复，每状态检查 35 个场景像素及一个 UI 混合像素。`viewer-close-minimized` 在深度模式最小化后直接退出。候选失败检查同时核对 Forward／View 配置，覆盖部分图像准备失败、准备异常、无效请求和恢复，并检查深度资源回收。截图和日志位于 `build/verification/custom-pass`。

Debug／Release 的 CustomPass、GraphSwitchChecks 构建及各 15 组图检查通过，无编译警告。两种配置的 viewer 各 19 状态 × 35 个独立场景像素及 UI 混合参考点通过；各 9 个候选请求（4 次发布、5 次保留）同时核对 Forward／View 配置并验证失败回退与资源回收。Debug 深度模式最小化直接退出、参数面板九状态回归、Suzanne 深度／最终画面切换与窗口生命周期冒烟通过，截图已查看。实际为 RTX 5060 Ti／NVIDIA 617.42，RGBA16F 97／sRGB 50／D32_SFLOAT 126；Debug 开启同步验证，无 warning／error，Release 未主动启用验证层。未修改或重编 Shader，沿用既有 SPIR-V。Suzanne 属于可见性检查，不是完整外观验收；其他 GPU／DPI、真实 GPU 等待／提交失败和性能未验证。

## HDR 范围查看

2026-11-19 继续在 View 下拉框加入 HDR range。它读取 ToneMap 前的 SceneHDR，图为 `ClearHDR → ForwardHDR → HDRRange → UI → Present`；关闭 Forward 时只显示经过初始化的 HDR 背景。Final image、Camera depth 和 HDR range 均沿用候选图切换，失败保留当前有效画面，窗口重建使用最后成功的配置。Restore defaults 恢复 Final image、Forward 开启和原有默认参数。

| SceneHDR 中存储的 RGB | 显示含义 |
|---|---|
| 任一分量严格大于 1 | 整像素标记为洋红色 `(1,0,1)` |
| 所有分量都在 0～1 | 直接显示线性 RGB，由 sRGB 附件编码一次 |
| 负分量且没有分量大于 1 | 负分量夹到 0 后预览 |

标记依据 RGBA16F 中实际存储的值判断，恰好等于 1 不标记。该视图不乘曝光，也不执行 Reinhard：调整曝光只改变 Final image；调整灯光、材质和场景会改变源 HDR 数据及标记。洋红色是诊断颜色，原始洋红色材质也可能显示同样颜色；本视图没有数值拾取、单通道／亮度显示或 NaN 专用标记，也不是 HDR 显示器输出模式。

新增实验侧 [HdrRangePass](../examples/CustomPass/HdrRangePass.h) 和 [hdr-range.slang](../examples/CustomPass/hdr-range.slang)。Pass 声明当前 HDR 版本的 Sampled 读取及呈现颜色覆盖写入，使用现有显式图形管线、set 0 图像／采样器绑定和全屏三角形，无 Push Constants；图处理 Color→Sampled 以及下一帧 Sampled→Color。UI 随后保留并叠加转换后的颜色，没有向 ImGui 传递未声明的图内图片。该增量不修改 Vulkan 后端或图编译器，也没有新增依赖。

`CustomPassHdrShaders` 是独立的手动目标，产物进入 `build/shaders/custom-hdr-range`，包括两个 SPIR-V、源快照与 manifest；`CustomPassHdrShaderChecks` 检查编译、验证与磁盘发布失败恢复。C++ 构建不隐式编译 Shader。若新包缺失，选择该视图时准备失败并保留当前有效图；首次启动和窗口重建仍按既有失败边界处理。

```powershell
cmake --build build --config Debug --target CustomPass CustomPassHdrShaders CustomPassHdrShaderChecks GraphSwitchChecks MadCatRenderGraphChecks
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario hdr
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario hdr-close-minimized
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario graph-failures
```

`hdr` 用解析平面与独立的半精度存储／sRGB 参考检查三种视图切换、普通 RGB 预览、等于 1／小于 1、分别只有 R／G／B 大于 1、曝光独立性、关灯／模型移动、Forward 开关、resize、最小化恢复和默认恢复。每状态检查 35 个场景像素及一个 UI 混合参考点；普通参数修改不创建新管线。`hdr-close-minimized` 在 HDR 视图最小化后直接退出；`graph-failures` 同时覆盖 HDR、深度及最终画面的候选失败／恢复与资源回收。

Debug／Release 的 CustomPass、GraphSwitchChecks、CustomPassChecks 构建及各 16 组 CPU 图检查通过，无编译警告；配置仅保留既有 GLM 弃用提示。新 Shader 包编译／SPIR-V 验证和 12 项编译／发布失败恢复检查通过，反汇编核对 Fragment 入口、set 0 的 image 0／sampler 1 及无 Push Constants。两种配置的 hdr 各 24 状态 × 35 个场景像素及 UI 混合参考点通过，涵盖等于 1／小于 1、各 RGB 分量独立超范围和曝光独立性；各 9 个候选请求（4 次发布／5 次保留）通过，覆盖三种视图的失败回退及资源回收。Debug 深度查看 19 状态、参数面板九状态回归，以及 Suzanne HDR 显示／resize／最小化直接退出冒烟通过，截图已查看。设备为 RTX 5060 Ti／NVIDIA 617.42，RGBA16F 97／sRGB 50／D32_SFLOAT 126；Debug 同步验证无 warning／error，Release 未主动启用验证层。旧 Shader 源码及产物未重编；其他 GPU／DPI、真实 GPU 等待／重置／提交失败及性能未验证。

## 添加自己的实验和 Shader

参考 [CustomPass/CMakeLists.txt](../examples/CustomPass/CMakeLists.txt)：新建可执行目标并链接 `MadCatRuntime`，使用 `madcat_add_graphics_shaders(MyShaders my-display "${CMAKE_CURRENT_SOURCE_DIR}/display.slang")` 登记独立手动编译目标。此函数在根 CMake 的 Shader 工具配置之后加载，因此实验目录也应登记在其后。

产物进入 `build/shaders/my-display/`，包含 `vertex.spv`、`fragment.spv`、源快照和 manifest。包名只允许字母／数字、下划线和连字符，不能与已有包重名；入口为 `vertexMain`／`fragmentMain`，Shader 当前须为单个自包含文件。C++ 通过构建定义把该目录传给准备阶段。现有单文件编译、验证和失败恢复流程适用于新增图形 Shader 包，无需增加包名白名单。

在已支持的 RHI 能力范围内，修改或新增普通 Pass 只涉及用户 C++、Slang 和构建登记。新增 GPU 能力仍需扩展 RHI 和图的访问模型。

## 当前范围与验证

可编程图形管线目前限一个颜色附件、由顶点 ID 生成的三角形列表，无深度测试或混合；可选一个片元采样 set（image 0／sampler 1），以及按 4 字节对齐、不超过 128 字节的 Vertex／Fragment Push Constants。该范围足以完成本例；UI 的索引绘制和混合由专用后端负责，不代表通用 RHI 已支持这些能力。现有场景几何绘制仍使用固定 Forward ABI。自定义采样绑定当前覆盖图内纹理，材质贴图仍由 Forward 绑定。图内颜色纹理限 RGBA16F／单 mip／层，采样使用 nearest。已有受限整图候选切换；Compute、任意 Descriptor 布局、Shader 反射验证和运行期 Shader 更新待实现。

2026-11-17 的 HDR 增量已通过 Debug／Release 构建、各 13 组图检查、32 项深度和 68 项既有管线检查；自定义 Shader 编译／SPIR-V 验证和 12 项编译失败恢复通过。两种配置各八个状态、每状态 35 个解析像素通过；两种配置的六项准备／绑定／参数／绘制失败均在提交前拒绝并正确退役。Debug 默认 Suzanne 为可见性冒烟，已查看截图；最小化直接退出也通过。GPU 环境为 RTX 5060 Ti／NVIDIA 617.42，RGBA16F、sRGB 格式 50、D32_SFLOAT。Debug 开启同步验证，无 warning／error；Release 未主动启用验证层。未验证其他 GPU、实际提交失败或性能。

可重复执行：

```powershell
cmake --build build --config Debug --target CustomPass CustomPassChecks CustomPassShaders CustomPassShaderChecks MadCatRenderGraphChecks
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario reference
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario failures
```

窗口脚本需要桌面会话，截图检查应串行运行。日志和截图保存在 `build/verification/custom-pass/`。

参数面板的专项检查：

```powershell
cmake --build build --config Debug --target CustomPass UiChecks MadCatRenderGraphChecks
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario ui
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario ui-failures
```

`ui` 通过目标窗口的鼠标／键盘事件编辑控件，核对曝光、辐照度、颜色、无效请求、恢复默认值、键盘焦点、窗口重建和独立参考像素；不向桌面发送全局键盘事件。`ui-failures` 覆盖控件回调未闭合窗口即抛异常、未声明用户纹理、漏报字体读取、重复 UI 绘制和建图失败，均须在提交前拒绝并清理上下文。

2026-11-18 两种配置构建和各 14 组图检查通过；`ui` 各 9 状态 × 35 个独立解析像素及 UI 混合参考通过，调参不增加图形管线或 UI GPU 后端创建次数。`ui-failures` 各 5 项通过，并回归原 HDR 八状态和六项失败边界。Debug 另通过默认 Suzanne 冒烟、最小化退出、BasicScene 七状态参考与五项应用失败检查。实际环境沿用上述 GPU／格式，Debug 同步验证无 warning／error，Release 未主动启用验证层；本次未修改或重编用户 Shader。不同 GPU／DPI、运行期字体和真实提交失败尚未验证。
