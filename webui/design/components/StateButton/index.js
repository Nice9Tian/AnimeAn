// 状态图片按钮：由工具内置的 mountStateImageControl 按 component.json 的 stateImages 自动挂载 5 状态九宫格 SVG 与文字层。
export default function create(el, { props }) {
  el.dataset.text = props.text ?? "";
  return {
    update(next) {
      el.dataset.text = next.text ?? "";
    },
    destroy() {
      delete el.dataset.text;
    },
  };
}
