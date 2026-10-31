// 标准占位区控件。
export default function create(el) {
  const spacer = document.createElement("div");
  spacer.className = "vf-std-spacer";
  el.append(spacer);
  return { update() {}, destroy: () => spacer.remove() };
}
