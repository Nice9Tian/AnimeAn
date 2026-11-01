// 标准多行文本编辑框控件。
export default function create(el, { props, emit }) {
  const area = document.createElement("textarea");
  area.className = "vf-std-textarea";
  area.addEventListener("input", () => emit("change", area.value));
  el.append(area);

  function update(next) {
    area.placeholder = String(next.placeholder ?? "");
    area.readOnly = Boolean(next.readOnly);
    area.disabled = next.enabled === false;
    const val = String(next.value ?? "");
    if (area.value !== val) area.value = val;
  }
  update(props);

  return { update, destroy: () => area.remove() };
}
