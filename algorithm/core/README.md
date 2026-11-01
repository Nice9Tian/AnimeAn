# algorithm/core

三级机制。不依赖 Qt 的基础类型，命名空间 `AnimeCore`。`algorithm/`、`projectio.*`、`clipreader.*` 建在这一层上，这样计算层不链接 Qt。

## 规则

- **不依赖 Qt。** 这里的 `.h`、`.cpp` 不许 include 任何 Qt 头文件，也不许出现 `Q` 开头的类型和 `qreal`。`animean_corelib` 的包含路径里没有 Qt，写错了编译就过不去。
- **方法名和 Qt 相同。** 计算层的代码已经按 Qt 的写法写成，方法名不变，替换时改动最小，比如 `isEmpty()`、`append()`、`first()`、`elementAt()`。只实现仓库里实际用到的方法，不补全 Qt 的整套接口。
- **行为和 Qt 相同。** 返回值、边界情况、空值和无效值的处理都以真实 Qt 为准。每种类型在 `tests/core/<类型>_tests.cpp` 里和真实 Qt 做差分测试，同一组输入两边结果相同。浮点要求逐位相同；做不到的地方（比如路径布尔运算）在测试里写清容差和原因。
- **不抄 Qt 源码。** 机器上也没有 Qt 源码。行为靠差分测试对齐，代码自己写。
- **隐式共享。** `Vector`、`Map`、`Hash`、`Set`、`String`、`Path`、`Image` 复制时只增加引用计数，写入时才分离。撤销快照靠这一点才便宜，见 `../scenehistory.h`。
- **和 Qt 的类型互转。** 核心类型通过 `bridge.h` 的 `Bridge<Foreign>` 开放隐式转换，转换代码在 `qtbridge_*.h`，只有 Qt 那一侧的代码才 include。计算层自己不 include 它们。

## 文件

| 文件 | 内容 |
|---|---|
| `bridge.h` | 和外部类型互转的机制 |
| `geometry.h` | `PointF`、`Point`、`LineF`、`RectF`、`Rect`、`Size`、`Transform`，以及 `qFuzzyCompare` 这类小函数的对应物 |
| `containers.h` | `Vector`、`Map`、`Hash`、`Set`、`Queue` |
| `color.h` | `Color` |
| `string.h`、`string.cpp` | `String`，内部是 UTF-16，下标含义和 `QString` 相同 |
| `json.h`、`json.cpp` | `JsonValue`、`JsonObject`、`JsonArray`，读写工程文件 |
| `path.h`、`path.cpp` | `Path`，对应 `QPainterPath` 除布尔运算以外的部分 |
| `pathops.h`、`pathops.cpp` | 折线的布尔运算和描边轮廓，建在 Clipper2 上 |
| `pathbool.h`、`pathbool.cpp` | 路径的布尔运算和描边轮廓，把 `Path` 展平后交给 `pathops` |
| `image.h`、`image.cpp` | `Image`，像素缓冲和 PNG 编解码，建在 stb 上 |
| `painter.h`、`painter.cpp` | `Pen`、`Brush`、`Painter`，对应画板用到的那部分 `QPainter`。`PaintDevice` 是它画到的地方：指令表或内存里的图片 |
| `qtbridge.h`、`qtbridge_*.h` | Qt 一侧的转换，计算层不用 |

第三方库在 `../../external/`，版本记在各自的 `SOURCE.txt`。

## 构建和测试

```bash
cmake -S algorithm/core -B build/core -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.9.1/mingw_64 -DBUILD_TESTING=ON
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

## 新旧比较

`tests/core/golden_dump.cpp` 用同一批输入跑计算层的全部公开函数，`golden_compare.py` 比较结果。`tests/core/golden_run.ps1 -OldTree <换类型之前的检出>` 把它编译在新旧两棵树上。除布尔运算的产物按面积和外框比较外，其余结果要求逐位相同，包括工程文件的序列化字节。
