// 标准单行输入框。
export default function create(el, { props, emit }) {
  const input = document.createElement("input");
  input.type = "text";
  input.className = "vf-std-input";
  input.addEventListener("input", () => emit("change", input.value));
  el.append(input);

  function update(next) {
    input.placeholder = String(next.placeholder ?? "");
    input.disabled = next.enabled === false;
    const value = String(next.value ?? "");
    if (input.value !== value) input.value = value;
  }
  update(props);

  return { update, destroy: () => input.remove() };
}
