// 标准页签栏控件。
export default function create(el, { props, emit }) {
  const bar = document.createElement("div");
  bar.className = "vf-std-tabbar";
  el.append(bar);

  function parseTabs(raw) {
    if (Array.isArray(raw)) {
      return raw.map((it, i) => (typeof it === "object" && it !== null ? String(it.label ?? it.title ?? it.name ?? i) : String(it)));
    }
    return String(raw ?? "")
      .split(",")
      .map((s) => s.trim())
      .filter(Boolean);
  }

  function update(next) {
    bar.replaceChildren();
    const tabs = parseTabs(next.tabs);
    const cur = Number(next.value ?? 0);
    const disabled = next.enabled === false;
    tabs.forEach((label, idx) => {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.dataset.vfTab = String(idx);
      btn.className = "vf-std-tabbar-tab" + (idx === cur ? " active" : "");
      btn.textContent = label;
      btn.disabled = disabled;
      btn.addEventListener("click", () => {
        if (disabled) return;
        if (idx !== cur) emit("change", idx);
      });
      bar.append(btn);
    });
  }
  update(props);

  return { update, destroy: () => bar.remove() };
}
