# SDF_2D_generator

[简体中文](README.zh-CN.md)

A Qt Widgets GUI and C++ examples for generating 2D Signed Distance Field (SDF) images from bitmap inputs.

## Features
- GUI workflow: load image, set output size/threshold/max distance, preview, cancel, and save PNG.
- Multithreaded distance transform (row/column passes) for faster SDF generation.
- Standalone C++ console demos using the bundled `lodepng` (no extra deps).

## Repo Layout
- `Qt_project/SDF_2D_generator` - Qt Widgets app (CMake, C++17).
- `SDFGenerate_cpp` - CMake-based console demos (C++11), used [lodepng library](https://github.com/lvandeve/lodepng) to implement.
- `README.zh-CN.md` - Chinese README.

## Build: Qt GUI
Requirements:
- CMake 3.16+
- Qt 5 or Qt 6 (Widgets + LinguistTools)
- A C++17 compiler

Build:
```bash
cmake -S Qt_project/SDF_2D_generator -B build
cmake --build build
```

Run the generated `SDF_2D_generator` executable from the `build` folder (or open the project in Qt Creator and build/run there).

### GUI Usage
1. Click **Browse** to load an image.
2. Adjust output size, threshold (grayscale cutoff), and max distance if needed.
3. Click **Generate SDF** and wait for the progress bar.
4. Click **Save SDF** to export a PNG.

## Build: CLI Demos
Requirements:
- CMake 3.16+
- A C++11 compiler

Build:
```bash
cmake -S SDFGenerate_cpp -B build_cli
cmake --build build_cli
```

### CLI Notes
- `SDFGenerate` reads `source600.png` and writes `target600.png` (plus a raw `target600` file) in the working directory.
- Output size, max distance, and threshold are hard-coded in `SDFGenerate.cpp`; edit the `#define` values to change them.
- `GenerateSelf` is a small helper that reads `source.png` and prints its dimensions.

## License
No license file is included in this repository.
