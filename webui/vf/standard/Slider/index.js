// 标准滑条控件。
export default function create(el, { props, emit }) {
  const wrap = document.createElement("div");
  wrap.className = "vf-std-slider";
  const input = document.createElement("input");
  input.type = "range";
  input.className = "vf-std-slider-input";
  wrap.append(input);
  el.append(wrap);

  input.addEventListener("input", () => emit("change", Number(input.value)));

  function update(next) {
    const min = Number(next.min ?? 0);
    const max = Number(next.max ?? 100);
    const step = Number(next.step ?? 1);
    const val = Number(next.value ?? 0);
    input.min = String(min);
    input.max = String(max);
    input.step = String(step > 0 ? step : 1);
    if (Number(input.value) !== val) input.value = String(val);
    input.disabled = next.enabled === false;
    wrap.dataset.orientation = String(next.orientation ?? "horizontal");
  }
  update(props);

  return { update, destroy: () => wrap.remove() };
}
