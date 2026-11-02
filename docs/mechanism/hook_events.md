# 事件流与声明式界面（hook_events）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。代码：`openglwidget.cpp`（派发）、`pythonbind/python_bindings.cpp`（绑定与回调注册）、`pyfile/python_hooks.py`（注册表与 `dispatch`）、`backend/session/backendsession.cpp` 与 `backend/business/animeanbusiness.cpp`（菜单 / 设置窗 / 图层右键菜单的构建与选项 JSON 推送）、`webui/src/ui/options.js`（选项面板）。2026-09-28 从架构文档下沉。

## 通信

| 谁 ↔ 谁 | 方式 |
|---|---|
| C++ → Python | **hook 事件**：C++ 组一个 message dict 调 `python_hooks.dispatch`。事件族：绘制交互事件（`update`、`linefinish`、`erasefinish`、`deletefinish`、`fillfinish`、`movefinish`）；前置请求（`fillrequest`、`visibility`，Python 置 `handled` 则 C++ 不做内建行为）；工具面（`handle`：arm / pick / press / move / release / cancel / hover / view，带修饰键、缩放、颜色，`pick` 可回 `grab` 认领拖拽）；界面（`option`、`menu`、`layermenu`、`viewbutton`、`overlayremove`、`overlayaction`、`pad`；带 `id` 的菜单项回来时，`menu` 是声明它的菜单的名字（如 `file`、`windows`），页面给了 `checked` 才带上，不带表示切换）；对话框（`dialog`：网页对一个对话框的回答 `button` / `accept` / `cancel` / `closed` / `error`，或请求开一个 `request`，见 `windows_theme.md`「对话框窗口」）；状态（`framechange`、`layerchange`、`onion`、`historyrestore`）；脚本工具（`extra`）。Python 用 `ui.set_hook_events` 推订阅掩码，C++ 对无人订阅的事件早退；`update`、`hover` 与覆盖层拖动的 `move` 按 33ms 节流（覆盖层拖动在窗口结束时补发被跳过的末尾 `move`），`linefinish` 是唯一保证到达的绘制事件 |
| Python → C++ | `animean_python` 绑定：模型操作（`SceneModel` / `VectorImage`）、几何（`vectorlogic`）、`ui` 门面（刷新与冻结、覆盖层、绘制颜色、光标、工具锁定、填充绘制模式、历史提交 / 撤销、窗口列表 / 显示 / 选页、位移预览、斥力板回写、绘制参数、主题、订阅掩码、刷新选项面板；对话框栈 `ui.set_dialogs`、Python 自己的状态 `ui.broadcast(key, json)`（键以 `py/` 开头）、读一份状态 `ui.state(key)`、像网页一样调一个后端方法 `ui.call(method, params)`，失败抛 `RuntimeError`） |
| Python 全局 | `__main__` 里的 `model`、`main_model`、`child_model`、`active_view`、画布尺寸，随选择变化同步。`animean_python.get_scene()` 返回的是信息字典（`sceneName`、`scene`），不是模型对象 |
| 声明式界面 | 选项面板 JSON（内建工具在 `childrenpanel/tool_controls/*.json` 与 `toolcontrol.options_for_tool`，脚本工具由 `toolcontrol.options_for_extra_tool_json` 生成；控件类型 slider / list / check / button / color / subwindow / settings，`visible_when` 联动）；菜单 `register_menu`（整条菜单栏都由 Python 声明，见下文「菜单栏」）；设置窗 `register_settings`；视图按钮 `register_view_button`（目前无人注册）；图层右键 `register_menu_provider`（上下文带 `layer_id`、`tag`、`owner_group`、`owner_tag`；条目支持 `settings` / `separator`） |
| 网页 ↔ 后端 | WebSocket 调用与状态推送；`SelectionAttention` 约束帧 / 层 / 资产的选择；后端按脏键合并推送 `state` |

## 菜单栏

整条菜单栏都是 Python 声明的：File、Edit、Theme、Windows 和纹理板的 File、Setting 在 `pyfile/app_menus.py`，其余由脚本模块登记。C++ 只按 `python_hooks.menus_json` 的顺序和内容建菜单栏，不认识任何一个菜单。

- `items` 可为可调用，打开时求值。
- `host` 指定挂主窗口还是纹理板的菜单栏，同名不同宿主是两个菜单。
- `order` 定位置：数字小的在前，缺省 50，同值按登记先后。主窗口 File 10、Edit 20、Theme 80、Windows 90、Settings 100；纹理板 File 10、Setting 20。
- 项可带 `id`，C++ 照用（`file.open`、`windows.timeline` 这些 id 网页和测试一直在用）。触发它时，作为声明它的菜单的 `menu` 事件回到 Python；没有处理器置 `handled` 就报错。没带 `id` 的项是 `script:<host>:<menu>:<name>`。
- 子菜单可带 `menu`、`host`：这里显示另一个登记的菜单，它的项用那个菜单的 id 和路由。File ▸ Texture ▸ Setting 显示纹理板的 Setting，Setting 里显示纹理板的 View。
- 菜单项背后是通用方法：`history.undo`、`theme.set`、`windows.setVisible`、`windows.showTextureView`、`texture.setChangeable`、`texture.setBackground`，见 `backend_protocol.md`。

## 回传约定

- `message["handled"] = True`：我接管了，C++ 不做内建行为。会改模型的处理器必须在**第一次改动之前**置它——C++ 把 Python 异常吞成调试文本，"处理器中途崩了"和"没人处理"分不开，兜底会在半改的模型上再跑一遍。`dialog` 事件和带 `id` 的菜单项也按它算：C++ 看的是有没有处理器置了它，不是有几个处理器收到（`dialogs.py`、`app_menus.py` 收到每一个），没人置就报错或丢掉那个对话框，见 `windows_theme.md`「对话框窗口」。
- `message["cancel_history"] = True`：这一步不要提交历史（例如引导线捕获失败、笔画已被移除）。
- `message["grab"] = "<id>"`：`pick` 阶段认领拖拽，C++ 把这次按下变成该 id 下的拖动。
- 交互事件 hook 在 C++ 提交历史之前运行，脚本的改动并入同一条记录。

## 覆盖层显示列表

- Python 各工具通过 `overlay_stack.set_items(view, owner, items)` 合成一张列表交给 `ui.set_overlay`，按 owner 排序拼接；C++ 渲染在所有图层之上，从后往前命中。
- 项是折线或闭合多边形，带颜色、线宽、线型；`removable` 的项有 × 徽章，点击派发 `overlayremove`（C++ 不删任何东西）；可带接受徽章（`overlayaction`，action = accept）；`draggable` 的项在任何工具下都能拖，拖动经 handle 事件报告（`move` 按 33ms 节流；被跳过的移动用单次定时器在窗口结束时补发最后位置与修饰键并刷新，松手、取消或拖动清空时定时器失效，松手前另补发一次最终 `move`）。
- 项可带 `label`：C++ 在第一个点处画一个屏幕尺寸不变的气泡，用项的颜色，文字按换行分行，这种项可以只有一个点。C++ 不知道文字是什么意思，Auto Mapping 用它标折叠（`_fold_note_items`）。文字由 `backend/board/textpath.cpp` 调用 Win32 GDI `GetGlyphOutlineW` 提取字形轮廓并转成 `AnimeCore::Path`。
- 徽章的命中框按视口换算到文档空间后再钳制。
- 按下的点同时落在徽章和可拖动项的抓取带上时：离徽章中心 12 屏幕像素以内、或在徽章内圈，算点徽章；外圈再比进入徽章的深度和离线的距离。线穿过自己的徽章时，只靠外圈规则会丢掉三分之一到一半的面积。

## C++ 的通用开关

- `ui.set_locked_tools(view, tools)`：拒绝一组工具、图标变暗、退回 Arrow；Arrow 永不锁。
- `ui.set_fill_paint_mode(view, on)`：允许画笔 / 橡皮交互事件落在 Fill 列上，橡皮擦填充区域。
- 两者都不知道"图层"是什么，谁锁什么由 `pyfile/layer_tool_policy.py` 决定，每次换层整份重推。

## Python 绑定的 API

`python_binding.md`（英文参考）、`python_binding_zh.md`（中文）。
