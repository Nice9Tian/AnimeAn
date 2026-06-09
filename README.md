# AnimeAn

AnimeAn 是一个基于 Qt 的桌面应用，提供动画和矢量相关的编辑界面，并集成了可选的 Python 支持，用于通过 `pybind11` 调用 C++ 数据模型和几何逻辑。

## 主要内容

- Qt Widgets + OpenGLWidgets 桌面界面
- `AnimeModel` / `AnimeVectorLogic` 相关数据与矢量算法
- 可选 Python 嵌入与 Python 扩展模块
- Windows 下的部署目标 `deploy_AnimeAn`

## 目录说明

- `main.cpp`：程序入口
- `mainwindow.*`：主窗口逻辑
- `algorithm/animemodel.*`：核心数据模型
- `algorithm/vectorlogic.*`：矢量路径和几何算法
- `pythonbind/python_bindings.cpp`：Python 底层绑定
- `pythonbind/animemodel.py`：Python 侧高层封装
- `scripts/agent_build.ps1`：Release 构建与部署验证脚本

## 构建

项目使用 CMake 构建。

### Windows / Qt

1. 安装 Qt 6 或 Qt 5，推荐 Qt 6。
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

如果需要构建 Python 扩展模块，可打开：

- `ANIMEAN_BUILD_PYTHON_MODULE=ON`

示例：

```powershell
cmake -S . -B build -DANIMEAN_BUILD_PYTHON_MODULE=ON
cmake --build build --config Release
```

## 部署

构建完成后，可执行生成的 `AnimeAn` 程序。

在 Windows + Qt 6 环境下，也可以使用 CMake 的部署目标：

```powershell
cmake --build build --config Release --target deploy_AnimeAn
```

部署输出位于 `dist/AnimeAn`。

## Python 入口

Python 绑定功能说明：

- 英文设计与 API 说明：`pybind_readme.md`
- 中文功能说明：`python_bind_chinese_readme.md`

最常见的使用方式：

```python
from animemodel import AnimeModel

model = AnimeModel()
```

## 许可

仓库内包含第三方依赖 `external/pybind11`。其许可与原项目一致，详见对应目录中的文档。
