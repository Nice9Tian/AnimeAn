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

连接地址：`ws://127.0.0.1:<端口>/?token=<随机串>`。token 不对就拒绝。`Origin` 只放行 `tauri://localhost`、`http://tauri.localhost`、`https://tauri.localhost` 和 `http://localhost:<任意端口>`；没有 `Origin` 头的连接放行（测试用）。同一时间只服务一个连接，第二个连接到来时断开旧的。

## 画板不改逻辑

`PaintOpenGLWidget`（`openglwidget.*`）的画板业务代码保持原样，基类由 `boardbase.h` 定为 `BoardSurface`（`using PaintBoardBase = BoardSurface;`）：

- `BoardSurface`（`backend/board/boardsurface.*`）继承 `AnimeCore::PaintDevice`，持有 `RecordingDevice`，提供 `update()`、`width()`、`height()`、`size()`、`rect()`、`setCursor()`、`unsetCursor()`、`setMouseTracking()`、`setFocusPolicy()`、`devicePixelRatioF()` 和全部事件虚函数。
- `Painter painter(this)` 在 `BoardSurface` 上绘制：`BoardSurface` 是 `PaintDevice`，把绘制调用转给内部的 `RecordingDevice`，录制成带 64 位哈希的指令表。`update()` 合并成一次 0 毫秒定时（`Timer`），定时到了调用 `paintGL()`。
- 鼠标、滚轮、焦点事件由 `BoardSurface::injectMouse`、`injectWheel`、`injectFocus`、`injectBlur` 等构造 `MouseEvent`、`WheelEvent`（`backend/board/inputevent.h`），直接调对应的虚函数；尺寸走 `resizeSurface`。派发给 Python 的钩子消息格式不变。

## 消息

文本帧都是一个 JSON 对象，`t` 字段是类型。二进制帧只用于图片。

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
```

- 连接建立后，后端先把全部 `state` 键发一遍。之后只发变了的键，值是该键的完整新值。
- `image` 文本帧之后紧跟一个二进制帧，内容是 RGBA8 非预乘像素，逐行从上到下。
- `image` 的 `view` 说明图是哪块画板画的，只有那块画板收下。光标和缩略图不带 `view`，归页面。
- 画板发过的图由后端记账，每块画板上限 512 MB。超了就从最久没画过的开始发 `image.forget`，网页收到就释放，当前帧画到的图不淘汰。之后再画到时重新发 `image` 帧。后端不留副本，新客户端连上时清零记账，下一帧重新发。

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
- 选择存在页面的 `localStorage`，键是 `animean.boardRenderer`，属于机器级偏好，不进工程文件，也不经过后端。菜单项由 `webui/src/ui/app.js` 加在后端的菜单后面。
- 切换时新建 canvas，因为同一个 canvas 只能拿到一种上下文。新渲染器用 `adopt` 接手指令表和图片，不需要后端重发。
- 显卡不支持 WebGL2 或上下文创建失败时退回 Canvas 2D。要读背景的合成模式（`multiply`、`screen` 等）WebGL 没有实现，退回 `source-over`；画板现在不发这些模式，唯一的 `setCompositionMode` 作用在离屏 `AnimeCore::Image`（`RasterDevice`）上。

## 状态键

| 键 | 内容 |
|---|---|
| `window` | 标题、状态栏文字、活动画板 `activeView`、素材面板目标画板 `assetView`、画布尺寸、工程与纹理路径、`pythonDebug` 日志（最多保留 2000 条） |
| `theme` | `AnimeTheme` 的模式和全部颜色 |
| `menus` | 菜单栏的完整结构，含 Python 返回的菜单 |
| `textureMenus` | 纹理板菜单栏（`File`、`Setting`、`View` 等）的完整结构，含 Python 注册的 `child` 菜单和脚本按钮行（`window.textureViewButtons`） |
| `tools` | 工具页、当前工具、被锁定的工具 |
| `toolOptions` | Python 返回的选项 JSON |
| `drawColor`、`drawSettings` | 当前颜色、绘制设置 |
| `layers/main`、`layers/child` | 图层树 |
| `assets/main`、`assets/child` | 素材列表 |
| `timeline` | 帧数、当前帧、播放状态、洋葱皮 |
| `thumbnails` | `{ revision }`，帧缩略图缓存的版本，画面变了就加一 |
| `history` | 历史记录 |
| `windows` | 面板的显示状态 |
| `cursor/main`、`cursor/child` | 光标：形状名，或者图片 `key` 加热点和 `scale`。位图是屏幕像素，`w`、`h` 也是；热点是逻辑像素，`scale` 是位图每个逻辑像素的像素数，网页用 `image-set` 声明 |
| `freeze` | Python 冻结界面的开关 |
| `pad` | Repulsion Pad 的值 |
| `viewport/main`、`viewport/child` | 滚动条：`h`、`v` 各含 `min`、`max`、`page`、`value`、`enabled`，由 `BackendSession::buildViewportState` 计算 |
| `event` | 一次性通知，`{ name, args }`，不合并、不重发：`dialog.*`、`menu.action`、`settings.open`、`window.show`、`window.select`、`textureView.surface`、`textureSubControl.surface` |

## 调用

方法名在 `backend/session/backendsession.cpp` 的 `dispatchCall`。图层右键走 `layer.contextMenu`（返回菜单项，含 Python 注册的项）和 `layer.contextAction`。纹理板搬迁时调 `texture.setHome`（`home`：`central`、`control`、`parked`，后端保存离开处的缩放与平移、恢复到达处视口并在到达 `control` 时自动适配），脚本按钮行调 `texture.viewButton`（`view`、`id`、`text`）。网页发 `input` 时带 `time`（毫秒），后端写进事件时间戳，稳定器要用它。

| 调用 | 参数 | 返回 / 说明 |
|---|---|---|
| `menu.refresh` | `{}` | 从 Python 重新生成菜单结构并返回 `{ menus }`，同时标记 `menus` 状态更新 |
| `layer.applyStructure` | `{ view, rows, movedLayer }` | 按拖动后的树形行序列重排图层与图层组（含父层携带跟随子层），返回 `{ landedOn, selected }` |
| `timeline.thumbnails` | `{ frames?, width, height, view? }` | 用 `renderFrameThumbnail` 渲染指定帧缩略图，新渲染的通过 `image` 帧下发，已缓存的不重发（连接时补发过），返回 `{ view, width, height, revision, thumbnails, frames }`。缓存作废时 `thumbnails` 状态的 `revision` 加一，网页据此重新请求屏幕上的帧 |
| `file.import` | `{ kind, paths, view?, probe?, fitToCanvas? }` | 未传 `view` 时按 `framePanelTargetViewName` 选择目标板；矢量导入支持 `probe: true` 先返回 `{ needsScale, sourceBounds, canvasWidth, canvasHeight }` |
| `project.save`、`project.saveAs`、`file.texture` | `{ path, probe?, overwrite? }` | 支持 `probe: true` 检查后缀补全改写后的目标文件是否已存在，返回 `{ needsConfirmOverwrite, fileName, path }` |

Qt 版打开时先弹 New Project 对话框，网页版同样在收到第一个 `window` 状态后弹出。
