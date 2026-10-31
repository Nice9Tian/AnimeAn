// 标准数字框。
export default function create(el, { props, emit }) {
  const wrap = document.createElement("div");
  wrap.className = "vf-std-spinbox";
  const input = document.createElement("input");
  input.type = "number";
  input.className = "vf-std-spinbox-input";
  const suffix = document.createElement("span");
  suffix.className = "vf-std-spinbox-suffix";
  wrap.append(input, suffix);
  el.append(wrap);

  let min = 0;
  let max = 9999;
  const clamp = (n) => Math.max(min, Math.min(max, n));

  input.addEventListener("input", () => {
    const n = Number(input.value);
    if (Number.isFinite(n)) {
      const clamped = clamp(n);
      if (input.value !== String(clamped)) input.value = String(clamped);
      emit("change", clamped);
    }
  });

  function update(next) {
    min = Number(next.min ?? 0);
    max = Number(next.max ?? 9999);
    input.min = String(min);
    input.max = String(max);
    input.step = String(next.step ?? 1);
    input.disabled = next.enabled === false;
    const raw = Number(next.value ?? 0);
    const val = String(Number.isFinite(raw) ? clamp(raw) : min);
    if (input.value !== val) input.value = val;
    const sfx = String(next.suffix ?? "");
    suffix.textContent = sfx;
    suffix.hidden = !sfx;
  }
  update(props);

  return { update, destroy: () => wrap.remove() };
}
