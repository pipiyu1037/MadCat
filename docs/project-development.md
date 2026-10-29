# MadCat 项目开发文档

MadCat 是一个使用自研 ECS 和 Vulkan 的渲染引擎。前期服务渲染算法研究和图形效果实验，长期考虑游戏与仿真用途。开发者应能加载模型、用 C++ 编写渲染 Pass、连接多个 Pass，并在运行时调整参数、查看中间结果。

本文维护开发范围、实现进度、半年计划和验收方法；架构方案独立维护于[架构设计文档](architecture-design.md)。两份文档供项目开发者及协作编码助手使用。文档更新日期为 2026 年 10 月 29 日。**已确认的需求是项目约束；标为“建议”的实现方案可以随实际验证修订。功能完成状态以当前实现进度和实际代码为准。**

## 当前实现进度

2026 年 10 月 29 日继续完成 ECS 组件增量：[Registry](../src/ECS/Registry.h) 提供 `add<T>`、可变／const `tryGet<T>` 和 `remove<T>`，内部按类型使用 [Sparse Set 组件池](../src/ECS/ComponentStorage.h)。每实体每类型至多一份，重复／无效操作被拒绝；实体销毁自动清理全部组件，槽位复用不继承旧数据。组件移动和析构要求不抛异常，引用在同类型结构性修改后需重新获取。Debug／Release 各 13 项[组件检查](../tests/ComponentChecks.cpp)、原有各 8 项 Entity 检查及主程序构建通过，无编译警告；未运行渲染窗口或 GPU 场景。多组件查询、层级、Transform 及 Mesh／Material 场景渲染仍待实现，F02 尚未完成。用户同时确认所有场景 Object 统一由 Entity 管理，以 Transform、Mesh、Material 等组件组合驱动后续 RenderScene 提取与渲染；完整接口见[组件存储契约](architecture-design.md#ecs-组件存储当前实现)。

2026 年 10 月 29 日完成 ECS 的首个纯 CPU 增量：[Entity](../src/ECS/Entity.h) 使用索引与 generation，[Registry](../src/ECS/Registry.h) 提供创建、销毁、存活检查和存活数量。槽位复用更换 generation，默认／越界／错误代际／已销毁句柄被拒绝；销毁不分配内存，代际达到上限后退役槽位以避免回绕。Debug／Release 构建与各 8 项 [Entity 检查](../tests/EntityChecks.cpp)通过，覆盖复制句柄失效、重复删除、4096 实体扩容、混合增删与同槽位 4096 次复用；主程序两种构建通过。句柄限所属 Registry 使用，跨 Registry 误用不检测；内存／索引耗尽和代际到顶分支未实测。该首个增量未运行 GPU 场景，也未包含组件、查询、层级、Transform 或 Sandbox 场景接入，未完成 F02；后续组件进度见上一段。命令见 [README](../README.md#entity-生命周期检查)，接口约定见[架构设计](architecture-design.md#ecs-实体生命周期当前实现)。

2026 年 10 月 26 日完成独立的 Ray Query 离线编译探针：锁定 Slang／SPIR-V 目标下的计算 Shader 通过验证，入口、capability、扩展和固定资源绑定已核对，运行时前置条件已记录。GPU 求交、加速结构构建及光追 feature 启用仍待实现；详细证据见下文路径追踪章节。

已完成 CMake 构建入口、GLFW 窗口、Vulkan Instance 与诊断回调、设备及 Surface 能力报告、设备选择、Device／单队列、Swapchain，以及一个在途帧的持续渲染、同步和窗口 resize／最小化恢复时的交换链重建。程序创建 1280 × 720 的 MadCat 窗口，通过动态渲染清除深蓝背景，再绑定最小图形管线并 Draw 彩色三角形；后端处理状态转换和 acquire／submit／present。重建与关闭前均等待 GPU 提交及呈现资源退役，先释放帧命令资源，再释放管线与交换链。零像素尺寸或最小化时等待事件，窗口恢复后重建并继续渲染，也可在最小化时关闭；不可恢复错误返回非零状态。Slang 手动编译、SPIR-V 验证、失败保留产物、启动时 Shader Module 加载和管线创建均已接通，运行期 Shader 更新尚未实现。

GLFW 3.4 由 CMake FetchContent 获取，固定到提交 `7b6aead9fb88b3623e3b3725ebb42670cbe4c579`。构建目标要求 C++23。运行时采用 Vulkan 1.3 实例级基线；CMake 查找 Vulkan 1.3.239 或更新头文件，以提供 EXT maintenance1 类型，通过 `Vulkan::Headers` 提供头文件，Vulkan 函数由 GLFW 动态获取。Debug 必须能找到 `VK_LAYER_KHRONOS_validation` 和 `VK_EXT_debug_utils`，Release 不主动启用验证层。清屏路径额外要求 EXT `swapchainMaintenance1` 和对应实例依赖；基础设备初始化仍保留原支持范围。

2026 年 10 月 13 日使用 CMake 4.4.4、MSVC 19.44.35228、Vulkan SDK 1.4.363.0 完成 Debug 和 Release 构建，未出现编译警告。运行时查询得到实例级 API 1.4.363。MadCat 已枚举本机物理设备，分别查询名称、类型、设备级 API、原始驱动版本和可用的驱动属性、队列族及主线／光追相关扩展。

| Instance 增量已有验证 | 实际结果 |
|---|---|
| Debug／Release 窗口运行 | 窗口标题为 MadCat，发送关闭事件后返回 0；Instance 正常销毁，Debug 没有验证 warning／error 或 GLFW 错误 |
| 缺少 Debug 验证层 | 通过空的 `VK_LAYER_PATH` 隔离验证层，程序报告缺少 `VK_LAYER_KHRONOS_validation` 并返回 1 |
| 无效驱动搜索路径 | 指定不存在的 `VK_DRIVER_FILES`，程序报告缺少窗口 Surface 扩展并返回 1 |
| 回调消息探针 | 通过 `vkSubmitDebugUtilsMessageEXT` 提交可控 warning 消息，确认回调输出消息标识和正文 |
| 初始化失败与异常清理探针 | 注入诊断回调创建失败、所需函数缺失和初始化后的异常，已创建的 Instance 均正常释放；模拟 Vulkan 1.2 时在创建前拒绝 |

设备报告分别查询 `dynamicRendering`、`synchronization2`、`bufferDeviceAddress`、`accelerationStructure` 和 `rayQuery`，依据设备核心版本或扩展组织 feature 查询，并输出加速结构限制。扩展列表与 feature 支持分开显示；缺少可选能力时继续报告设备，空设备列表、函数缺失和枚举失败返回非零状态。

| 2026-10-13 设备查询结果 | 设备级 API／驱动 | 队列族数量 | 上述五项 feature | 计算 Ray Query 前置条件 |
|---|---|---|---|---|
| NVIDIA GeForce RTX 5060 Ti，独立 GPU | 1.4.351／617.42 | 6 | 全部支持 | 查询可用 |
| AMD Radeon(TM) Graphics，集成 GPU | 1.3.280／24.10.24 | 5 | 全部支持 | 查询可用 |

RTX 5060 Ti 的加速结构几何／实例／primitive 上限分别为 16777215／16777215／536870911，scratch 对齐为 128 字节；AMD 集显对应为 16777216／16777216／536870912，scratch 对齐为 256 字节。这些是驱动报告的限制，不是实际场景容量或性能测量。

计算 Ray Query 前置检查包括 Compute 队列、三个光追相关 feature、`VK_KHR_acceleration_structure`、`VK_KHR_ray_query`、`VK_KHR_deferred_host_operations`，以及按设备核心版本计算的其他扩展依赖。报告只表明查询条件成立，不启用 feature，也不代表 Shader 编译或 GPU 求交已经验证。[加速结构扩展依赖](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_acceleration_structure.html)、[Ray Query 扩展依赖](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_query.html)

| 2026-10-13 设备报告验证 | 实际结果 |
|---|---|
| Debug／Release 构建与窗口运行 | 均构建成功且无编译警告，完整报告两张设备后发送关闭事件，返回 0；Instance 正常销毁，Debug 没有验证 warning／error 或 GLFW 错误 |
| 15 个本地查询探针 | 未初始化、空设备、函数缺失、设备／扩展枚举失败、`VK_INCOMPLETE` 重试、核心提升与扩展提供、feature 为假、光追扩展／地址 feature／依赖／Compute 队列缺失、旧设备与混合设备均符合预期 |

2026 年 10 月 15 日新增窗口 Surface，当时由 `VulkanInstance` 所有者持有，在诊断回调、Instance 和窗口之前销毁。报告使用实际 Surface 查询每个队列族的呈现支持；有呈现队列时才查询格式／颜色空间、present mode、图像数量、尺寸、数组层数及 usage／transform／composite alpha 标志。无呈现支持的设备保留其他能力报告，并说明跳过原因；查询 API 失败则报告操作、`VkResult` 和设备／队列上下文。[Surface 查询的有效使用要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceCapabilitiesKHR.html)

| 2026-10-15 实际 Surface 查询，1280 × 720 窗口 | 支持 Present 的队列族索引 | 兼具 Graphics／Compute／Present 的队列族 | 格式／模式数量 | 图像数量范围 |
|---|---|---|---|---|
| NVIDIA GeForce RTX 5060 Ti | 0、2 | 0 | 7／5 | 2～8 |
| AMD Radeon(TM) Graphics | 0、1、2、3、4 | 0 | 5／3 | 2～16 |

两张设备均报告当前尺寸 1280 × 720、最大数组层数 1、usage `0x9f`、支持及当前 transform `0x1`；composite alpha 分别为 `0x3` 和 `0x1`。两张设备均提供 `B8G8R8A8_SRGB`／`SRGB_NONLINEAR` 和 FIFO；这是本次窗口及驱动的查询结果，不能当作以后窗口尺寸或其他设备的固定配置。

| 2026-10-15 Surface 增量验证 | 实际结果 |
|---|---|
| Debug／Release 构建与实际窗口运行 | 均通过，无编译警告；两张设备报告完成后发送关闭事件，返回 0；Surface 在 Instance 前释放，Debug 无验证 warning／error 或 GLFW 错误 |
| 22 个 Surface 探针 | 未初始化、缺少 Surface、空窗口、重复创建、异常退出、创建失败、缺失销毁／查询函数、呈现／能力／列表查询失败、无呈现支持、`VK_INCOMPLETE` 重试、空列表和可变尺寸／无数量上限均符合预期；销毁顺序及次数检查通过 |
| 原有 15 个设备能力探针回归 | 全部通过；探针适配了 Surface 前置条件，并继续使用模拟物理设备查询 |

2026 年 10 月 16 日完成封装重构：`VulkanInstance` 只拥有 Instance 与诊断回调；`VulkanSurface` 独立拥有 Surface；`PhysicalDeviceInfo` 保存可复制的查询数据与借用句柄；`SurfaceSupport` 保存某次设备／Surface 组合查询的快照。设备查询不再要求 Surface，呈现查询检查 Instance 一致性；两种查询均成功后整体替换输出，失败时保留原结果。打印函数只读取数据。所有权、接口和组装入口见 [README](../README.md#vulkan-后端封装)。

| 本次封装重构验证 | 实际结果 |
|---|---|
| Debug／Release 构建与实际窗口运行 | 均通过，无编译警告；正常退出返回 0，Debug 无验证 warning／error 或 GLFW 错误；完整日志与封装前逐字一致 |
| 22 个 Surface 与 15 个设备能力探针回归 | 全部通过；探针已适配独立 Surface 与数据查询接口 |
| 新增 18 项封装检查 | Surface 创建前查询设备、数据复制与空 `pNext`、打印不查询 Vulkan、空 Surface／无效设备／跨 Instance 提前拒绝、查询失败保留旧结果及失败后恢复均通过 |

该次封装验证使用 Windows 11、CMake 4.4.4、MSVC 19.44.35228、Vulkan SDK 1.4.363.0；设备级 API 与驱动和上表一致，无 Shader 或资产输入。探针、窗口检查脚本和日志放在本地 `build/verification`，属于不纳入版本控制的验证产物。失败探针只替换测试进程中的查询／创建结果，没有修改系统驱动或验证层安装。构建和运行命令见 [README](../README.md)。

2026 年 10 月 17 日新增 `VulkanDevice`：从配对的 `PhysicalDeviceCandidate` 快照选择设备，仅启用 `VK_KHR_swapchain`、`dynamicRendering` 和 `synchronization2`，获取一个队列族中的队列 0。筛选要求设备级 Vulkan 1.3、上述扩展与 feature、可用的 Surface 格式和呈现模式、颜色附件 usage，以及同一队列族的 Graphics／Compute／Present 支持。合格设备按独显、集显、其他类型排序，同级按枚举顺序，队列族取第一个兼容项。本机已具备统一队列条件，当前采用单队列方案以简化后续同步；暂不支持分离队列，也不将光追能力作为本轮必需条件。具体接口、失败行为和生命周期见 [README](../README.md#vulkan-后端封装)。

| 2026-10-17 Device 增量验证 | 实际结果 |
|---|---|
| Debug／Release 构建与实际窗口运行 | 均通过，无编译警告；选中 RTX 5060 Ti，队列族 0／队列 0；关闭返回 0，Device 先于 Surface 和 Instance 释放，Debug 无验证 warning／error |
| 两张实际设备分别创建 Device | RTX 5060 Ti 与 AMD 集显均成功启用本轮能力并获取队列，Debug 验证无 warning／error；API 和驱动与上表一致 |
| 39 个模拟场景 | 覆盖 API／扩展／feature／Surface／单队列筛选、快照身份、设备优先级和同级顺序、失败清理、重复初始化、失败恢复、异常退出及设备等待失败，均通过 |

该次使用相同 Windows、CMake、MSVC 和 Vulkan SDK 环境，无 Shader 或资产输入。窗口请求 1280 × 720，该次运行 Surface 实际报告 1028 × 720，说明交换链尺寸必须以运行时查询为准；当时尚未创建 Swapchain 或提交 GPU 工作。

2026 年 10 月 22 日新增 `VulkanSwapchain`：Device 保留所选物理设备的数据副本，交换链初始化时使用该数据重新查询 Surface 支持，并确认所选队列仍可呈现。交换链优先使用 `B8G8R8A8_SRGB`／`SRGB_NONLINEAR`，否则保留第一个具体格式组合；采用 FIFO、单队列独占共享和颜色附件 usage。图像数量在 Surface 最小值上加一并受非零最大值限制；尺寸优先采用 Surface 固定值，否则将 framebuffer 像素尺寸约束到支持范围。使用当前 transform，composite alpha 按 Opaque、Pre-multiplied、Post-multiplied、Inherit 顺序选择受支持项。完整接口、错误行为和生命周期见 [README](../README.md#vulkan-后端封装)。[创建参数要求](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html)

| 2026-10-22 Swapchain 增量验证 | 实际结果 |
|---|---|
| Debug／Release 构建与实际窗口运行 | 均通过，无编译警告；1280 × 720、sRGB 格式、FIFO、3 张图像；关闭返回 0，Swapchain 先于 Device、Surface 和 Instance 释放，Debug 无验证 warning／error |
| 两张实际 GPU 分别创建 Swapchain | RTX 5060 Ti 与 AMD 集显均成功创建 320 × 240、sRGB／FIFO／3 张图像的交换链并正常释放，Debug 无验证 warning／error |
| 27 个配置与 15 个模拟生命周期场景 | 覆盖格式回退、FIFO、固定／可变像素尺寸、图像数量限制和溢出、transform／alpha、无效配置、创建前刷新、函数缺失、创建／枚举失败、空／不足／无效图像、枚举异常清理、重复初始化、失败恢复及 `VK_INCOMPLETE` 重试，均通过 |
| 原有 39 个 Device 模拟场景回归 | 全部通过；保留物理设备数据副本未改变筛选和失败清理行为 |

该次环境为 Windows 11、CMake 4.4.4、MSVC 19.44.35228、Vulkan SDK 1.4.363.0；GPU API／驱动与上表一致，无 Shader 或资产输入。验证脚本、探针与日志保存在本地 `build/verification`，不纳入 Git。Swapchain 图像由交换链管理，调用方只借用句柄；销毁前仍须完成所有图像使用并释放依赖对象，交换链析构不自动等待 GPU。[销毁要求](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroySwapchainKHR.html) 该交换链增量当时尚未 acquire、提交或呈现；后续清屏增量的证据见下文。

同日的下一增量新增 `VulkanClearRenderer`，在后端创建图像视图、一个命令池／命令缓冲、两个二进制信号量及 acquire／submit／present Fence，通过动态渲染的颜色附件 CLEAR／STORE 清屏，usage 仍为颜色附件，无 Shader 或管线。首次使用图像从 `UNDEFINED` 转换，后续从 `PRESENT_SRC_KHR` 转换到颜色附件，完成后转换回呈现状态；帧资源复用和退出均等待各项实际完成条件。

为严格确认呈现资源可释放，本轮启用可用的 `VK_EXT_surface_maintenance1` 及其 `VK_KHR_get_surface_capabilities2` 实例依赖，查询并按支持启用设备 EXT `swapchainMaintenance1`，通过呈现 Fence 退役资源。提交 Fence／单独的 Device idle 不能替代此条件。该选择服务当前正确性目标；基础设备排序未变，清屏器在缺少能力时明确拒绝初始化，暂不接入 KHR 别名或自动改选设备。[官方呈现同步说明](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)

| 2026-10-22 清屏增量验证 | 实际结果 |
|---|---|
| Debug／Release 构建与持续清屏 | 均通过，无编译警告；RTX 5060 Ti、1280 × 720、sRGB／FIFO／3 张图像，均持续呈现超过 120 帧后正常关闭 |
| 图像内容 | 截图中心 RGB `(56, 80, 111)`，符合线性清屏色 `(0.04, 0.08, 0.16)` 的 sRGB 编码 |
| Debug 验证层与同步验证 | 使用 `VK_LAYER_VALIDATE_SYNC=1`，正常运行、resize 停绘、最小化停绘与退出均无 warning／error；按清屏器 → Swapchain → Device → Surface → Instance 释放 |
| 56 个模拟场景 | 覆盖帧复用、图像状态、函数缺失、部分创建失败与恢复、获取超时、suboptimal／out-of-date、录制／提交／呈现失败及 Fence 等待失败后清理；全部通过 |
| 两张实际 GPU | RTX 5060 Ti 单独运行 30 帧并安全退役；AMD 集显仍可创建 Device／Swapchain，缺少 EXT maintenance1 时按预期拒绝清屏 |
| 原有探针回归 | Swapchain 的 42 个配置／生命周期场景、Device 的 39 个场景及设备能力模拟探针通过 |

该清屏增量当时仅在 resize／最小化／WSI 状态变化后停绘；后续重建增量见下文。acquire 使用 50 ms 有限超时；suboptimal acquisition 先完成提交与呈现再处理重建。不可恢复帧错误停止后续提交；退出时完成等待若再次失败则终止进程，避免释放无法确认完成的资源，设备丢失恢复未实现。当前证据不代表三角形、Shader、Render Graph 或完整第一阶段已完成。探针、脚本、截图和日志仍是 `build/verification` 中不纳入 Git 的本机产物。

同日的重建增量由 Sandbox 的 `SandboxApplication` 管理窗口事件和可替换的 `Presentation`（先声明 Swapchain，再声明清屏器）。尺寸变化或 out-of-date／suboptimal 请求重建时，先等待全部相关完成条件，再依次释放清屏器、交换链，重新查询 Surface 支持并创建新资源。采用同步等待、先销毁后创建以保持当前机制简单，不使用 `oldSwapchain` 复用；重建失败清理部分资源并安全退出。最小化／零尺寸时暂停获取图像和创建，使用事件等待恢复；Windows 刷新回调调用受重入保护、异常不跨 C 边界的同一渲染入口，支持原生拖动缩放期间更新。[GLFW 刷新回调](https://www.glfw.org/docs/3.4/window_guide.html#window_refresh)

| 2026-10-22 重建增量验证 | 实际结果 |
|---|---|
| Debug／Release 构建与窗口恢复 | 均通过；多次缩小／放大及两次最小化恢复后，Swapchain 尺寸与实际 framebuffer 一致，每次九点像素均为预期清屏色 |
| Windows 原生缩放模态循环 | 在系统模态循环未退出时已通过刷新回调完成新尺寸呈现，九点像素检查通过 |
| 连续变化与退出 | Debug 连续 24 次尺寸变化、4 次快速最小化恢复后最终画面正确；最小化期间关闭返回 0，各场景资源创建／销毁数量一致 |
| 验证层与同步验证 | RTX 5060 Ti；上述 Debug 场景开启 `VK_LAYER_VALIDATE_SYNC=1`，无 warning／error |
| 模拟与实际设备探针 | 原有 56 项清屏场景及新增 12 项应用重建／暂停／恢复／失败／重入／回调生命周期检查均通过；RTX 单独清屏和 AMD 缺少能力的拒绝路径通过 |

本次沿用上述 Windows、工具链、SDK 与 GPU 驱动，无 Shader／资产输入，无性能测量。重建会等待 GPU，未实现异步退役、创建失败后的运行期重试、设备丢失恢复或新增 feature 路径。验证产物在本地 `build/verification/resize-*` 和清屏探针日志中；历史清屏脚本的停绘预期以对应增量为准。

## 项目目标和开发约束

半年目标是完成一个可用于研究的最小渲染工作台，包括基础光栅渲染、PBR 和可运行的路径追踪 Pass。暂不设实体数量、帧率或显存占用指标，优先保证机制可理解、边界清晰、结果正确和扩展方便。

| 项目 | 用户已确认的需求 |
|---|---|
| 前期定位 | 渲染算法研究和图形效果实验优先 |
| 长期方向 | 同时考虑游戏与仿真用途 |
| 开发方式 | 独立开发，计划周期半年 |
| 语言和平台 | Modern C++23，先支持 Windows 11 |
| 主要测试设备 | NVIDIA GeForce RTX 5060 Ti；实际 Vulkan 能力以运行时查询为准 |
| 开发经验 | 图形算法工程师，熟悉 OpenGL，对 Vulkan 一般熟悉 |
| ECS | 自行实现，不采用现成开源 ECS；主要目的是理解机制和掌控架构 |
| 场景 Object | 所有场景对象统一以 Entity 管理，以类似 Unity 的组件组合方式使用；为实体添加 Transform、Mesh、Material 等组件并引用有效资产后，由 RenderScene 提取并渲染 |
| 场景能力 | 实体、Transform、相机、模型、灯光，支持父子层级和 Transform 继承 |
| 图形接口 | 使用 Vulkan，通过自行实现的 RHI 封装 |
| RHI 范围 | 只实现 Vulkan，当前不为其他图形 API 预留抽象 |
| Pass 编写 | C++ 控制管线、资源绑定和 Draw／Dispatch，尽可能少直接操作 Vulkan |
| Pass 连接 | 第一版使用 C++；后续支持配置文件和节点编辑器 |
| 图执行 | 根据 Pass 声明自动处理执行顺序、资源状态转换和同步 |
| Shader | 使用 Slang，由用户手动触发重新编译并更新结果 |
| C++ Pass 更新 | 可以通过重新构建程序更新 |
| 渲染顺序 | 模型与贴图、基础光照与阴影，然后 PBR，随后完成路径追踪 |
| 第三方依赖 | 接受第三方工具库，模型格式先支持 glTF |
| 第一版界面 | 渲染窗口、参数面板、Pass 开关、中间结果查看 |
| 物理和动画 | 半年内保留接入接口，不要求实现功能 |
| 迭代方式 | 小步慢走，每次代码改动适合单个 commit，并说明修改动机 |
| 具体研究效果 | 尚未确定；不将 SSAO、SSR 等额外效果列为半年必需项 |

Falcor 的模块化 Render Graph 和研究工作流是体验参考。MadCat 采用自己的 ECS、Vulkan 封装和阶段计划，不将完整复刻 Falcor 作为目标。Falcor 官方项目介绍列出了 Render Graph、模型加载、Shader 编译和路径追踪等能力，可作为工作流参考。[Falcor 官方项目](https://github.com/NVIDIAGameWorks/Falcor)

## 半年交付范围

| 编号 | 必需能力 | 建议验收方法 |
|---|---|---|
| F01 | 可运行的 Windows 11 C++23 程序 | 从干净构建启动渲染窗口，能够调整窗口尺寸并正常退出 |
| F02 | 自研 ECS 和场景层级 | 场景 Object 统一使用 Entity；创建、查询和删除实体及组件；父节点变化正确影响子节点；拒绝层级环 |
| F03 | glTF 模型和贴图 | 通过 Entity 的 Mesh、Material 等组件引用资产并显示约定模型，几何、贴图和节点变换正确 |
| F04 | 自定义 Pass 和 Render Graph | 新增一个 C++ Pass，声明资源并接入图；不修改图编译器或 Vulkan 后端即可执行 |
| F05 | 自动顺序和同步 | 多个 Pass 连续读写资源时结果正确；图错误提供明确诊断；同步验证无未解决错误 |
| F06 | 手动 Shader 更新和实时参数 | 修改 Slang 后手动编译、应用；编译失败保留旧版本；参数修改不要求重启程序 |
| F07 | 研究工具界面 | 能调整参数、切换适用的 Pass、查看阴影图和其他中间纹理 |
| F08 | 基础光照、阴影和 PBR | 先完成贴图与基础光照、阴影，再用材质测试场景验证 PBR 行为 |
| F09 | 可运行的路径追踪 Pass | 完成多次散射和逐帧累积；相机或场景改变后正确重置累积 |
| F10 | 物理和动画接入接口 | 存在可复用的场景系统接入点，并明确执行阶段、Transform 写入权和场景到 GPU 的同步方式 |

F09 的具体算法与实现路径仍需按架构文档中的建议验证，验收范围至少包括多次散射、材质参与、累积和变化后的重置。只演示光追阴影、反射或一次求交，不能替代路径追踪目标。

半年内不要求实现完整场景编辑器、节点编辑器、配置文件建图、脚本系统、C++ 动态插件、Shader 文件监视、多图形 API、完整物理或动画、多队列调度、复杂 ECS 并行执行、实时降噪、ReSTIR 或 DLSS。这些功能可以在核心工作流稳定后另行安排。

## 架构设计入口

模块职责、Pass 编写方式、资源与历史所有权、显式绑定和参数链路统一维护于[架构设计文档](architecture-design.md)。该文档作为首版实施基线，区分已确认约束、接口示例和实现参考建议；字段、容器等在实现时确定，当前多数目标接口尚未实现。本文继续维护功能范围、进度、计划和验收证据。

## Shader 工具链实现与验证

当前使用固定版本的 `slangc`，通过独立手动目标生成 SPIR-V；具体版本、产物和已验证行为如下。运行期更新与后续界面触发流程独立维护于[架构设计文档](architecture-design.md#shader-布局与运行期更新设计)。

当前已实现第一段离线编译流程：`MadCatShaders` 为独立手动目标，不随普通 C++ 构建执行。Slang 固定版本为 `2026.17-25-gc7954ebd1`，程序路径由 CMake 查找或 `MADCAT_SLANGC` 指定，完整版本不符则拒绝编译。`shaders/triangle.slang` 包含 `vertexMain`、`fragmentMain`，直接生成 SPIR-V 1.6、保留入口名、使用 `-O0` 并将警告视为错误；`spirv-val --target-env vulkan1.3` 验证每个模块。顶点采用 `SV_VulkanVertexID`，实测只需 `Shader` capability，两个阶段的颜色接口均在 location 0。

两个阶段读取同一份源文件快照，全部通过验证后才发布至 `build/shaders/triangle/`，包含两个 `.spv`、源码快照及记录工具／参数／SHA256 的 `manifest.txt`。编译或验证失败时上一组四个文件保持不变；进程锁串行化同目录发布，中断后可恢复备份。当前只是离线产物发布，不代表运行期管线更新或 Shader 热更新已实现；include／import 依赖记录和反射待后续增量。使用方式见 [README](../README.md#手动编译-shader)。

| 2026-10-22 Slang 增量验证 | 实际结果 |
|---|---|
| Shader 编译与验证 | Debug／Release 均编译两个阶段并通过 Vulkan 1.3 验证，产物 SHA256 一致，入口与 capability 已通过反汇编核对 |
| 12 项构建检查 | 初次／重复构建、片元入口缺失、语法错误、版本不符、编译器／验证器／源码缺失、验证器失败、失败恢复和发布中断恢复均通过；失败时上一组四个文件哈希不变 |
| C++ 构建独立性 | 普通 Debug／Release 构建通过；另在显式指定缺失 Shader 工具的独立配置下，MadCat 的 Debug 目标仍构建通过 |

环境为 Windows 11、CMake 4.4.4、MSVC 19.44.35228、Vulkan SDK 1.4.363.0；Slang 版本如上，验证器为 `SPIRV-Tools v2026.4 v2026.4.rc2-0-gef96ed76`。本轮没有创建 Vulkan Shader Module、管线或执行新 Shader；SPIR-V 验证通过不能替代后续 GPU 场景验证。检查代码在 `tests/ShaderBuildChecks.cmake`，构建日志在 `build/verification/shader-build`，不要求安装额外测试框架。

后续 Shader Module 增量已接通启动加载：Sandbox 从 CMake 配置记录的 `<构建目录>/shaders/triangle/` 绝对路径读取两个模块，独立于工作目录及 Swapchain 重建。`src/Shader/Spirv` 在对齐的字数组中检查头部、版本 1.0～1.6、字节序和指令边界；这不是完整 SPIR-V 语义、入口／阶段、绑定布局或 Device capability 验证，仍要求输入来自离线验证流程。`VulkanShaderModule` 先解析销毁函数再创建模块，失败不接管输出，重复初始化保留旧模块，析构释放模块并要求 Device 存活。相关主机操作须完成；管线不要求模块随 GPU 工作保活。[创建参数](https://docs.vulkan.org/refpages/latest/refpages/source/VkShaderModuleCreateInfo.html)、[销毁规则](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyShaderModule.html)

| 2026-10-22 Shader Module 增量验证 | 实际结果 |
|---|---|
| Debug／Release 构建与 35 项检查 | 均通过，无编译警告；CPU 及模拟 Vulkan 检查覆盖损坏文件、两种字节序、Unicode／空格路径、函数缺失、创建失败与重试、重复初始化和部分清理 |
| 实际窗口与生命周期 | 两种构建均从其他工作目录启动、创建两个模块；resize 与两次最小化恢复后模块数量不变，九点清屏颜色检查通过，正常关闭返回 0；另验证 Debug 最小化期间关闭 |
| 六项真实启动失败 | 隔离目录中分别移除顶点／片元文件、置空片元文件、损坏魔数、改变大小为非 4 倍数、截断指令；均在 Vulkan 创建前拒绝对应文件、返回 1，已创建模块先于 Device 释放 |
| Validation Layers 与同步验证 | RTX 5060 Ti、API 1.4.351、驱动 617.42；上述最终 Debug 场景启用同步验证，无 warning／error |

该 Shader Module 增量沿用上述 Slang／SPIRV-Tools 版本和 `triangle.slang`，当时没有执行 Shader 或测量性能。测试源码为 `tests/ShaderModuleChecks.cpp`，通过 `MadCatShaderModuleChecks` 手动目标运行，结果在 `build/verification/shader-module/<配置>/`；该次窗口和故障脚本、日志在本机 `build/verification/`，不随 Git 分发。只在启动时加载，重新编译前先关闭程序，成功后重启；尚无并发发布／读取协议，不验证 manifest 哈希，也不支持安装包资源定位。后续图形管线与 Draw 的完成证据见下文。

### 最小三角形管线与 Draw

2026 年 10 月 22～23 日接入 `VulkanTrianglePipeline`：空 Pipeline Layout、固定 `vertexMain`／Vertex 与 `fragmentMain`／Fragment、无顶点缓冲的 Triangle List、一个颜色附件、单采样、无剔除／深度／混合。通过 `VkPipelineRenderingCreateInfo` 指定实际 Swapchain 格式，每帧绑定管线、设置当前像素尺寸的 viewport／scissor，再记录 `Draw(3, 1, 0, 0)`。Shader 的 location 0 颜色接口已通过实际管线与像素验证，但没有实现通用反射或接口兼容性检查。[动态渲染格式声明](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineRenderingCreateInfo.html)

本次采用每次交换链重建时重新创建管线，以简单、明确的方式处理格式变化；动态 viewport／scissor 使用新尺寸，不维护管线缓存或同格式复用优化。`Presentation` 按 Swapchain、管线、帧渲染器声明成员，反向释放时先等待 GPU／WSI 并释放命令，再释放管线和交换链。Shader Module 在应用外保留，用于后续重建。当前仍为后端验证路径，未扩展为通用 Pass、材质或 Render Graph 接口。[管线销毁前置条件](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyPipeline.html)

| 2026-10-22～23 三角形增量验证 | 实际结果 |
|---|---|
| Debug／Release 构建 | 均通过，无编译警告；每种配置的 25 项管线模拟检查及原有 35 项 Shader Module 检查通过 |
| 实际三角形与尺寸恢复 | 两种配置均从其他工作目录启动，检查初始画面、三次 resize 和两次最小化恢复；每幅图采样六个背景点、四个按重心坐标计算的插值颜色点，sRGB RGB 容差为 3，全部通过 |
| Debug 原生缩放循环 | 在系统原生缩放循环仍活动时将 framebuffer 更新到 934 × 611，三角形和背景像素通过；该检查使用窗口消息与程序化尺寸变化，未进行人工鼠标验收 |
| Debug 连续变化与退出 | 24 次尺寸变化、四次快速最小化／恢复后画面正确；另验证最小化关闭。各场景正常返回 0，管线创建／销毁数量一致且先于 Swapchain 释放 |
| Validation Layers 与同步验证 | 所有最终 Debug 场景启用 `VK_LAYER_VALIDATE_SYNC=1`，无 warning／error；实际设备 RTX 5060 Ti，API 1.4.351，驱动 617.42 |

环境与 Shader 版本沿用上述记录，未修改 Shader，未测量性能。`MadCatTriangleChecks` 复用模拟 Vulkan 程序，覆盖函数缺失、Layout／Pipeline 创建失败与重试、重复初始化、格式、动态尺寸及 Draw 参数；真实驱动上的资源耗尽未注入。可分发的窗口检查在 `tests/TriangleWindowChecks.ps1`，运行说明见 [README](../README.md#三角形绘制与检查)，截图和日志位于 `<构建目录>/verification/triangle/`。当前只显示一个裁剪空间三角形，尚无通用资源绑定、场景、相机或 Render Graph；该次尚未完成 Ray Query 编译与 GPU 求交探针，后续编译探针证据见路径追踪章节。

Shader 布局和运行期更新的目标设计见[架构设计文档](architecture-design.md#shader-布局与运行期更新设计)；当前运行程序仍只在启动时加载 Shader，尚未验证通用反射或运行期替换。

## 模型和渲染功能基线

### glTF 导入范围

建议先支持 glTF 2.0 的静态三角网格、索引、位置、法线、UV、节点层级、基础材质、PNG／JPEG 贴图和采样器。`.gltf` 与 `.glb` 的具体覆盖范围随导入阶段逐项验证，并记录支持清单；法线缺失、稀疏 accessor、压缩和扩展不默认视为已支持。

资产转换和坐标约定的实现参考建议见[架构设计文档](architecture-design.md#资产转换与数学约定)。

### 光栅渲染

建议初期采用前向渲染，依次完成：

1. 模型几何、相机、深度测试、基础贴图和色彩空间处理。
2. 最小基础光照，例如方向光与简单漫反射。
3. 单个方向光阴影图，以及用于稳定结果的基础偏移和滤波。
4. HDR 颜色输出与显示转换，使后续 Pass 能操作线性数据。
5. Metallic-Roughness PBR，逐项接入基础色、金属度、粗糙度、法线与其他材质数据。

PBR 的第一份验收场景应能比较不同粗糙度、金属度和基础色的材质球。IBL、更多灯型、级联阴影、透明材质和延迟渲染作为独立增量评估，不绑定到第一份 PBR 演示。

### 路径追踪

路径追踪执行路径的现有建议见[架构设计文档](architecture-design.md#路径追踪执行路径)；下文记录早期能力验证及功能验收范围。

建议在首月查询并记录 `accelerationStructure`、`rayQuery`、`bufferDeviceAddress` 及其扩展依赖与限制，并编译一个最小求交 Shader。此时不构建完整路径追踪器。若 Ray Query 路径不可用，应先诊断环境，必要时比较专用光追管线方案，而不是拖到第五个月才验证基础能力。

2026 年 10 月 26 日已完成上述编译探针。独立手动目标 `MadCatRayQueryProbe` 编译 `shaders/ray-query.slang`，使用现有锁定版本 `2026.17-25-gc7954ebd1`、SPIR-V 1.6 和显式 `-capability spvRayQueryKHR`；省略此 capability 会触发隐式 profile 升级诊断，并被 warnings-as-errors 拒绝。通过 `spirv-val --target-env vulkan1.3` 后，用 `spirv-dis` 检查计算入口、工作组、query 指令和固定资源绑定，成功后发布源码快照、二进制、反汇编和 manifest。复用原有候选／备份发布流程，不新增运行时依赖；三角形和探针产物分别保存。[命令、接口和设备启用清单](../README.md#ray-query-编译探针)

| 2026-10-26 编译探针验证 | 实际结果 |
|---|---|
| Debug／Release 编译与验证 | 两种配置均通过，`compute.spv` SHA256 为 `e7587422be40149889df2986ed2221c23e7c86fef2d211a9d45a334e8114aad8` |
| SPIR-V 检查 | `computeMain`／GLCompute、工作组 1 × 1 × 1、Logical／GLSL450；只需 `Shader`／`RayQueryKHR` capability，声明 `SPV_KHR_ray_query`／`SPV_KHR_storage_buffer_storage_class`；后者由核心基线覆盖 |
| 资源接口 | set 0 binding 0 为 `scene` 加速结构，binding 1 为 `results` Storage Buffer；写入一个 32 位命中标记 |
| 失败与恢复 | 每种配置各 18 项检查通过，包括版本／工具错误、编译／验证／反汇编失败、查询移除、binding／工作组变化、capability 省略和发布中断恢复；失败时上一组四个文件哈希不变 |
| 三角形构建回归 | 原有 12 项 Shader 检查通过，重新编译后的两个三角形 SPIR-V 哈希与已有记录一致 |
| 设备查询 | 本次 `vulkaninfo` 重新报告 RTX 5060 Ti（API 1.4.351、驱动 617.42）和 AMD 集显（API 1.3.280、驱动 24.10.24）均有 Compute 队列、三个所需扩展及三个所需 feature 支持；仅表示查询可用 |

工具为 CMake 4.4.4、Vulkan SDK 1.4.363.0 和 SPIRV-Tools `v2026.4 v2026.4.rc2-0-gef96ed76`。在 Vulkan 1.3 基线下，未来 Device 仍须显式启用 `VK_KHR_acceleration_structure`、`VK_KHR_ray_query`、`VK_KHR_deferred_host_operations` 以及 `bufferDeviceAddress`、`accelerationStructure`、`rayQuery`；当前设备创建链未启用这些项目。固定 Descriptor 不要求额外启用 Descriptor 数组或 update-after-bind。尚未执行新 Shader、GPU 求交、同步验证或性能测量，不能视为完整第一阶段或路径追踪完成。失败检查日志在本地 `<构建目录>/verification/ray-query-build/`，本次设备查询报告在 `build/verification/ray-query-device-report.txt`。

路径追踪按以下增量实现：

- 为共享几何建立 BLAS，为世界变换后的实例建立 TLAS；初期正确重建即可，更新与压缩优化后置。
- 完成主射线和求交显示，再接入法线、UV 和材质读取。
- 实现漫反射基线、多次散射、随机采样、路径吞吐量和最大反弹次数。
- 以发光几何或其他明确的光源模型形成可验证的间接照明；灯光采样等改进单独加入。
- 实现浮点逐帧累积、样本计数、显示转换，以及采样数和反弹次数的界面参数。
- 后续逐步接入与 PBR 一致的 BSDF；不把降噪、重要性采样或实时帧率作为首个路径追踪版本的要求。

最小验收使用静态、不透明三角网格场景，包含一次间接照明可见的布置。固定相机时，增加样本后噪声趋势应下降；检查有限数值，避免 NaN、明显漏光和变换错误。复杂材质、透明、介质和运动模糊不在首个验收范围。

相机、视口尺寸、几何、Transform、材质、光源、影响结果的 Shader 或采样参数变化时，必须正确判断是否重置历史。Tone Mapping 等纯显示参数可以不清空线性累积。对累积存储的读写和 BLAS／TLAS 构建到求交的依赖，须纳入图同步计划。

## 物理和动画的后续接入范围

半年范围为可复用的场景系统接入点及明确的写入阶段，暂不实现求解、碰撞或骨骼动画。具体接入设计与实现参考建议见[架构设计文档](architecture-design.md#物理和动画的后续接入)，实际能力仍待实现验证。

## 工具和第三方依赖建议

| 用途 | 建议候选和官方参考 | 采用理由与边界 |
|---|---|---|
| 构建 | CMake 已使用；[Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html) 后续按需加入 | 当前使用 Visual Studio 2022 x64 生成器，命令见 README |
| Windows C++ 工具链 | 支持项目实际使用的 C++23 特性的 MSVC 工具链 | 建议先采用 Windows 常用工具链；不假定所有 C++23 库能力都已具备 |
| 依赖管理 | [vcpkg Manifest 模式](https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode) | 建议锁定依赖基线；Slang SDK 的分发方式可独立安排 |
| 窗口和输入 | GLFW 3.4 已使用；[Vulkan 支持](https://www.glfw.org/docs/3.4/vulkan_guide.html) 已用于 Instance 与 Surface 初始化 | 当前实现窗口创建、事件等待、关闭、Vulkan 函数获取和窗口 Surface；其他输入能力后续增加 |
| Vulkan 开发环境 | [LunarG Vulkan SDK](https://vulkan.lunarg.com/) 已使用 | 当前要求头文件至少 1.3.239；本机验证 SDK 1.4.363.0，运行时版本和验证层另行查询 |
| 调试界面 | [Dear ImGui](https://github.com/ocornut/imgui) | 提供参数面板及官方 Vulkan 适配，当前不建设完整编辑器 |
| 数学 | [GLM](https://github.com/g-truc/glm) | 向量、矩阵与四元数；统一并验证坐标、深度和布局约定 |
| glTF 解析 | [cgltf](https://github.com/jkuhlmann/cgltf) | 小型解析器候选；引擎仍负责资产转换、图片读取与解码 |
| GPU 内存分配 | [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | 在 Vulkan 后端使用，资源所有权与 GPU 使用寿命仍由引擎管理 |
| Shader | Slang | 已接入手动编译；固定 `2026.17-25-gc7954ebd1`，产物在 `build/shaders/triangle/`，运行期更新待实现 |
| 图片解码与测试 | 待实现该功能时选择 | 必须补齐纹理解码；测试框架保持简单，不提前扩大依赖 |

当前已采用 CMake、固定提交的 GLFW 3.4 和本机 Vulkan SDK。GLFW 通过 FetchContent 管理，Vulkan 开发环境通过 `find_package(Vulkan 1.3.239 REQUIRED)` 查找。其余工具库组合仍属于建议，具体版本和包管理方式按后续实际需求确定。cgltf 不默认把外部图片解码为像素，需要额外的文件处理和图片解码步骤。[cgltf 使用说明](https://github.com/jkuhlmann/cgltf)

源码目录组织建议见[架构设计文档](architecture-design.md#源码组织)，实际目录与支持范围以当前源码为准。

## 半年阶段计划

以下以项目开始后的月份安排，不代表固定工时承诺。每周可投入时间、实际开始日和显存规格尚未确认；每个阶段完成后依据证据调整后续工作量。阶段中的任务仍应拆为单个 commit 规模的增量。

| 阶段 | 目标与主要依赖 | 建议完成证据 |
|---|---|---|
| 第一个月 | 构建与窗口、Vulkan 基础、Slang 编译、最小 RHI；同时完成早期光追能力探针 | 窗口与三角形可运行，调整尺寸正常，Slang 到 SPIR-V 路径成功，所需能力清单与探针结论已记录 |
| 第二个月 | 自研 ECS、父子 Transform、相机、glTF 几何与贴图；依赖基础 RHI | 层级测试通过，约定模型可显示，相机可控制，CPU 资产与 GPU 数据边界明确 |
| 第三个月 | 最小 Pass 模型、C++ 建图、自动资源顺序与同步；把基础光照和阴影接入图 | Shadow → Lighting → Display 流程正确，图错误可诊断，阴影结果可查看 |
| 第四个月 | PBR、调参面板、Pass 开关、中间纹理查看、手动 Shader 更新 | 材质球场景正确，新增简单后处理不修改图核心，Shader 编译失败可恢复 |
| 第五个月 | 加速结构接入、计算 Ray Query Pass、材质读取和多次散射；依赖早期能力验证与稳定图 | 能对 glTF 场景求交并得到路径追踪图像，几何实例与世界变换正确 |
| 第六个月 | 累积与变化重置、路径追踪工具参数、整合验收、使用文档和缓冲 | 路径追踪样本累积正确，全部半年验收场景可重复运行，物理与动画接入接口及边界有明确说明 |

Shader 编译与资源状态模型先做最小验证，再逐步完善；不等待完整 RHI 或完整 ECS 才显示第一幅画面。图自动同步完成之前，可以在后端基础验证代码中显式处理必要同步；图接入后，应按已定义职责统一，避免手工与自动同步长期混用。

若进度不足，优先延后额外材质、IBL、更多灯型、阴影质量优化和工具美化。路径追踪、基础模型工作流、自研 ECS、层级、自动图同步和手动 Shader 更新属于已确认目标；需要改变这些范围时，应明确与用户重新确定取舍。

## 小步迭代和修改说明

**一个增量只解决一个主要问题，并保持项目在该阶段可构建或可验证。** 单个 commit 的大小按可解释、可审阅和可独立验证判断，不以固定代码行数硬性衡量。生成文件、第三方代码和资产应与手写实现区分说明。

每次修改前，先说明当前问题、为何此时需要修改、预期行为和改动边界；修改后说明具体改动、运行的验证、结果与限制。若未运行构建、未在 GPU 上验证或某功能仍是占位，必须如实标明。涉及多个独立机制的任务应继续拆分，不一次实现一整个月的功能。

建议每次交付使用以下简短格式：

> 修改动机：当前缺少什么能力，或存在什么问题，本次改动为何必要。  
> 本次改动：实现一个明确能力，列出涉及的主要模块。  
> 验证结果：执行了哪些构建、测试或人工场景，是否通过。  
> 已知限制：尚未支持的范围和下一步依赖。

例如，“将世界矩阵作为派生组件统一更新，使阴影和主渲染使用一致的父子变换”，是可审阅的修改动机；“优化架构”不足以说明具体问题。

建议第一批实现增量按以下顺序拆分，每项仍可根据实际改动量继续细分：

1. 添加最小 C++23 构建目标与运行入口，验证本机工具链。
2. 添加窗口与事件循环，验证创建、关闭和尺寸事件。
3. 添加 Vulkan Instance 与诊断回调，验证正常初始化和清理。
4. 枚举设备及 feature，输出与主线和光追目标相关的能力清单。
5. 创建设备、队列、Surface 和 Swapchain；必要时进一步拆为多个增量。
6. 提交清屏帧，补齐帧同步与呈现状态。
7. 单独处理 resize、最小化和 Swapchain 重建。
8. 用 Slang 生成并加载最小 Shader 产物。
9. 添加最小管线与 Draw 封装，显示三角形。
10. 添加 Ray Query 编译探针；GPU 求交探针按所需资源支持另行拆分。
11. 添加 Entity 生命周期与失效句柄检查。
12. 添加一种组件存储，再单独增加多组件查询。
13. 添加层级和世界 Transform 更新，再单独覆盖重新挂接与删除行为。
14. 按几何、索引、法线、UV、贴图分别增加模型导入能力。

当前已完成构建、窗口、Vulkan 初始化与能力报告、Device／单队列、单帧同步、交换链与管线重建、Slang 到 SPIR-V 的手动编译与验证、启动时的 Shader Module 加载、最小图形管线和三角形 Draw、Ray Query 离线编译探针，以及 CPU Entity 生命周期、按类型组件增删获取和实体销毁清理。架构设计已收敛为首版实施基线，常规接口和内部存储在实现时确定，停止逐项扩展架构讨论；这不表示目标能力已经实现。ECS／场景与模型主线的下一候选增量是多组件查询，明确遍历期间的结构性修改边界，再推进场景层级和 Transform；后续所有场景对象使用 Entity，渲染通过 Mesh、Material 等组件组合接入。Ray Query GPU 求交按缓冲、设备能力启用、绑定、加速结构和计算提交依赖另行拆分。运行期 Shader 更新仍需安全替换和失败恢复验证。

## 验证策略与风险

正确性优先于性能优化。对 ECS 生命周期、层级、图依赖、资源状态和历史重置等核心机制采用针对性测试；对界面和低风险可逆改动，使用适当构建或人工场景，不为每个小改动机械添加测试。

| 验证对象 | 最小验证内容 |
|---|---|
| ECS | 旧句柄失效、组件添加删除、多组件查询、延迟结构性修改和引用失效约定 |
| 层级 | 多层父子继承、重新挂接、循环拒绝、父节点删除、旋转与非均匀缩放 |
| 图编译 | 线性依赖、分叉合并、循环、缺失输入、不明确多写者、无效尺寸或格式 |
| 同步 | 写后读、读后写、写后写、Compute 到 Graphics、历史读写、上传与加速结构依赖 |
| Vulkan 运行 | 开启 Validation Layers 与同步验证，检查 resize、最小化、资源销毁和正常退出 |
| 资源与 Shader | 参数布局、纹理色彩空间、编译失败保留旧版本、绑定接口改变时正确重建 |
| 光栅场景 | 贴图模型、父子变换、方向光阴影、不同 PBR 参数材质球 |
| 路径追踪 | 间接照明、多次散射、累积重置、有限数值、样本增加后的噪声趋势 |

图编译和 ECS 测试应尽量不依赖 GPU。GPU 测试需要记录模型、相机、参数、设备、驱动与 Shader 版本。路径追踪使用固定随机种子辅助诊断，并采用数值或图像容差；不把不同驱动下逐像素一致视为必需。首次验收不设帧率门槛。

| 风险 | 应对和需要保留的证据 |
|---|---|
| Vulkan 同步复杂，隐藏访问破坏图推导 | 明确资源声明与所有权，采用单队列起步，以同步验证和小场景覆盖关键依赖 |
| RHI 设计过大拖慢算法开发 | 围绕实际 Pass 增加接口，当前不建设多后端抽象 |
| ECS 先追求高性能扩大工作量 | 先验证生命周期、查询和层级，性能测量出现需要后再优化存储 |
| Slang、SPIR-V 与设备能力不匹配 | 首月能力探针，锁定版本并记录编译目标与限制 |
| 路径追踪在后期才启动导致延期 | 早期验证技术链路，第五个月前备齐场景数据、资源绑定与计算 Pass |
| 模型格式范围膨胀 | 用约定资产驱动导入，记录支持子集和明确错误，不默认支持所有 glTF 扩展 |
| 历史资源、Shader 更新造成失效或竞态 | 版本变化触发重置，安全边界替换，GPU 完成后回收资源 |
| 独立开发投入不足 | 每月根据实际完成证据调整额外功能，核心目标变化由用户决定 |

## 后续需要确定的事项

以下信息按对应实现增量收集、选择和验证，已有工具与设备证据继续沿用。常规实现选择依据架构基线自主落实，不作为逐项用户确认清单；项目目标、已确认职责或关键行为变化再提请用户决定：

- 每周可投入时间、正式开始日期和实际显存容量，用于调整计划与测试资产规模。
- 本机编译器、Vulkan SDK、驱动、Slang 和第三方库版本，以及实际 C++23 能力。
- 测试模型与材质场景、glTF 子集、图片解码方案及资产授权来源。
- Sparse Set、坐标约定、删除策略和其他建议方案在实现中的验证结论。
- 路径追踪的光源模型、首份材质范围、采样方式和 Ray Query 路径的探针结果。
- 后续研究效果、配置文件格式、节点编辑器交互，以及物理和动画的具体范围。

文档与代码保持同步。每个阶段完成时更新已支持能力和验证证据；建议方案一旦改变，应记录修改原因、替代方案与对后续阶段的影响，而非将旧建议继续当作已实现约束。
