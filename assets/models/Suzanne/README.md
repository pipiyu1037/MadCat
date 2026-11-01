# Suzanne 开源验证模型

Blender 的经典 Suzanne 猴头模型，直接取自 [Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/4995a638c96dbc94376d3d6164e70fee3ac7f7a2/Models/Suzanne)。用于验证真实外部 glTF 文件、外部缓冲、索引上传和静态几何预览。

## 来源与许可

- 上游固定提交：`4995a638c96dbc94376d3d6164e70fee3ac7f7a2`。
- 原始文件目录：[Models/Suzanne/glTF](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/4995a638c96dbc94376d3d6164e70fee3ac7f7a2/Models/Suzanne/glTF)。下列四个资源文件原样下载，未转换、重导出、简化或修改节点。
- 上游署名：© 2017 UX3D；Norbert Nopper。
- 上游模型许可：[CC0 1.0 Universal](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/4995a638c96dbc94376d3d6164e70fee3ac7f7a2/Models/Suzanne/README.md#legal)。[LICENSE.txt](LICENSE.txt) 原样复制自该提交的 `LICENSES/CC0-1.0.txt`。

`.gltf`、`.bin` 与两张 PNG 必须放在同一目录，保留相对文件名。当前渲染只使用几何；仍保留图片，使上游资源包完整，供后续材质工作使用。

| 文件 | 字节数 | SHA256 |
|---|---:|---|
| [Suzanne.gltf](Suzanne.gltf) | 3730 | `7E8AE013010AFF530162EF2795CEC74C2646019E224AF17BCFB691664F0F0AEC` |
| [Suzanne.bin](Suzanne.bin) | 590400 | `B85C2727AA41318E00673D8892F5879D46FB6E476E280F28EE1FEBD07602B6B8` |
| [Suzanne_BaseColor.png](Suzanne_BaseColor.png) | 493251 | `1C009F45E53B260CEECD383F2C356ED08B08E05ACD4633BCC713213319B2092B` |
| [Suzanne_MetallicRoughness.png](Suzanne_MetallicRoughness.png) | 698438 | `89539CB3E7C7B6C17DA3700569B1037E137DA860958201F491621E2B9735C6F2` |

## 运行与验证

从项目根目录执行；Shader 需先按 [README](../../../README.md#手动编译-shader) 准备，与当前三维顶点布局匹配。

```powershell
cmake --build build --config Debug --target MadCat MadCatGltfInspect
.\build\Debug\MadCatGltfInspect.exe assets\models\Suzanne\Suzanne.gltf
.\build\Debug\MadCat.exe --gltf assets\models\Suzanne\Suzanne.gltf
```

导入统计应为 1 Mesh、1 Primitive、11808 顶点、11808 索引（3936 个三角形）、1 节点和 1 场景根。源文件为 glTF 2.0，无必需扩展、动画或蒙皮；外部缓冲为 590400 字节。

关闭手动运行的窗口后，可执行现有自动窗口检查：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Debug -Scenario model -ModelPath assets/models/Suzanne/Suzanne.gltf
cmake --build build --config Release --target MadCat MadCatGltfInspect
powershell -NoProfile -ExecutionPolicy Bypass -File tests/TriangleWindowChecks.ps1 -Configuration Release -Scenario model -ModelPath assets/models/Suzanne/Suzanne.gltf
```

2026-11-01 实测 Debug／Release 构建、导入与窗口检查通过：初始绘制、模型父节点变换、相机父节点变换、resize、最小化恢复及正常退出；每次启动只上传一次顶点和索引，重建后复用。Debug 开启同步验证，无 warning／error。窗口截图与日志在 `build/verification/triangle/*-model-Suzanne-*`，构建与检查汇总在 `build/verification/external-gltf/`，均不纳入 Git。

此模型的窗口验证使用可见性冒烟检查并检查截图，没有独立逐像素参考。颜色仍来自位置包围盒，预览沿用 Sandbox 的模型与相机姿态，未应用源文件的法线、UV、材质或贴图；这次验证不表示材质、完整模型外观或 F03 已验收。后续 CPU [模型实例检查](../../../tests/ModelSceneChecks.cpp) 已用此文件验证双实例 Entity、独立变换／删除与共享几何；共享 GPU 几何和实例绘制仍待接入。
