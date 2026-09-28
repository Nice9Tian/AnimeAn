# 工程文件与导入（project_files）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。代码：`projectio.*`、`clipreader.*`、`opentoonz_tools/toonz_to_dict.py`、`mainwindow.cpp`（导入导出）。2026-09-28 从架构文档下沉。

## 数据放哪

| 位置 | 内容 | 说明 |
|---|---|---|
| `*.anproj`（JSON） | 主画板与纹理板两个场景：列（类型 vector / fill / raster、父层、可见性、锁定）、帧与帧名（只在设置过时写出）、资产、单元格、笔画（点列 + 含三次段的路径 + 属性 + 线型）、填充区域（含属性）、栅格、图层组与 tag、scriptData | 用户自选位置；历史不入文件 |
| `*.textureview`（JSON） | 单个场景（纹理板） | 跨工程复用图案 |
| QSettings（`AnimeAn` / `AnimeAn`） | 主题模式；时间轴的停靠区、浮动、折叠、可见与浮动窗几何 | 机器级，注册表 |
| 系统临时目录 | To 3D 导出的 HTML 查看器 | 每次导出一个新文件 |
| 会话内存 | 历史快照、播放缓存、单元焦点、斥力板基线 | 不落盘 |
| `test_document/` | 样例：`A.pli`、`bigA.clip`、`texture.textureview` | 入库 |

没有用户数据目录：除 QSettings 外程序不落盘（2026-09-28 核实：`QSettings` 只出现在 `theme.cpp` 与 `timelinewindow.cpp`）。

## 文件归属与身份

- File 菜单的 Save / Save As 永远存完整 `.anproj`（两块板）；Open 一起替换两块板并重置两份历史。
- 纹理板的 File 菜单（也复制在应用 File ▸ Texture 下）读写 `.textureview`，只替换纹理板。
- 两块板的场景身份固定（main / child）；打开文件后无条件重新施加，避免两个场景在绑定里撞名。
- 路径里的三次段原样往返，不展平。发布前的 `.animean` 旧格式不再支持。

## 导入

| 命令 | 格式 | 做什么 |
|---|---|---|
| Import ▸ Raster… | 图片 | 进目标板成为栅格列 |
| Import ▸ OpenToonz Lines… | `.tnz` / `.pli` | `toonz_to_dict.py` 解 TOStream 与矢量 level，只取线条（二次贝塞尔链），不解 `.tlv` 栅格 |
| Import ▸ Clip Studio Paint… | `.clip` | `clipreader` 走 CSFCHUNK 容器，从外部块里认出矢量笔画链，不依赖 SQLite，不读栅格 |

应用 File 菜单的导入永远进主画板；纹理板菜单的导入进纹理板，并先把纹理板显示出来。导出 Texture View Image 抓取纹理板的帧缓冲，导出前清掉活动板描边。
