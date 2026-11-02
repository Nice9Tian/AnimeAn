# algorithm/core

三级机制。不依赖 Qt 的基础类型，命名空间 `AnimeCore`。计算层（`algorithm/`、`projectio.*`、`clipreader.*`）、画板（`openglwidget.*`、`backend/board/`）与后端会话（`backend/business/`、`backend/session/`）全部建在这一层上。

## 规则

- **不依赖 Qt。** 这里的 `.h`、`.cpp` 不许 include 任何 Qt 头文件，也不许出现 `Q` 开头的类型和 `qreal`。`animean_corelib` 的包含路径里没有 Qt，写错了编译就过不去。
- **方法名和 Qt 相同。** 接口沿用同名的几何、容器、路径、画笔方法，比如 `isEmpty()`、`append()`、`first()`、`elementAt()`。只实现仓库里实际用到的方法，不补全整套接口。
- **行为和 Qt 相同。** 返回值、边界情况、空值和无效值的处理都以真实 Qt 为准。每种类型在 `tests/core/<类型>_tests.cpp` 里和真实 Qt 做差分测试，同一组输入两边结果相同。浮点要求逐位相同；做不到的地方（比如路径布尔运算）在测试里写清容差和原因。
- **不抄 Qt 源码。** 机器上也没有 Qt 源码。行为靠差分测试对齐，代码自己写。
- **隐式共享。** `Vector`、`Map`、`Hash`、`Set`、`String`、`Path`、`Image` 复制时只增加引用计数，写入时才分离。撤销快照靠这一点才便宜，见 `../scenehistory.h`。
- **测试桥接。** 核心类型通过 `bridge.h` 的 `Bridge<Foreign>` 开放隐式转换，转换代码在 `qtbridge_*.h`，仅供差分测试在开启 Qt 时互转；产品代码不 include 它们。

## 文件与类型

| 文件 | 内容 |
|---|---|
| `bridge.h` | `Bridge<Foreign>`：和外部类型互转的机制 |
| `geometry.h` | `Point`、`PointF`、`LineF`、`Rect`、`RectF`、`Size`、`SizeF`、`Transform` |
| `containers.h` | `Vector`、`List`（`Vector` 的别名）、`Map`、`Hash`、`Set`、`Queue` |
| `color.h` | `Color` |
| `string.h`、`string.cpp` | `String`、`StringList`、`Char`、`Latin1Char`、`Latin1String`，内部是 UTF-16，按码元索引 |
| `json.h`、`json.cpp` | `JsonValue`、`JsonObject`、`JsonArray`、`JsonDocument`、`JsonParseError`、`JsonVariant`，读写工程与设置文件 |
| `path.h`、`path.cpp` | `Path`、`FillRule`（`OddEvenFill`、`WindingFill`）：三次贝塞尔路径、外框、弧长、百分比取点、展平与点包含测试 |
| `pathops.h`、`pathops.cpp` | 折线的布尔运算和描边轮廓，建在 Clipper2 上 |
| `pathbool.h`、`pathbool.cpp` | `Path` 的布尔运算（`united`、`intersected`、`subtracted`）和描边轮廓（`strokeOutline`），把 `Path` 展平后交给 `pathops` |
| `image.h`、`image.cpp` | `Image`：ARGB32 / RGBA8888 像素缓冲、缩放、变换和 PNG 编解码，建在 stb 上 |
| `painter.h`、`painter.cpp` | `Pen`、`Brush`、`Painter`、`PaintDevice`、`PaintState`。`PaintDevice` 是绘制目标：指令表（`BoardSurface` 里的 `RecordingDevice`）或内存位图（`RasterDevice`） |
| `rasterdevice.h`、`rasterdevice.cpp` | `RasterDevice`：`Painter` 画到内存 `Image` 的软件光栅器（扫描线覆盖率填充、描边、虚线、合成模式、图片采样） |
| `file.h`、`file.cpp` | `File`、`SaveFile`、`FileInfo`、`Dir`、`CoreApplication`（`applicationDirPath`），文件与路径操作 |
| `settings.h`、`settings.cpp` | `Settings`：机器级偏好设置，读写 `%APPDATA%\AnimeAn\settings.json`（环境变量 `ANIMEAN_SETTINGS_DIR` 可覆盖目录） |
| `qtbridge.h`、`qtbridge_*.h` | 差分测试专用的互转头文件，产品目标不引用 |

第三方库在 `../../external/`，版本记在各自的 `SOURCE.txt`。

## 构建和差分测试

产品构建不需要 Qt：只有 `algorithm/core/CMakeLists.txt` 在 `BUILD_TESTING` 下用 `find_package(Qt6 QUIET COMPONENTS Core Gui)` 探测，不传 Qt 路径时照常编 `animean_corelib` 和 `animean_core`，只是不编 `core_*` 测试目标。

要编译并运行差分测试（`tests/core/*_tests.cpp`），配置时开启 `BUILD_TESTING=ON` 并通过 `-DCMAKE_PREFIX_PATH` 指定 Qt6 路径：

```bash
cmake -S algorithm/core -B build/core -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.9.1/mingw_64 -DBUILD_TESTING=ON
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

## 新旧比较

`tests/core/golden_dump.cpp` 用同一批输入跑计算层的全部公开函数，`golden_compare.py` 比较结果。`tests/core/golden_run.ps1 -OldTree <换类型之前的检出>` 把它编译在新旧两棵树上。除布尔运算的产物按面积和外框比较外，其余结果要求逐位相同，包括工程文件的序列化字节。
