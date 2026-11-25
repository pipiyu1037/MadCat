# Lantern 静态验证场景

带底座、支架、链条和灯体的静态路灯场景。保留 3 个 Mesh、4 个节点及原始贴图，用于验证层级、纹理上传和 Forward 图切换。

来源：[Khronos glTF Sample Assets / Lantern](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/4995a638c96dbc94376d3d6164e70fee3ac7f7a2/Models/Lantern)，固定提交 `4995a638c96dbc94376d3d6164e70fee3ac7f7a2`。glTF、外部缓冲和图片原样下载，没有重导出、改节点或格式转换。

署名：© 2017 Microsoft，sbtron（原始版本）；上游另列 © 2018 Frank Galligan（Draco 压缩版本贡献），本目录使用未压缩 glTF。

许可：CC0-1.0，保留原文 [LICENSE.txt](LICENSE.txt) 和上游说明 [upstream-readme.txt](upstream-readme.txt)。[source.json](source.json) 记录每个文件的固定来源 URL、字节数和 SHA256，便于重新下载及核对。许可和文件按各自上游内容保留。

从仓库根目录运行：

```powershell
cmake --build build --config Debug --target CustomPass MadCatShaders CustomPassShaders
.\build\Debug\CustomPass.exe --fit assets/models/Lantern/Lantern.gltf
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario lantern
```

`--fit` 在示例侧把选中场景的节点世界空间 AABB 居中、等比缩放，最长半边为 0.7；原始资产保持不变。面板的 Forward 复选框控制真实图节点连接，关闭后显示背景与 UI。仅验证静态导入、当前 Lambert／基础色路径的可见性、切换与生命周期；外部模型没有完整逐像素外观参考，不能据此声称 PBR、法线贴图或自发光已经支持。解析平面另有独立参考像素检查。

| 文件 | 字节数 | SHA256 |
|---|---:|---|
| [Lantern.gltf](Lantern.gltf) | 5970 | `2106fea76c458caf43078c34ae7da7016cedd1f22e6f4badf94590be590f53d6` |
| [Lantern.bin](Lantern.bin) | 231324 | `ef0e2e89e4b14ff9035b43867a16dc3218569b29363ccb16b283b86f1dc50eb0` |
| [Lantern_baseColor.png](Lantern_baseColor.png) | 4461532 | `a2d6aa660f0b9ce46b3863e955248454e73be96063218b799af9f750e0f7be71` |
| [Lantern_emissive.png](Lantern_emissive.png) | 169335 | `eafa6390501d537f5bbcf92c64ad1c39d2493aef4508e9cc9d537771efe8bad7` |
| [Lantern_normal.png](Lantern_normal.png) | 2619883 | `3318a4d3bef8be53c192d87b7401145493774126c54a22e50e1269f9476065fa` |
| [Lantern_roughnessMetallic.png](Lantern_roughnessMetallic.png) | 2078597 | `01b8105756fd86f13e66602f85ee03e7e5daf22a8531f32bef8bbe678cf38cf6` |
| [upstream-readme.txt](upstream-readme.txt) | 1080 | `58a4903ae9c0bbb7d3f6ff1b9263b3b480edb7ca82109afdb06723275835a6c7` |
| [LICENSE.txt](LICENSE.txt) | 6533 | `a43fc2004d1e073f1107a1f6146664d08dae6cea93cb828742a4bec6da828e91` |

法线贴图现已接通：运行 `CustomPass.exe --pbr --fit assets/models/Lantern/Lantern.gltf`，可用 Normal map 开关对比。材质的 normalTexture 使用源切线与 UV0，以线性 UNORM 采样，MR 与法线两张 2048² 数据图片分别上传；几何与原资产文件保持原样。按 8／9 检查非均匀缩放／镜像。当前综合测试要求开关产生可测画面差异，并检查视图／窗口生命周期；没有独立逐像素外观参考。
