# 网页界面和后端的连接

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。代码：`backend/`（后端进程）、`webui/`（网页界面）、`src-tauri/`（桌面外壳）。

## 进程

```text
Tauri 外壳（Rust，只启动和回收）
  └─ WebView2：webui/
        │ WebSocket 127.0.0.1:<端口>
        ▼
AnimeAnBackend.exe：真实模型、真实画板逻辑、嵌入式 Python（pyfile/）
```

后端不链接任何 Qt 模块：用 `backend/platform/eventloop.*` 跑事件循环，`backend/net/wsserver.*` 提供本地 WebSocket 服务，计算层 `animean_core`、画板与会话层全部使用 `AnimeCore` 基础类型（`algorithm/core/`）。

启动参数：`AnimeAnBackend.exe --port <端口，0 表示自动> --token <随机串>`。监听成功后向 stdout 打一行 `ANIMEAN_BACKEND_PORT=<端口>`，只监听 127.0.0.1。

后端起不来时窗口照样打开，网页取 `backend_endpoint` 失败后弹框说明原因。运行中后端异常退出时外壳向主窗口发 `backend-exited`，网页弹框提示重启。

连接地址：`ws://127.0.0.1:<端口>/?token=<随机串>`。token 不对就拒绝。`Origin` 只放行 `tauri://localhost`、`http://tauri.localhost`、`https://tauri.localhost` 和 `http://localhost:<任意端口>`；没有 `Origin` 头的连接放行（测试用）。可以同时连着几个页面，后来的不挤掉先来的，见下面「多个页面」。

## 多个页面

一个后端可以同时连着几个页面（例如外壳之外，开发时在浏览器面板和 Chrome 里各开一页），它们看到同一份工程、同一个画面，任何一页的操作所有页面都跟着变。代码：`backend/net/wsserver.*`（连接编号、广播和单发）、`backend/session/backendsession.*`（`PageState`、`setPresenter`、`handleInputMessage`）、`webui/src/board/fit.ts`。测试：`node backend/tests/multi_page.mjs <AnimeAnBackend.exe>`。

| 什么 | 发给谁 |
|---|---|
| 调用的回复 | 只发给发起调用的那一页（各页的调用编号各自从 1 数起） |
| 状态、画板帧、图片、场数据、`event` | 所有页面 |
| 新页面连上时的全部状态和缓存图片（光标、缩略图） | 只发给新页面；然后两块画板对所有页面重新铺底：后端先让已在的页面释放画板图片（`image.forget`），下一帧是完整帧 |

- **画板只有一个尺寸**，听「操作页」的：第一个连上的页面；之后哪一页在画板上做完一次操作（按下到最后一个键松开，或者滚一下滚轮），哪一页就成为操作页；操作页断开时，在下一轮事件循环由最近操作过的那一页接手；这时有页面正按着指针，就等它松开，由它接手。只把鼠标移过去不算。别的页面报的画板尺寸、纹理板归属（`texture.setHome`）先记在页面名下，轮到它时才生效。
- **换操作页时视图不跳**：画板中心对着的文档位置不动，缩放乘以两边面积比的平方根，两个窗口能看到的文档面积相同。两个窗口形状相同时，接手的一方看到的画面完全不变；来回交接回到原来的视图。纹理板在子控件框里时照旧按内容自动适配；交接时纹理板换了位置（例如一页放在中央、另一页放在子控件框），它回到那个位置上次的视图，不再缩放。
- **别的页面按比例显示**：一帧的尺寸和画板元素差到 1 像素以上时（`fitFrame`），网页把画板和动画叠层按比例缩放、居中显示（`board-mirror`，`object-fit: contain`），指针坐标换算回后端画板的坐标再发。在这一页上做完第一次操作它就成为操作页。那第一笔是在操作页的缩放下画的，屏幕像素计的预算（稳定器、拟合容差）按操作页的屏幕算。
- **一次只有一页拿着指针**：一页按下后，到它最后一个键松开为止，别的页面的指针事件丢弃；按下被丢掉的那一次拖动，后面的移动、松开和中途按下的第二个键也一起丢掉，直到这一页报告没有键按着；滚轮、焦点这类不带按键的消息不结束它。拿着指针的页面断开时，后端在下一轮事件循环替它补一次松开。
- **场显示能力**（`board.capabilities`）各页各报，所有报过的页面都支持才算支持。
- **帧的节流**：任何一页确认了最新一帧，后端就发下一帧，一页卡住不拖住别的页面。连着两个以上页面时，积压超过 512 MB，或者积压超过 16 MB 且 60 秒没有进展的页面会被断开；只连一个页面时不断。断开的页面不自动重连：浏览器里的页面弹框说明，要刷新页面重连（外壳里由 `backend-exited` 提示）。
- 只连一个页面时，行为和以前单连接时相同（新旧对比逐条相同）。唯一的不同：页面在笔按着时断开，那一笔由后端补一次松开收尾，以前是留着没收尾。
- **限制**：没有外壳的页面打开文件选择框时立刻回答「取消」。外壳的端口和令牌不对外，正常使用时浏览器页面连不上外壳的后端；要是真有外壳页面和浏览器页面连着同一个后端，外壳那边的文件选择流程会被浏览器页面的「取消」抢先结束。

## 画板不改逻辑

`PaintOpenGLWidget`（`openglwidget.*`）的画板业务代码保持原样，基类由 `boardbase.h` 定为 `BoardSurface`（`using PaintBoardBase = BoardSurface;`）：

- `BoardSurface`（`backend/board/boardsurface.*`）继承 `AnimeCore::PaintDevice`，持有 `RecordingDevice`，提供 `update()`、`width()`、`height()`、`size()`、`rect()`、`setCursor()`、`unsetCursor()`、`setMouseTracking()`、`setFocusPolicy()`、`devicePixelRatioF()` 和全部事件虚函数。
- `Painter painter(this)` 在 `BoardSurface` 上绘制：`BoardSurface` 是 `PaintDevice`，把绘制调用转给内部的 `RecordingDevice`，录制成带 64 位哈希的指令表。`update()` 合并成一次 0 毫秒定时（`Timer`），定时到了调用 `paintGL()`。
- 鼠标、滚轮、焦点事件由 `BoardSurface::injectMouse`、`injectWheel`、`injectFocus`、`injectBlur` 等构造 `MouseEvent`、`WheelEvent`（`backend/board/inputevent.h`），直接调对应的虚函数；尺寸走 `resizeSurface`。派发给 Python 的钩子消息格式不变。

## 消息

文本帧都是一个 JSON 对象，`t` 字段是类型。二进制帧用于图片和场数据。

### 网页 → 后端

```json
{"t":"call","id":1,"method":"layer.add","params":{"view":"main"}}
{"t":"input","view":"main","kind":"press","x":10.5,"y":20.25,"button":1,"buttons":1,"modifiers":0,"pressure":0.5,"time":123456}
{"t":"ack","view":"main","frame":42}
```

`kind`：`press`、`move`、`release`、`wheel`、`enter`、`leave`、`resize`、`focus`。

- 坐标是画板左上角起的 CSS 像素。
- `button`、`buttons` 的值用 `AnimeCore::MouseButton`（`backend/board/inputevent.h`）：左 1，右 2，中 4。
- `modifiers` 的值用 `AnimeCore::KeyboardModifier`：Shift `0x02000000`，Ctrl `0x04000000`，Alt `0x08000000`。
- `wheel` 带 `dx`、`dy`，单位和 `AnimeCore::WheelEvent::angleDelta`（1/8 度，一格 120）相同。
- `resize` 带 `w`、`h`、`dpr`。

### 后端 → 网页

```json
{"t":"reply","id":1,"ok":true,"result":{}}
{"t":"reply","id":1,"ok":false,"error":"..."}
{"t":"state","key":"layers/main","value":{}}
{"t":"board","view":"main","frame":42,"w":567,"h":690,"keep":120,"ops":[]}
{"t":"image","key":"a1b2","view":"main","w":100,"h":80,"bytes":320000}
{"t":"image.forget","key":"a1b2","view":"main"}
{"t":"field","key":"f1","w":64,"h":64,"version":3,"bytes":65536}
```

- 连接建立后，后端先把全部 `state` 键发一遍。之后只发变了的键，值是该键的完整新值。
- `image` 文本帧之后紧跟一个二进制帧，内容是 RGBA8 非预乘像素，逐行从上到下。
- `image` 的 `view` 说明图是哪块画板画的，只有那块画板收下。光标和缩略图不带 `view`，归页面。
- 画板发过的图由后端记账，每块画板上限 512 MB。超了就从最久没画过的开始发 `image.forget`，网页收到就释放，当前帧画到的图不淘汰。之后再画到时重新发 `image` 帧。后端不留副本，新页面连上时清零记账（已在的页面同时收到 `image.forget`，见「多个页面」），下一帧重新发。
- `field` 文本帧之后紧跟一个二进制帧，内容是 `w * h * 4` 个小端 `Float32`（`RGBA32F`），逐行从上到下，每像素为 `(dx, dy, tag, 0)`：位移、层序标记（0 是无效节点）和保留通道。`version` 为全局单调递增序号。场帧只在发送用到该场的 `board` 帧之前发出；同一 `key` 尚未发出的旧值会被新值直接覆盖，等待 `ack` 期间不发送。

## 绘制指令

`board` 帧的 `ops` 是数组，每个元素也是数组，第一项是操作名。

| 操作 | 参数 | 含义 |
|---|---|---|
| `T` | m11, m12, m21, m22, dx, dy | 设置变换，同 `AnimeCore::Transform` |
| `P` | r, g, b, a, width, cap, join, miter, cosmetic, dash | 设置画笔。`a` 是 0–255。`cap`：0 平头、1 方头、2 圆头。`join`：0 尖角、1 斜角、2 圆角。`dash` 是数组或 `null`，长度以线宽为单位。`a` 为 0 表示不描边 |
| `B` | r, g, b, a | 设置纯色画刷。`a` 为 0 表示不填充 |
| `G` | kind, coords, stops | 设置渐变画刷。`kind`：`linear`、`radial`。`stops` 是 `[pos, r, g, b, a]` 数组 |
| `O` | opacity | 全局不透明度，0–1 |
| `M` | mode | 合成模式：`source-over`、`source`、`destination-out`、`multiply` 等，用 Canvas 的名字 |
| `K` | path 或 `null`, rule | 设置裁剪。`rule`：0 奇偶、1 非零。`null` 表示取消 |
| `R` | x, y, w, h | 按当前画笔、画刷画矩形 |
| `S` | path, rule | 按当前画笔、画刷画路径 |
| `I` | key, sx, sy, sw, sh, dx, dy, dw, dh | 画图片，`key` 对应先发的 `image` 帧 |
| `H` | smooth | 图片平滑开关，0 或 1 |
| `F` | key, version, x, y, w, h, maxEdge, passes | 开启场作用域。`key`、`version` 对应先发的 `field` 帧，`x, y, w, h` 是场覆盖的画布矩形，`maxEdge` 是细分最大边长，`passes` 是 `[[tagLo, tagHi, opacity, r, g, b, a], ...]`（未指定 `tint` 时 `r, g, b, a` 均为 0）。作用域内的 `S` 指令按该场变形绘制 |
| `f` | 无 | 结束场作用域 |

`path` 是扁平数组，`[命令, 坐标...]` 连续排列。命令：0 移到（x, y），1 直线到（x, y），2 三次曲线到（c1x, c1y, c2x, c2y, x, y），3 闭合。

## 增量绘制

记录引擎给每条指令算哈希。每帧 `keep` 是和上一帧相同的前缀条数，`ops` 只含之后的指令。网页把自己保存的指令表截到 `keep` 条，再接上 `ops`，就是这一帧的完整指令表。第一帧和尺寸变化后 `keep` 是 0。

已提交的笔画在指令表前面，落笔时只有末尾的当前一笔在变，所以网页可以把稳定的前缀缓存成位图，每帧只画末尾。

网页每画完一帧发一次 `ack`。后端在收到上一帧的 `ack` 之前不发新帧，只记下「要重画」，所以网页慢时不会堆积。

## 渲染器

同一份指令表有两种画法，语义以 `webui/src/board/renderer.ts` 的 `executeOps` 为准。

| 渲染器 | 文件 | 稳定前缀 | 全量重画（3000 条笔画，每条 40 段三次曲线） |
|---|---|---|---|
| WebGL2，缺省 | `glrenderer.ts`、`tess.ts` | 画在多重采样缓冲里，解析成一张纹理；后缀帧先铺这张纹理 | 几何缓存命中 4–7 ms，换缩放档重建 260–290 ms |
| Canvas 2D | `renderer.ts` | 画在一张缓存位图里 | 约 600 ms |

- 上表是 RTX 3080 上的实测，比较页是 `webui/tests/webgl_compare.html`。
- WebGL2 不许把多重采样缓冲 blit 到另一个多重采样缓冲，所以前缀不能直接复制，要先解析成纹理。模板缓冲也带不过去，后缀开始时裁剪要重新写一次。
- 笔画几何按路径内容缓存，缩小时复用，放大超过 1.75 倍才重建。展平误差不超过 0.1 屏幕像素。
- 几何放在一个大顶点缓冲里，GPU 缓冲按整个容量预留，新几何只传新增部分。擦除时一帧要新增几十条切片，缓冲只按数据量分配时每条都会全量上传，单帧要几十毫秒。
- 选择存在页面的 `localStorage`，键是 `animean.boardRenderer`，属于机器级偏好，不进工程文件。Settings 菜单是 Python 的（`pyfile/ui_settings.py`，排在 Windows 后面）：网页启动时经 `tool.option`（`hook` 为 `ui_renderer`）报上正在用的渲染器和 WebGL 能不能用，用户选了以后 Python 广播 `py/preferences`，网页按它切换并存进 `localStorage`。
- 切换时新建 canvas，因为同一个 canvas 只能拿到一种上下文。新渲染器用 `adopt` 接手指令表和图片，不需要后端重发。
- 显卡不支持 WebGL2 或上下文创建失败时退回 Canvas 2D。要读背景的合成模式（`multiply`、`screen` 等）WebGL 没有实现，退回 `source-over`；画板现在不发这些模式，唯一的 `setCompositionMode` 作用在离屏 `AnimeCore::Image`（`RasterDevice`）上。

## 状态键

| 键 | 内容 |
|---|---|
| `window` | 标题、状态栏文字、活动画板 `activeView`、素材面板目标画板 `assetView`、画布尺寸、工程与纹理路径、`pythonDebug` 日志（最多保留 2000 条） |
| `theme` | `AnimeTheme` 的模式和全部颜色 |
| `menus` | 主窗口菜单栏的完整结构，全部是 Python 声明的（File、Edit、Theme、Windows 在 `pyfile/app_menus.py`，其余由脚本登记），按 Python 给的顺序。项有 `id`、`text`、`shortcut`、`kind`、`checked`、`enabled`、`children`，可另带 `extraShortcuts`（不显示的快捷键）。见 `hook_events.md`「菜单栏」 |
| `textureMenus` | 纹理板菜单栏的完整结构，也是 Python 声明的：`File`、`Setting`（`app_menus.py`），加脚本登记的 `child` 菜单（如 `View`）。脚本按钮行不在这里，在 `windows.textureViewButtons` |
| `textureSettings` | 纹理板的设置：`changeableTimeline`、`changeableTexture`、`background`（`white`、`black`、`transparent`），纹理板的 Setting 菜单按它打勾 |
| `tools` | 工具页、当前工具、被锁定的工具 |
| `toolOptions` | Python 返回的选项 JSON |
| `drawColor`、`drawSettings` | 当前颜色、绘制设置 |
| `layers/main`、`layers/child` | 图层树 |
| `assets/main`、`assets/child` | 素材列表 |
| `timeline` | 帧数、当前帧、播放状态、洋葱皮 |
| `thumbnails` | `{ revision }`，帧缩略图缓存的版本，画面变了就加一 |
| `history` | 历史记录 |
| `windows` | 面板的显示状态和当前页（`paint` 的当前页按焦点算，见 `windows_theme.md`）、时间轴的停靠状态、纹理子控件框是否可见（`textureSubControlVisible`）、纹理板的脚本按钮行（`textureViewButtons`） |
| `cursor/main`、`cursor/child` | 光标：形状名，或者图片 `key` 加热点和 `scale`。位图是屏幕像素，`w`、`h` 也是；热点是逻辑像素，`scale` 是位图每个逻辑像素的像素数，网页用 `image-set` 声明 |
| `freeze` | Python 冻结界面的开关 |
| `pad` | 二维手柄状态（原 Repulsion Pad 用，已归档在 `../../archive_feature/repulsion_pad/README.md`） |
| `viewport/main`、`viewport/child` | 滚动条：`h`、`v` 各含 `min`、`max`、`page`、`value`、`enabled`，由 `BackendSession::buildViewportState` 计算 |
| `dialogs` | 开着的对话框，从下到上，是 `pyfile/dialogs.py` 经 `ui.set_dialogs` 交来的数组，C++ 不认识其中的种类。每项有 `id`、`kind`（`message`、`canvasSize`、`color`、`settings`、`file`）、`modal`、`title` 和该种类的字段，见 `windows_theme.md`「对话框窗口」 |
| `py/<名字>` | Python 自己的状态（`ui.broadcast`），如 `py/preferences`。前缀把它和上面的键隔开 |
| `event` | 一次性通知，`{ name, args }`，不合并、不重发：`contextMenu`、`window.show`、`window.select`、`textureView.surface`、`textureSubControl.surface`、`board.effect`（只发给当时连着的网页，后连上的不补发） |

## 调用

方法名在 `backend/session/backendsession.cpp` 的 `dispatchCall`。图层右键走 `layer.contextMenu`（返回菜单项，含 Python 注册的项）和 `layer.contextAction`。纹理板搬迁时调 `texture.setHome`（`home`：`central`、`control`、`parked`，后端保存离开处的缩放与平移、恢复到达处视口并在到达 `control` 时自动适配），脚本按钮行调 `texture.viewButton`（`view`、`name`、`on`；也认 `id`、`checked`）。网页发 `input` 时带 `time`（毫秒），后端写进事件时间戳，稳定器要用它。`board.capabilities`（参数 `{"view":"main"|"child","fieldDisplay":true|false}`）由网页在画板渲染器就绪后上报该视图是否支持场显示，供 `animean_python.ui.field_display_supported(view)` 查询（缺省 `False`；连着几个页面时要所有报过的页面都支持，页面断开时它的那份作废）。

| 调用 | 参数 | 返回 / 说明 |
|---|---|---|
| `menu.refresh` | `{}` | 从 Python 重新生成菜单结构并返回 `{ menus }`，同时标记 `menus` 状态更新 |
| `layer.applyStructure` | `{ view, rows, movedLayer }` | 按拖动后的树形行序列重排图层与图层组（含父层携带跟随子层），返回 `{ landedOn, selected }` |
| `timeline.thumbnails` | `{ frames?, width, height, view? }` | 用 `renderFrameThumbnail` 渲染指定帧缩略图，新渲染的通过 `image` 帧下发，已缓存的不重发（连接时补发过），返回 `{ view, width, height, revision, thumbnails, frames }`。缓存作废时 `thumbnails` 状态的 `revision` 加一，网页据此重新请求屏幕上的帧 |
| `file.import` | `{ kind, paths, view?, probe?, fitToCanvas? }` | 应用 File 菜单的导入（未传 `view`）进主画板，`view: "child"` 或 `kind` 带 `texture.` 前缀时进纹理板，不是探测时先把目标板显示出来；矢量导入支持 `probe: true` 先返回 `{ needsScale, sourceBounds, canvasWidth, canvasHeight }` |
| `project.save`、`project.saveAs`、`file.texture` | `{ path, probe?, overwrite? }`；`file.texture` 另带 `kind`（`open`、`save`、`saveAs`、`exportImage`） | 保存类调用支持 `probe: true` 检查后缀补全改写后的目标文件是否已存在，返回 `{ needsConfirmOverwrite, fileName, path }` |
| `menu.trigger` | `{ id, title?, checked? }` | 菜单项都回到 Python。Python 带 `id` 声明的项（`file.open`、`edit.undo`、`windows.timeline` 等），作为声明它的菜单的 `menu` 事件交回，`checked` 给了才带上，不给表示切换；没有处理器置 `handled` 时返回错误。File 项的选择框、覆盖确认、出错提示由 `pyfile/file_menu.py` 决定，它再调上面这些文件方法。`script:<host>:<菜单>:<项>` 交给那个脚本菜单；`settings:<名字>` 请 Python 开设置窗；别的 id 返回 `Unknown menu id` |
| `windows.setVisible` | `{ name, visible? }` | 显示或隐藏一个窗口（面板名或 `timeline`），不给 `visible` 就切换。Windows 菜单（Python）和网页的面板关闭钮、Timeline 的关闭钮和小圆钮都走它。返回 `{ name, visible }` |
| `windows.showTextureView` | `{}` | Windows ▸ Texture View：让纹理子控件框显示，发 `textureSubControl.surface` 事件 |
| `texture.setChangeable` | `{ timeline?, texture? }` | 纹理板的 Changable Timeline / Changable Texture，返回 `textureSettings` |
| `texture.setBackground` | `{ mode }`（`white`、`black`、`transparent`） | 纹理板的背景，返回 `textureSettings` |
| `dialog.result` | `{ id, action, value? }`，`action` 是 `button`、`accept`、`cancel`、`closed`、`error` | 一个对话框的回答，转成 Python 的 `dialog` 事件。兜底：`closed`（窗口被关）、`error`（页面出错）、Python 处理时抛错或没有接（没置 `message["handled"]`），C++ 都从 `dialogs` 里删掉这一条并记下 id，Python 之后再发旧栈也带不回来 |
| `board.effect` | `{ view, kind, duration, center?, radius?, clip?, path?, width?, ... }`，几何是画布坐标，路径是 `S` 操作的扁平格式 | 后端按该画板的缩放平移把几何换成画板 CSS 像素，其余字段原样，发 `event`（`board.effect`）；`kind` 后端不认识，由 `pyfile/board_effects.py` 定、`webui/src/board/effects.ts` 画，见 `painting_tools.md`「画板动画」 |
| `dialog.request` | `{ kind, ... }`，如颜色控件的 `{ kind: "color", hook, name, value, menu? }`（`menu` 是设置窗的名字，选好的颜色发回哪个方法由 Python 定） | 网页请 Python 开一个对话框，转成 `dialog` 事件（`action` 为 `request`）；Python 没有开（没置 `message["handled"]`）时返回错误 |

Qt 版打开时先弹 New Project 对话框。现在由 `pyfile/file_menu.py` 在启动后的第一轮事件循环里开出 Canvas Size（`ui.call_later(0, …)`），它留在 `dialogs` 状态里，网页连上就显示，网页重载也不会再问一次。后端那时若已经有打开的工程（`currentFilePath` 不空），不问；问题还挂着时有工程打开了（网页连上之前先 `project.open`），Python 在历史重置后的下一轮事件循环里撤回这个问题，留着那个工程。
