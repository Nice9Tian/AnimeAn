// Tabs as the stylesheet in parentwindow.cpp draws them.
export default function create(el, { props, emit }) {
  const bar = document.createElement("div");
  bar.className = "qt-tabs";
  el.append(bar);
  let tabs = [];
  let shown = null;
  let current = 0;

  function build(next) {
    const widths = String(next.widths ?? "").split(",").map(Number);
    tabs = String(next.tabs ?? "").split(",").map((text, i) => {
      const tab = document.createElement("div");
      tab.className = "qt-tab";
      tab.textContent = text.trim();
      if (widths[i]) tab.style.width = `${widths[i]}px`;
      tab.addEventListener("click", () => { if (i !== current) emit("change", i); });
      return tab;
    });
    bar.replaceChildren(...tabs);
  }

  function update(next) {
    const key = `${next.tabs}|${next.widths}`;
    if (key !== shown) {
      shown = key;
      build(next);
    }
    current = Number(next.value ?? 0);
    tabs.forEach((tab, i) => tab.classList.toggle("sel", i === current));
  }
  update(props);

  return { update, destroy: () => bar.remove() };
}
