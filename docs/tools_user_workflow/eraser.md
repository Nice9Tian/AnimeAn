# 橡皮（eraser）

Tools ▸ Painting 的 **Eraser**，工具提示 "Erase, delete line, cut line"。一个图标，三种模式。

## 在哪

- Painting 页图标 **Eraser**。
- Options 面板：**Eraser Mode**（AreaMode / LineMode / CutMode）。

## 怎么用

1. **AreaMode**：按住拖动，笔刷范围里的线段被擦掉；一条线可以被擦成几段。
2. **LineMode**（Delete Line）：点一条线，整条删除。
3. **CutMode**（Cut Line）：点一条线上的某处，删掉它与两侧相邻线交点之间的那一段；交点保留在两条线上，两条线在那里各自有了端点。
4. 在**独立填充层**上，橡皮擦的是填充区域：刷过的区域被去掉。

## 会看到什么

- 三种模式各自一条历史记录（Erase / Delete Line / Cut Line）。
- 跟随线稿的填充层在擦除后自动重算它的区域。

## 边界

- 跟随线稿的填充层上橡皮被锁。
- 擦除半径是画布像素，放大后视觉半径变大。

机制（三级）：`../mechanism/stroke_fitting.md`。
