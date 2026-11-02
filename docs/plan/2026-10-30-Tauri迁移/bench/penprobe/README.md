# 真笔门槛对比探针（WebView2 vs Qt）

本目录与 `webui/pen_probe.html` 提供口径相同的两个采样统计工具，用于比较同一块数位板在 Qt（`QTabletEvent`）和 WebView2（`PointerEvent` + `pointerrawupdate` + `getCoalescedEvents()`）下的样本数、采样率、压感、倾斜与系统手势干扰。两边均将数位笔（`pen`）和鼠标（`mouse`）分开统计，累计显示最近 10 笔与合计，并支持导出相同字段的 JSON。

## 1. 构建并运行 Qt 探针

在仓库根目录下用 PowerShell 执行以下完整命令构建并启动 Qt 探针：

```powershell
$env:PATH = "C:\Qt\6.9.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;" + $env:PATH
cmake -S "docs/plan/2026-10-30-Tauri迁移/bench/penprobe" -B build/penprobe -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.9.1/mingw_64 -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
cmake --build build/penprobe
.\build\penprobe\penprobe.exe
```

在打开的窗口中画线即可实时看到每一笔及累计统计；按键盘 `C` 键会把完整 JSON 结果写入系统剪贴板并同时打印到终端 `stdout`（按 `R` 键可清空重测）。

## 2. 在打包版里打开网页探针

打包版（`dist\AnimeAn\AnimeAn.exe`）支持通过环境变量 `ANIMEAN_START_PAGE` 直接在主窗口加载指定相对页面且不启动后端。在仓库根目录下用 PowerShell 执行：

```powershell
$env:ANIMEAN_START_PAGE = "pen_probe.html"
.\dist\AnimeAn\AnimeAn.exe
```

在打开的 WebView2 窗口中画线后，点击顶部「复制 JSON」按钮（或按 `C` 键）即可把统计 JSON 复制到剪贴板；若剪贴板权限受限，页面会自动弹出文本框供手动复制。测试完成后执行 `Remove-Item Env:ANIMEAN_START_PAGE` 即可恢复正常启动主界面。

## 3. 比什么与通过标准

用同一块数位板和同一支笔，在 Qt 探针和 WebView2 网页探针中按同样手法分别测试两轮（每轮开始前可先清空）：

1. **同样的线各画 10 笔**：
   - **快速长线 10 笔**：横跨画布快速挥笔画 10 条长线，导出 JSON。
   - **慢速短线 10 笔**：小范围慢速精细画 10 条短线，导出 JSON。
2. **样本数与采样率（Hz）**：比较 `summary.pen` 的总样本数（`sampleCount`）、每笔平均样本数（`avgSamplesPerStroke`）以及平均/中位采样率（`avgHz`、`medianHz`）。**通过门槛：WebView2 的样本数不低于 Qt 版的 80%（这个门槛是提议，可按实际手感调整）。**
3. **有没有 `pointercancel`**：检查 `summary.pen.cancelCount`（以及每一笔的 `cancelled`）是否为 `0`，确认绘画过程中笔迹不会被异常中断。
4. **系统手势抢不抢笔**：观察落笔停顿、长按、快速起笔或从窗口边缘起笔时，Windows Ink 长按右键环、边缘滑动等系统手势会不会抢走或吞掉笔的前段采样。
5. **压感与倾斜范围**：检查 `pressureMin`、`pressureMax` 是否覆盖轻落笔到重按的连续区间，`tiltMax`（支持倾斜的数位笔）是否正常变化。
