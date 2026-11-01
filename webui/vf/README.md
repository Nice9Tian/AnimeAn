# VibeFrontend 运行时核心副本 (`webui/vf/`)

## 来源信息

- 来源仓库：`C:/Users/admin/Documents/VibeFrontend`
- 来源提交号：`00ff26b` (`00ff26b873f1a7a13afd7ce255d0c2d39d621405`)

## 复制的文件清单

- `app/src/runtime.ts` → `webui/vf/src/runtime.ts`
- `app/src/model.ts` → `webui/vf/src/model.ts`
- `app/src/expr.ts` → `webui/vf/src/expr.ts`
- `app/src/dock.ts` → `webui/vf/src/dock.ts`
- `app/src/shapes.ts` → `webui/vf/src/shapes.ts`
- `app/src/loader.ts` → `webui/vf/src/loader.ts`
- `app/src/check.ts` → `webui/vf/src/check.ts`
- `app/src/md5.ts` → `webui/vf/src/md5.ts`
- `app/src/structhash.ts` → `webui/vf/src/structhash.ts`
- `app/src/paint.ts` → `webui/vf/src/paint.ts`
- `app/src/transform.ts` → `webui/vf/src/transform.ts`
- `app/src/project.ts` → `webui/vf/src/project.ts`
- `app/standard/` → `webui/vf/standard/`

## 相对上游版本的改动说明

其余文件均保持与上游 `00ff26b` 原样一致，仅对以下三处作了必要修改：

1. **`webui/vf/src/runtime.ts`（增量刷新与依赖缓存）**
   - 在 `reset()` 中建立静态场景索引（`menuItemById`、`parentById`、`windowById`、`stackParentById`、`dependentNodesByNode`），避免每次 `computeView(id)` 全量遍历场景树与菜单项。
   - 为 `view(id)` 增加按状态路径与条件路径追踪的持久缓存（`viewCache`、`depsByPath`、`depsByNode`）：在 `resolve()` 与 `object.bind` 读取时记录当前控件依赖的状态路径与条件代号；当状态、条件、窗口、Dock、FloatBox、菜单勾选或指针悬停/按下变化时，仅失效受影响节点的 `viewCache`（并通过 `dependentNodesByNode` 级联失效关联的 Stack 子页与 Dock 菜单项）。
   - 增加增量排版缓存（`computeLayout(rootSize)`、`layoutDirty`、`cachedLayoutBoxes`）：比对控件前后 `NodeView` 的尺寸、可见性、文字（`text`/`label`/`title`）、Stack 页码及 FloatBox 几何属性，仅在这些属性、窗口尺寸或停靠布局发生变化时重算 `computeSceneLayout`。
   - 新增 `consumeDirtyNodes()`、`computeLayout(rootSize)`、`refresh(rootSize)` 与 `invalidateAll()` 供播放器与增量刷新测试调用。
   - 优化 `syncActiveTabsWithVisibility()`，仅在需要切换激活页签时才克隆 `DockLayoutSpec`。

2. **`webui/vf/src/project.ts`（解耦编辑器专属 `./fs`）**
   - 移除对编辑器本地文件模块 `./fs` 的导入，改为由 `loadProject(io: ProjectIO, previous)` 接收调用方提供的 `ProjectIO`。
   - `importSource` 在无 `document` 环境（如 Node 测试）下回退使用 `data:text/javascript` URL 导入。
   - 导出 `importSource`、`applyComponentStylesToDoc`、`injectComponentStyles`。

3. **`webui/vf/src/dock.ts`、`model.ts`、`check.ts`（停靠面板的 `widthStep`、`widthBase`）**
   - 从 VibeFrontend 的提交 `6ebf46e`（分支 `work/editor-interaction`）同步：左右停靠区的宽度吸附到 `widthBase + n × widthStep`。这三个文件以那边为准。
