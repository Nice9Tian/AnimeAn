// 标准文字控件。
export default function create(el, { props }) {
  const label = document.createElement("div");
  label.className = "vf-std-label";
  el.append(label);

  function update(next) {
    label.textContent = String(next.text ?? "");
    label.dataset.tone = String(next.tone ?? "normal");
    label.dataset.align = String(next.align ?? "left");
  }
  update(props);

  return { update, destroy: () => label.remove() };
}
