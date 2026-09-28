# 历史与 scriptData（history_scriptdata）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。代码：`algorithm/scenehistory.*`、`openglwidget.cpp`（`commitHistory` / `goToHistory` / `resetHistory`）、`mainwindow.cpp`（全局排序、跨板撤销的界面切换）、`pyfile/script_store.py`。2026-09-28 从架构文档下沉。

## 快照式历史

- 每步操作完成后整份 `AnimeSceneModel` 入历史（Qt 隐式共享，只有改动的部分占内存）；条目 0 是基线；每块板一份，有条数上限。
- 每条带全局单调序号：Ctrl+Z 撤的是两块板合起来最近的一步，Ctrl+Y 严格按反序恢复；任一块板上的新操作清掉两块板的重做尾巴。
- 撤到另一块板上的操作时，主窗口显示并激活那块板，状态栏说明。
- 恢复是整份拷回模型，Python 侧的场景指针保持有效；恢复后强制重新施加两块板固定的场景身份（main / child），并派发 `historyrestore` 事件让脚本重建自己的缓存（引导线覆盖层、工具锁定、单元焦点）。
- 打开工程重置两块板的历史；只打开 `.textureview` 重置纹理板。历史不写入文件。
- Python 改了模型后调 `ui.history_commit(label, view)` 成为一条可撤销记录；`ui.history_undo` / `ui.history_redo` 也可用。手势 hook 里 `message["cancel_history"]` 可阻止 C++ 为这一步提交（避免空记录截断重做尾巴）。

## scriptData

- 场景里一个 C++ 不解释的字符串字段，随快照与 `.anproj` / `.textureview` 一起走。
- `script_store.read / write(scene, key, value)` 按顶层键分命名空间，每个工具只动自己的键、保留其余。现有键：Auto Mapping 资产（H/V 轴线、Mapping Area，按板）、`mapping_units`（单元配置，只在主场景）、`additional_line`、`fold_horizon`、`palette`。
- 因为在快照里，画引导线、点 Mapping Area、点 × 删除都是可撤销的；撤销 / 重做后脚本经 `historyrestore` 重建字典和覆盖层。
- 纹理板存自己板的资产，主场景存自己板的资产加单元 meta；各自的撤销恢复各自的部分。

## 边界

- 斥力板预览用的临时内部层在提交前删除，历史里没有它。
- 无 id 的手绘笔画在编辑后整体重编号，所以 To 3D 导出里的 `src` / `anchors` 只对导出时刻有效。
