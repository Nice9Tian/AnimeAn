// 标准下拉框。
export default function create(el, { props, emit }) {
  const select = document.createElement("select");
  select.className = "vf-std-combobox";
  let lastOptions = "";

  select.addEventListener("change", () => emit("change", select.value));
  el.append(select);

  function update(next) {
    const rawOptions = String(next.options ?? "");
    if (rawOptions !== lastOptions) {
      lastOptions = rawOptions;
      select.replaceChildren();
      for (const item of rawOptions.split(",").map((s) => s.trim()).filter(Boolean)) {
        const opt = document.createElement("option");
        opt.value = item;
        opt.textContent = item;
        select.append(opt);
      }
    }
    select.disabled = next.enabled === false;
    const val = String(next.value ?? "");
    if (val && select.value !== val) select.value = val;
  }
  update(props);

  return { update, destroy: () => select.remove() };
}
