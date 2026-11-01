// 标准复选框。
export default function create(el, { props, emit }) {
  const label = document.createElement("label");
  label.className = "vf-std-checkbox";
  const box = document.createElement("input");
  box.type = "checkbox";
  const text = document.createElement("span");
  label.append(box, text);
  el.append(label);

  box.addEventListener("change", () => emit("change", box.checked));

  function update(next) {
    box.checked = Boolean(next.value);
    box.disabled = next.enabled === false;
    text.textContent = String(next.text ?? "");
    label.dataset.disabled = String(next.enabled === false);
  }
  update(props);

  return { update, destroy: () => label.remove() };
}
