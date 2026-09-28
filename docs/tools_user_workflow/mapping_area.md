# Mapping Area（mapping_area）

Tools ▸ Mapping 的 **Mapping Area**（浅蓝）。像油漆桶一样点一下，圈出 Auto Mapping 的范围。

## 在哪

- Mapping 页按钮 **Mapping Area**。
- Options 面板：Mapping 页所有工具共用的运行选项块，最后一行嵌着纹理板。

## 怎么用

1. 在一个封闭形状里点一下，以所有可见图层的线为边界探出区域，显示为半透明浅蓝。
2. 画在**主画板**上：Auto Mapping 的输出被精确裁到区域里（笔画在边界处切断），相当于"把图案填进这个形状"。
3. 画在**纹理板**上：只有区域里的图案参与 Auto Mapping。
4. 两块板可以各有一个；再点一次替换旧的；右上角 × 删除。

## 会看到什么

- 区域随轴线拖动、撤销、重跑自动刷新。

## 边界

- 每块板一个区域。
- Auto Mapping 输出的笔画不参与区域探测，上一次的图案不会把区域切碎。
- 单元模式下区域属于有焦点的单元，随单元配置保存。

规格（一级）：`../auto_mapping/auto_mapping_2_spec.md` 第 7 节（区域裁剪）。
