# 几何验证资产

外部场景入口：[Cornell Box](CornellBox/README.md) 提供红绿墙、共享几何的两个盒子和顶灯，使用 `CustomPass --cornell-box` 运行。来源、MIT 许可、固定版本、SHA256 与当前光照边界见其说明；当前支持直接光和基础色查看，尚无面积光／间接反弹。

[TransformProbe.gltf](TransformProbe.gltf) 是本仓库原创的静态几何验证数据，没有第三方模型或纹理。它包含两个相交三角形组成的 Mesh、两个共享该 Mesh 的节点实例，以及一个仅属于未选中场景的远处节点。数据内嵌为 base64，无外部文件。

默认场景的父节点平移为 `(0.2, 0.1, 0)`；第一个子节点平移 `(0.3, 0, 0.2)` 并绕 Y 旋转 90°，第二个子节点平移 `(-0.4, 0, -0.2)`、缩放 `(0.6, 0.8, 0.7)`。未选中节点在 `(50, 50, 50)`，不应影响预览包围盒。默认场景应产生两个节点实例、四个 Primitive、12 个顶点与 12 个索引。

| Primitive | Mesh 局部位置 | u16 索引 |
|---|---|---|
| 0 | `(-.7,-.5,0)`、`(.7,-.5,0)`、`(0,.7,.3)` | `2,0,1` |
| 1 | `(-.4,-.2,-.5)`、`(.5,-.2,-.5)`、`(.1,.6,.5)` | `1,2,0` |

独立解析参考得到激活几何的 AABB 最小值 `(-.62,-.4,-.55)`、最大值 `(1,.8,.9)`；预览中心为 `(.19,.2,.175)`，等比缩放为 `20/27`。CPU 与 GPU 像素检查分别使用这些参考值，不能从程序日志、上传数据或矩阵反推预期结果。

当前运行方式见 [RenderScene 与共享几何](../../README.md#renderscene-与共享几何)。两个模型实例共 8 次绘制，两个源节点和两个模型共享同一份 6 顶点／6 索引几何；每帧最多 38 个解析像素验证独立变换、删除／恢复与深度顺序。验证不包含材质、法线、UV 或贴图，显示颜色是由包围盒坐标生成的诊断颜色。

## 法线与 UV 参考资产

[AttributeProbe.gltf](AttributeProbe.gltf) 是本仓库原创的静态属性检查资产，无第三方模型或纹理。一个 Mesh、两个无索引 Primitive，数据内嵌为 base64；首个 Primitive 有法线与 UV，第二个缺失两者。两个模型实例共享 6 顶点／6 索引，共 4 次 DrawIndexed。

| Primitive | 局部位置 | 法线 | UV0 |
|---|---|---|---|
| 0 | `(-1,-1,0)`、`(0,-1,.5)`、`(-.5,1,0)` | `(.6,.8,0)`、`(0,.6,.8)`、`(.8,0,.6)` | `(0,0)`、`(1.25,-.25)`、`(.25,1)` |
| 1 | `(0,-1,0)`、`(1,-1,.5)`、`(.5,1,0)` | 缺失 | 缺失 |

AABB 为 `[-1,-1,0]..[1,1,.5]`，居中点 `(0,0,.25)`，预览适配缩放 `.6`。参考脚本直接使用这些解析常量，独立计算透视插值、旋转／逆缩放与显示颜色；第二个 Primitive 在 N／U 模式显示洋红色。法线有意变化，用于验证顶点到片元的传输，不作为真实曲面或材质模型。

运行 `tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario attributes` 或 Release 配置。两者每帧 22 个参考像素均通过，覆盖默认／法线／UV、旋转、非均匀缩放、相机、反序、resize 与最小化恢复；Debug 同步验证无 warning／error。按键和完整命令见 [README](../../README.md#法线与-uv-可视化)。

## 材质与采样参考资产

[MaterialProbe.gltf](MaterialProbe.gltf) 是原创内嵌资产，无第三方图片。一个 Mesh、四个无索引 Primitive，双实例共享 12 顶点／12 索引、4 材质、3 纹理；两张 PNG 实际参与采样，第三张 JPEG 只用于 CPU 解码检查。2×2 PNG 的四个顶行优先 RGBA 为 `(64,128,192,255)`、`(192,64,128,0)`、`(128,192,64,255)`、`(32,96,160,255)`；8×8 PNG 全部为 `(64,128,192,128)`；2×2 JPEG 由原创纯色 `(128,64,32)` 编码，CPU 检查允许有损误差。

| 材质 | 行为 |
|---|---|
| nearest-mask | 第一张图、nearest、U repeat／V clamp，因子 `(.5,.75,1,1)`、MASK cutoff .5 |
| linear-opaque | 共享第一张图、linear、U clamp／V mirrored repeat，因子 `(1,.5,.25,.8)`、OPAQUE |
| trilinear-minified | 8×8 固定颜色图、trilinear mip、repeat，UV 跨 256 次重复以读取缩小 mip；因子 `(.1,.3,.7,1)` |
| factor-no-uv | 无纹理／无 UV，因子 `(.2,.4,.6,1)`、doubleSided |

[独立像素参考](../../tests/MaterialPixelReference.ps1)直接使用解析坐标／UV／纹理字节与因子常量，计算透视插值、sRGB 解码、过滤、寻址、裁剪和深度，不读取应用上传数据或矩阵。stb_image 替换后，Debug 的默认／镜像／反序／resize／最小化恢复五次采样各 54 点通过，含四类材质与透明裁剪；同时检查只上传 3 张 GPU 图像（白色回退及两张实际图片）并创建 4 份绑定，退出安全释放，同步验证无 warning／error。命令见 [材质工作流](../../README.md#基础材质图片与纹理采样)。这不是光照／PBR 测试。

## 方向光参考资产

[LightingProbe.gltf](LightingProbe.gltf) 复用本仓库原创 MaterialProbe 的位置、UV、材质和内嵌图片；增加用于 Lambert 验证的法线与反向索引，不含第三方资产。Primitive 0 的三个法线为 `(.6,0,.8)`、`(0,.8,.6)`、`(-.8,0,.6)`；Primitive 1 索引 `0,2,1`，法线均 `(0,0,-1)`，材质改为 doubleSided，用于验证背面翻转；Primitive 2 缺法线，L 模式应为洋红色；Primitive 3 法线均 `(0,0,1)`。沿用 MaterialProbe 的 AABB `[-1,-1,0]..[1,1,.6]` 与适配缩放 `.6`。

[光照解析参考](../../tests/LightingPixelReference.ps1)复用材质采样和投影插值辅助函数，以独立逆缩放、旋转、法线归一化、背面修正及 Lambert 公式计算预期值。灯光颜色 `(1,.8,.6)`，辐照度缩放 π，父 Y 旋转 −35°／55°、子 X 旋转 −25°；关闭时缩放为零。Debug／Release 八次采样每次 54 点通过；同时检查帧 UBO 创建／释放数量、释放顺序和灯光切换不重建管线。Debug 开启同步验证，无 warning／error；Release 未主动启用验证层。复现命令见 [基础方向光](../../README.md#基础方向光)。

## 投射阴影参考资产

[ShadowProbe.gltf](ShadowProbe.gltf) 是本仓库原创内嵌参考场景，复用 MaterialProbe 的原创 2×2 RGBA PNG，无第三方资产。一个 Mesh、两个 indexed quad：接收平面 `[-1,-1,0]..[1,1,0]`、基础色 `(.7,.7,.7,1)`；遮挡板 `[-.35,-.35,.5]..[.35,.35,.5]`、UV `[0,1]`、nearest／Clamp、MASK cutoff .5、doubleSided。两者法线均为 +Z，每个 quad 使用 `0,1,2,2,3,0` 索引。启动居中点 `(0,0,.25)`、适配缩放 .6，两个实例共享 8 顶点／12 索引和两个材质绑定。

[独立投射阴影参考](../../tests/ShadowLightingReference.ps1) 使用上述解析常量与固定灯光／相机参数计算遮挡图 texel 深度、MASK 孔洞、接收点深度比较、偏移和九点 PCF，不读取程序矩阵／UBO 或 GPU 上传数据。参考包含稳定内点和滤波边缘点，检查亮区、全阴影及部分可见度。运行 `tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario shadow-lighting`，Release 替换配置；各 15 组场景通过，完整记录与限制见[方向光投射阴影](../../README.md#方向光投射阴影)。这不是 PBR／路径追踪验证。

## 外部开源模型

[Lantern](Lantern/README.md)（CC0-1.0）和 [Box](Box/README.md)（CC-BY-4.0）从 Khronos 官方固定提交原样下载，保留 glTF、外部缓冲、完整原始贴图、上游说明和许可。各目录的 `source.json` 提供来源 URL、字节数和 SHA256。它们用于 CustomPass 的静态场景、基础色／Lambert 可见性及运行期 Forward 图切换验证；运行时可加 `--fit` 居中缩放，原始文件不变。外部场景是可见性／生命周期检查，不替代解析平面的独立像素参考，也不代表 PBR 或发光材质验收。

[Suzanne](Suzanne/README.md) 是从 Khronos 官方 glTF Sample Assets 固定提交原样下载的经典猴头模型，采用 CC0 许可。目录保留 `.gltf`、外部 `.bin`、两张贴图及许可；没有格式转换。已通过 Debug／Release 导入和静态绘制、模型／相机变换、resize 与最小化恢复检查。来源、文件哈希、命令和验证边界见模型说明。
