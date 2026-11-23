# Cornell Box 场景

来源：[NVIDIA RTXDI Assets / CornellBox](https://github.com/NVIDIA-RTX/RTXDI-Assets/tree/1da7b749ef0e3fb606ab46dde8e5bfbacd078a02/CornellBox)，固定提交 `1da7b749ef0e3fb606ab46dde8e5bfbacd078a02`。通过 Git 稀疏检出和 Git LFS 获取 glTF、外部缓冲与 White.png，文件保持上游原样，未转换或修改材质。

许可：MIT，Copyright (c) 2023 NVIDIA GameWorks。保留完整 [LICENSE.txt](LICENSE.txt) 和[上游说明](upstream-readme.txt)；[source.json](source.json) 记录固定来源、下载 URL、字节数及 SHA256。

场景包含红／绿侧墙、地面、背墙、顶面、两个盒子和顶灯几何。CPU 导入为 5 Mesh／5 Primitive、40 顶点／60 索引、9 节点／1 场景根、4 材质、1 张 8×8 图片。两个盒子共享立方体 Mesh，地面／背墙／顶面共享平面 Mesh；实例化后 8 次绘制仍只上传 40 顶点和 60 索引。

```powershell
cmake --build build --config Debug --target CustomPass MadCatShaders CustomPassShaders CustomPassHdrShaders
.\build\Debug\CustomPass.exe --cornell-box
```

预设默认使用 PBR 直接光，将原始场景 AABB 居中并等比缩放到最长半边 .7，绕 Y 旋转 180°，把原本朝 −Z 的开口转向 +Z 相机；整体沿 X 移动 .4，为面板留出空间，相机视角为 45°。这些变换只作用于实例根，原始资产保持不变。预设提供模型路径，不能与自定义模型路径或 `--pbr-spheres` 同用。

View 的 Final image 使用从开口斜上方射入的单方向光；Base color 显示无光照基础色，再经过现有曝光／Reinhard 显示转换，可同时检查两侧墙的材质。Base color 不表示间接光照，白色盒子与同色背墙可能融为一体，可切到 Camera depth 检查几何遮挡。按 6 旋转光源，7 移动相机，1 或 Restore defaults 恢复。

当前不计算顶灯发光、面积光采样、间接反弹或颜色渗透，最终画面也未连接投射阴影。直接光模式中背光墙面为黑色属于当前光照范围，不应与完整路径追踪的 Cornell Box 参考图比较。原文件 `Light` 材质保留的 emissiveFactor 为 `[17,12,4]`，当前导入器不消费它；这些值也超出 [glTF 核心 emissiveFactor](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#material-emissivefactor) 的 [0,1] 范围，本次没有重写资产或宣称通过完整 glTF 合规检查。后续正式接入自发光时需明确转换方案。

检查命令：

```powershell
cmake --build build --config Debug --target MadCatGltfInspect
.\build\Debug\MadCatGltfInspect.exe assets/models/CornellBox/CornellBox.gltf
powershell -NoProfile -ExecutionPolicy Bypass -File tests/CustomPassWindowChecks.ps1 -Configuration Debug -Scenario cornell
```

窗口检查覆盖场景可见性、基础色模式的红／绿墙、五种视图、相机／灯光变化、Forward 开关、resize 和最小化恢复，并核对资源退役。这是资产接入和显示工作流检查，没有完整逐像素光照参考。
