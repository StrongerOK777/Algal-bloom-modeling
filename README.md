# Algal Bloom Modeling

## 1. 项目简介

本项目使用 C++17 实现太湖蓝藻时空分析与预警模拟，核心流程包括：

1. 多光谱 GeoTIFF 读取与可视化。
2. 基于 NDVI 的蓝藻区域提取。
3. 基于光流的短时漂移预测与预警标注。
4. 基于规则的打捞船最小数量仿真。

当前版本已经完成头源分离（.h 仅声明，.cpp 存放实现），并支持通过 CMake 在 macOS、Linux、Windows 上构建。

## 2. 说明

Example 目录中的部分示例代码与素材来自作业中给出的html格式，使用 VS Code 上 html2ipynb 插件进行转换，但是部分内容似乎在 python 3x 上不是很兼容，所以有所改动。

PS：本作业内容为 GPT-5.3-Codex 和人力实现。

## 3. 项目思路与处理流程

### 3.1 总流程

程序入口位于 src/main.cpp，按顺序执行：

1. runPaintOrigin
2. runPaintCompare
3. EarlyWarning::generateEarlyWarningForecastImages
4. EarlyWarning::generateEarlyWarningGif
5. SimulationShip::runSimulation

### 3.2 各模块职责

1. test 模块（src/test.cpp）
	- 读取 GeoTIFF（GDAL）。
	- 将浮点波段做截断线性拉伸后写成可视化 PNG。
	- 叠加时间与卫星标签。

2. paintorigin 模块（src/paintorigin.cpp）
	- 生成两个时刻原始可视化图（Pic1、Pic2）。
	- 调用 ffmpeg 合成基础对比动图 satellite.gif。

3. paintcompare 模块（src/paintcompare.cpp）
	- 计算 NDVI（使用红、近红外波段）。
	- 通过阈值将蓝藻区域二值化。
	- 使用 Farneback 光流估计漂移场。
	- 生成 1~8 小时预测图与综合网格图。
	- 调用 ffmpeg 生成 bloom_forecast.gif。

4. Earlywarning 模块（src/Earlywarning.cpp）
	- 对关键点位进行半径检测（是否被蓝藻影响）。
	- 生成滚动 5 小时预警文字。
	- 输出 bloom_Xh_warning.png 并生成预警 GIF。

5. SimulationShip 模块（src/SimulationShip.cpp）
	- 基于 bloom1、bloom2 估计扩散趋势。
	- 在取水口范围内模拟打捞、扣血与生存判定。
	- 搜索“4小时内血量始终大于0”的最小船数。
	- 输出 ship_simulation.gif 和 summary 图。

## 4. 输入与输出

### 4.1 主要输入

1. Example/GeoTIFF 下的原始多光谱影像。
2. Example/GeoTIFF_Landmasked 下的陆地掩膜后影像。

默认路径由 src/main.cpp 中参数给出，路径是相对程序运行工作目录解释的。

### 4.2 主要输出

程序运行后会在运行目录下 output 文件夹生成：

1. Pic1.png、Pic2.png、satellite.gif
2. infrared1.png、infrared2.png
3. ndvi1.png、ndvi2.png
4. bloom1.png、bloom2.png
5. bloom_1h.png 到 bloom_8h.png
6. bloom_forecast.gif、bloom_forecast_grid.png
7. bloom_1h_warning.png 到 bloom_8h_warning.png
8. bloom_forecast_warning.gif
9. ship_simulation.gif、ship_simulation_summary.png

## 5. 常见坑点与排查建议

### 5.1 构建与依赖类坑

1. 头源分离后，不能再单文件编译。
	- 现象：undefined reference 或 LNK2019。
	- 处理：使用 CMake 或把所有 src/*.cpp 一起编译。

2. CMake 能找到 OpenCV 但找不到 GDAL（或反过来）。
	- 处理：确认 CMake 是否使用了正确工具链。
	- Windows + vcpkg 必须传 CMAKE_TOOLCHAIN_FILE。

3. Windows 上 opencv2/freetype.hpp 缺失。
	- 原因：OpenCV 安装未包含对应模块特性。
	- 处理：重装 OpenCV（vcpkg 下建议带 contrib 与 freetype 特性）。

### 5.2 运行类坑

1. ffmpeg 命令失败。
	- 现象：GIF 没生成，函数返回失败。
	- 处理：先在终端执行 ffmpeg -version，确认 PATH 中可用。

2. 路径正确但读取不到 Example 数据。
	- 原因：运行工作目录不一致。
	- 处理：建议从 build 目录运行程序，保持与默认相对路径一致。

3. 结果图出现异常空白或异常暗。
	- 原因：遥感浮点值直接可视化不稳定。
	- 本项目处理：先做截断拉伸再写 PNG。

4. Windows 中文显示乱码。
	- 当前 CMake 已在 MSVC 下增加 /utf-8。
	- 若终端仍乱码，建议切换 UTF-8 代码页或使用支持 UTF-8 的终端。

### 5.3 算法参数类坑

1. NDVI 阈值、光流参数、位移尺度因子都影响预测结果。
2. 不同数据源分辨率变化会导致船只模拟参数失配。
3. 建议先固定样例数据验证，再逐步调参。

## 6. 从零开始配置教程

以下步骤默认你已安装 Git，并且代码已克隆到本地。

---

### 6.1 macOS（Homebrew）

#### Step 1: 安装工具链与依赖

```bash
brew update
brew install cmake pkg-config opencv gdal ffmpeg
```

#### Step 2: 配置与编译

```bash
cd Algal-bloom-modeling
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

#### Step 3: 运行

```bash
cd build
./output/algal_bloom
```

---

### 6.2 Linux（Ubuntu 示例）

#### Step 1: 安装工具链与依赖

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
	 libopencv-dev libgdal-dev ffmpeg
```

#### Step 2: 配置与编译

```bash
cd Algal-bloom-modeling
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

#### Step 3: 运行

```bash
cd build
./output/algal_bloom
```

---

### 6.3 Windows（vcpkg，推荐方案）

你已经在 Windows 上使用 vcpkg 安装 OpenCV、GDAL、ffmpeg，这个方案非常适合本项目。

#### Step 1: 准备工具

1. 安装 Visual Studio 2022（勾选 Desktop development with C++）。
2. 安装 CMake（或使用 VS 自带 CMake）。
3. 准备 vcpkg 并完成 bootstrap。

#### Step 2: 安装依赖（x64）

```powershell
vcpkg install opencv4:x64-windows gdal:x64-windows ffmpeg:x64-windows
```

如果出现 freetype 头文件缺失，可尝试：

```powershell
vcpkg install opencv4[contrib,freetype]:x64-windows
```

#### Step 3.1: 用 vcpkg toolchain 配置 CMake

```powershell
cmake -S . -B build ^
  -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows
```
#### Step 3.2: 直接在 VS26 中添加路径

具体位于“项目 -> 属性 -> C/C++ -> 高级” 然后把自己安装的包路径填入即可。

#### Step 4: 编译

```powershell
cmake --build build --config Release
```

#### Step 5: 确保 ffmpeg 可执行

执行以下命令确认：

```powershell
ffmpeg -version
```

若失败，请将 ffmpeg.exe 所在目录加入 PATH 后重开终端。

#### Step 6: 运行

```powershell
cd build
.\output\Release\algal_bloom.exe
```

部分生成器可能输出到 .\output\algal_bloom.exe，请按实际目录调整。

## 7. 构建命令速查

```bash
cmake -S . -B build
cmake --build build
```

运行：

macOS/Linux:

```bash
cd build && ./output/algal_bloom
```

Windows:

```powershell
cd build
.\output\Release\algal_bloom.exe
```

## 8. 项目整体框架

```text
Algal-bloom-modeling/
├── CMakeLists.txt
├── README.md
├── Example/
│   ├── GeoTIFF/
│   ├── GeoTIFF_Landmasked/
│   ├── Result example/
│   └── ...
└── src/
	 ├── main.cpp                # 程序入口，串联全流程
	 ├── test.h / test.cpp       # GeoTIFF读取、拉伸、基础可视化
	 ├── paintorigin.h / .cpp    # 原始影像输出与基础GIF
	 ├── paintcompare.h / .cpp   # NDVI、光流、扩散预测
	 ├── Earlywarning.h / .cpp   # 关键点滚动预警
	 ├── SimulationShip.h / .cpp # 打捞船仿真与最小船数搜索
	 └── output/                 # 运行输出目录（示例）
```