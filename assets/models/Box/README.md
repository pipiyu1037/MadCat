# Box 静态验证场景

单网格、单材质的立方体场景，用于简单几何对照和 Forward 开关验证。

来源：[Khronos glTF Sample Assets / Box](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/4995a638c96dbc94376d3d6164e70fee3ac7f7a2/Models/Box)，固定提交 `4995a638c96dbc94376d3d6164e70fee3ac7f7a2`。glTF、外部缓冲和图片原样下载，没有重导出、改节点或格式转换。

署名：© 2017 Cesium；Cesium（全部模型内容）。

许可：CC-BY-4.0，保留原文 [LICENSE.txt](LICENSE.txt) 和上游说明 [upstream-readme.txt](upstream-readme.txt)。[source.json](source.json) 记录每个文件的固定来源 URL、字节数和 SHA256，便于重新下载及核对。许可和文件按各自上游内容保留。

从仓库根目录运行：

```powershell
cmake --build build --config Debug --target CustomPass MadCatShaders CustomPassShaders
.\build\Debug\CustomPass.exe --fit assets/models/Box/Box.gltf
```

`--fit` 在示例侧把选中场景的节点世界空间 AABB 居中、等比缩放，最长半边为 0.7；原始资产保持不变。面板的 Forward 复选框控制真实图节点连接，关闭后显示背景与 UI。仅验证静态导入、当前 Lambert／基础色路径的可见性、切换与生命周期；外部模型没有完整逐像素外观参考，不能据此声称 PBR、法线贴图或自发光已经支持。历史解析平面参考检查已移除；本模型继续供手动预览，当前自动检查见[复杂场景测试](../../../README.md#复杂场景测试)。

| 文件 | 字节数 | SHA256 |
|---|---:|---|
| [Box.gltf](Box.gltf) | 2898 | `4a0d69eecfce0672a50b71dc218cbacec6c53fe2445040c235c6314b1b2c41b9` |
| [Box0.bin](Box0.bin) | 648 | `3266a8e39b9f425b3341cbe5eec7849f44310256bfa651e6b8b40c85ce0ccafb` |
| [upstream-readme.txt](upstream-readme.txt) | 898 | `ae63b4ad1c8136b881686e66faf930c9b9e71fa823a7a4b1738076ae06739c35` |
| [LICENSE.txt](LICENSE.txt) | 13034 | `c8041334a99cb48852b2139253cbde5f8ac87e27f124aed51223e0d5bf0080eb` |
