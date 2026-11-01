// 设计工具里的占位。真实实现在 webui/src/ui，由 main.ts 换上。
export default function create(el) {
  el.textContent = "Python 调试面板";
  return { update() {}, destroy() { el.textContent = ""; } };
}
