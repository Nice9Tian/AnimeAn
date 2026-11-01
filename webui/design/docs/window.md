# AnimeAn 主窗口

Tools 面板的控件都在这个设计项目里：节点在 `ui/scene.json`，逻辑在 `logic/app.logic.yaml`，样式在 `components/`。Python 注册的工具要在这里各有一个节点才会显示，节点名和工具名的对应写在逻辑里。

Painting 页是 `Flow` 排版，按钮固定 44×44，面板变宽变窄时按整列增减。停靠区宽度吸附到整列，规则写在 `toolsDock` 的 `widthStep` 和 `widthBase` 上（n 列是 50n + 10），网页界面和编辑器都按它走。

其余面板的实现在 `webui/src/ui`，这里只定排版和停靠。
