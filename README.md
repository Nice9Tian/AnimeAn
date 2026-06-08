# AnimeAn

AnimeAn 是一个基于 Qt 的桌面应用，提供动画/矢量相关的编辑界面，并集成了可选的 Python 支持，用于通过 `pybind11` 调用 C++ 数据模型和几何逻辑。

## 主要内容

- Qt Widgets + OpenGLWidgets 桌面界面
- `AnimeModel` / `AnimeVectorLogic` 相关数据与几何处理
- 可选 Python 嵌入与 Python 扩展模块
- Windows 下的部署目标 `deploy_AnimeAn`

## 目录说明

- `main.cpp`：程序入口
- `mainwindow.*`：主窗口
- `animemodel.*`：核心数据模型
- `vectorlogic.*`：矢量与路径处理逻辑
- `pythonbind/python_bindings.cpp`：Python 绑定
- `pythonbind/animemodel.py`：Python 侧封装
- `scripts/agent_build.ps1`：构建与部署验证脚本

## 构建

项目使用 CMake 构建。

### Windows / Qt

1. 安装 Qt 6 或 Qt 5（推荐 Qt 6）。
2. 配置构建目录：

```powershell
cmake -S . -B build
```

3. 构建目标：

```powershell
cmake --build build --config Release
```

### Python 支持

默认开启嵌入式 Python 支持，项目会优先使用：

- `tools/python312`

如果你需要 Python 扩展模块，可打开：

- `ANIMEAN_BUILD_PYTHON_MODULE=ON`

示例：

```powershell
cmake -S . -B build -DANIMEAN_BUILD_PYTHON_MODULE=ON
cmake --build build --config Release
```

## 运行

编译完成后，运行生成的 `AnimeAn` 可执行文件即可。

在 Windows + Qt 6 环境下，还可以使用 CMake 的部署目标：

```powershell
cmake --build build --config Release --target deploy_AnimeAn
```

部署输出会位于 `dist/AnimeAn`。

## Python 入口

Python 相关的说明见 `pybind_readme.md`。

最常见的使用方式是：

```python
from animemodel import AnimeModel

model = AnimeModel()
```

## 许可

仓库内包含第三方依赖 `external/pybind11`。其许可与原项目一致，详见对应目录中的文档。
