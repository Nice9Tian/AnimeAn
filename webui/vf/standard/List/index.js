// 标准列表控件。支持数组行渲染、选中行、置灰行和拖动重排。
export default function create(el, { props, emit }) {
  const container = document.createElement("div");
  container.className = "vf-std-list";
  el.append(container);
  let dragFrom = -1;

  function itemLabel(item) {
    if (item && typeof item === "object") {
      return String(item.name ?? item.label ?? item.title ?? item.text ?? item.id ?? "");
    }
    return String(item ?? "");
  }

  function update(next) {
    container.replaceChildren();
    const disabled = next.enabled === false;
    container.dataset.disabled = String(disabled);
    const items = Array.isArray(next.items) ? next.items : [];
    const selected = typeof next.value === "number" ? next.value : 0;
    const draggable = Boolean(next.draggable);
    const mutedFrom = typeof next.mutedFrom === "number" ? next.mutedFrom : -1;
    if (items.length === 0) {
      const empty = document.createElement("div");
      empty.className = "vf-std-list-empty";
      empty.textContent = String(next.emptyText ?? "暂无内容");
      container.append(empty);
      return;
    }
    items.forEach((item, index) => {
      const isObj = item && typeof item === "object" && !Array.isArray(item);
      const muted = (isObj && Boolean(item.muted)) || (mutedFrom >= 0 && index >= mutedFrom);
      const row = document.createElement("div");
      row.dataset.vfRow = String(index);
      row.className = "vf-std-list-row" + (index === selected ? " selected" : "") + (muted ? " muted" : "");
      const text = document.createElement("span");
      text.className = "vf-std-list-text";
      text.textContent = itemLabel(item);
      row.append(text);
      if (isObj && item.badge !== undefined && item.badge !== "") {
        const badge = document.createElement("span");
        badge.className = "vf-std-list-badge";
        badge.textContent = String(item.badge);
        row.append(badge);
      }
      row.addEventListener("click", () => {
        if (disabled) return;
        emit("select", index);
        if (index !== selected) {
          emit("change", index);
        }
      });
      if (draggable) {
        row.draggable = true;
        row.addEventListener("dragstart", (ev) => {
          dragFrom = index;
          if (ev.dataTransfer) {
            ev.dataTransfer.effectAllowed = "move";
            ev.dataTransfer.setData("text/plain", String(index));
          }
        });
        row.addEventListener("dragover", (ev) => {
          ev.preventDefault();
          if (ev.dataTransfer) ev.dataTransfer.dropEffect = "move";
        });
        row.addEventListener("drop", (ev) => {
          ev.preventDefault();
          if (disabled) return;
          const from = dragFrom >= 0 ? dragFrom : Number(ev.dataTransfer?.getData("text/plain") ?? -1);
          dragFrom = -1;
          if (from >= 0 && from !== index) {
            emit("reorder", { from, to: index });
          }
        });
      }
      container.append(row);
    });
  }
  update(props);

  return { update, destroy: () => container.remove() };
}
