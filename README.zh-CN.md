# SDF_2D_generator

[English README](README.md)

本仓库包含一个 Qt Widgets 图形界面程序，以及若干 C++ 示例，用于从位图生成 2D Signed Distance Field (SDF) 图像。

## 功能亮点
- GUI 流程：加载图片、设置输出尺寸/阈值/最大距离、预览、可取消、保存 PNG。
- 多线程距离变换（按行/列两次扫描）加速 SDF 生成。
- 独立的 C++ 控制台示例，内置 `lodepng`，无需额外依赖。
<img width="994" height="742" alt="image" src="https://github.com/user-attachments/assets/b73d94c6-2c9c-4401-a2e0-0dce1caa9823" />

## 仓库结构
- `Qt_project/SDF_2D_generator` - Qt Widgets 应用（CMake，C++17）。
- `SDFGenerate_cpp` - 控制台示例（CMake，C++11）。
- `README.md` - 英文说明。

## 构建：Qt GUI
依赖：
- CMake 3.16+
- Qt 5 或 Qt 6（Widgets + LinguistTools）
- 支持 C++17 的编译器

构建：
```bash
cmake -S Qt_project/SDF_2D_generator -B build
cmake --build build
```

在 `build` 目录中运行生成的 `SDF_2D_generator` 可执行文件（也可用 Qt Creator 打开并构建运行）。

### GUI 使用方法
1. 点击 **Browse** 选择图片。
2. 调整输出尺寸、阈值（灰度分界）和最大距离。
3. 点击 **Generate SDF** 并等待进度条完成。
4. 点击 **Save SDF** 导出 PNG。

## 构建：命令行示例
依赖：
- CMake 3.16+
- 支持 C++11 的编译器

构建：
```bash
cmake -S SDFGenerate_cpp -B build_cli
cmake --build build_cli
```

### 命令行说明
- `SDFGenerate` 读取 `source600.png`，输出 `target600.png`（同时生成原始数据文件 `target600`）。
- 输出尺寸、最大距离、阈值写在 `SDFGenerate.cpp` 的 `#define` 中，需手动修改。
- `GenerateSelf` 会读取 `source.png` 并打印其尺寸。

## 许可证
仓库中未包含许可文件。
