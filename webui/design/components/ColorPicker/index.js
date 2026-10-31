// 自绘调色盘控件：画面由 effects/ColorPickerDraw 负责绘制，控件本身只提供容器。
export default function create(el) {
  el.classList.add("vf-color-picker-host");
  return {
    update() {},
    destroy() {
      el.classList.remove("vf-color-picker-host");
    },
  };
}
