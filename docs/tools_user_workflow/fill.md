# 填充（fill）

Tools ▸ Painting 的 **Fill**。封闭区域里点一下填色，填充落在专门的填充层上。

## 在哪

- Painting 页图标 **Fill**。
- Options 面板：**Color**（调色板控件）、**Fill Scope**（ALL / Current）。
- Layers 面板右键：**New Fill Layer**；填充层的行右键：**To Independent Layer**。

## 怎么用

1. 选颜色，在封闭区域里点一下，区域被填上。**Fill Scope** 决定用哪些层的线找边界：Current 只看当前层，ALL 看所有可见层。
2. 当前层是线稿层时，第一次填充自动新建一个填充层挂在它下面（Layers 里显示为它的子项），并**跟随**这条线稿层：线稿改了（画线、擦除、删线、撤销），填充按原来点下的位置重新算区域。之后当前层回到线稿层，继续画线不用切层。
3. 在已有的填充区域里再点一下：换颜色 / 更新它，不叠一层。
4. 右键填充层 ▸ **To Independent Layer**：解除跟随，变成独立填充层。独立填充层上 Pen 是区域刷、Eraser 擦区域，Connect 被锁。
5. 右键 ▸ **New Fill Layer** 直接建一个独立填充层。

## 会看到什么

- 每次填充一条历史记录；跟随重算不产生额外记录。
- 跟随线稿的填充层上 Pen / Eraser / Connect / Transfer 图标变暗，只留 Arrow 和 Fill。

## 边界

- 填充层不能给自己当边界：在填充层上填充时 Current 自动升级成 ALL。
- Fill Scope 是 **ALL** 时，Auto Mapping 输出的花纹笔画也算边界：给衣服填底色，颜色不会灌进封闭的花纹（比如一朵向日葵）里。不想让花纹参与，就用 **Current**，只按当前线稿层找边界（默认）。
- Mapping Area 用同一套区域探测，但它不是填充，不进图层；它总是忽略 Auto Mapping 的输出，这样重跑时上一次的花纹不会把区域切碎。

机制（三级）：`../mechanism/fill_layers.md`。
