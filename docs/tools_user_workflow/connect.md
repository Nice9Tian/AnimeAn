# 连接（connect）

Tools ▸ Painting 的 **Connect**。点两个点，两点之间生成一条新笔画。

## 在哪

- Painting 页图标 **Connect**。
- Options 面板：**Auto Snap**、**Connect Mode**（PolyMode / CurveMode / SmoothMode）、**Smooth**（只在 SmoothMode 显示）、**Auto Accept**。

## 怎么用

1. 移动光标：**Auto Snap** 开着时，光标附近的候选点被提示出来——优先端点和拐角，其次曲率顶点，再其次最近的顶点；关掉 Auto Snap 就取光标的精确位置。
2. 点第一个点，再点第二个点，两点之间生成一条新笔画，落在当前层。**PolyMode** 是直线；**CurveMode** 顺着两端的切向弯过去，离开两条线时都相切；**SmoothMode** 用 Smooth 滑条在两者之间调，一端等于 CurveMode，另一端等于直线。
3. 新笔画保持"聚焦"：改 Options 它就地重算；画布上出现**接受 / 删除**两个按钮，点接受留下，点删除去掉；切换工具视为接受。
4. **Auto Accept** 开着：没有按钮，直接画下一条，焦点自动移过去。

## 会看到什么

- 悬停时的吸附提示把手；每条连接一条历史记录。

## 边界

- 两端切向平行或背离时退回直线；接近平行时弯曲程度有上限，不会飘出去。
- 撤销 / 重做后聚焦丢失，不能再调上一条。
- 跟随线稿的填充层和独立填充层上 Connect 都被锁（填充没有端点可连）。

机制（三级）：`../mechanism/painting_tools.md`。
