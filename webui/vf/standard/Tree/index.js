// 标准树控件。支持层级缩进、分组折叠、勾选框、选中行和拖动重排。
function normalizeItems(rawItems) {
  if (!Array.isArray(rawItems)) return [];
  const flat = [];
  const walk = (list, depthOffset) => {
    for (let i = 0; i < list.length; i++) {
      const item = list[i];
      if (typeof item !== "object" || item === null) {
        flat.push({
          index: flat.length,
          id: String(flat.length),
          label: String(item),
          kind: "",
          isGroup: false,
          depth: depthOffset,
          collapsed: false,
          checked: true,
        });
        continue;
      }
      const hasChildren = Array.isArray(item.children) && item.children.length > 0;
      const depth = typeof item.depth === "number" ? item.depth : depthOffset;
      const idx = flat.length;
      flat.push({
        index: idx,
        id: String(item.id ?? item.name ?? idx),
        label: String(item.label ?? item.name ?? item.text ?? idx),
        kind: String(item.kind ?? ""),
        isGroup: Boolean(item.isGroup || hasChildren),
        depth,
        collapsed: Boolean(item.collapsed),
        checked: item.checked !== undefined ? Boolean(item.checked) : item.visible !== undefined ? Boolean(item.visible) : true,
      });
      if (hasChildren) walk(item.children, depth + 1);
    }
  };
  walk(rawItems, 0);
  return flat;
}

export default function create(el, { props, emit }) {
  const container = document.createElement("div");
  container.className = "vf-std-tree";
  el.append(container);
  let dragFrom = -1;

  function update(next) {
    container.replaceChildren();
    const disabled = next.enabled === false;
    container.dataset.disabled = String(disabled);
    const rows = normalizeItems(next.items);
    const selected = typeof next.value === "number" ? next.value : Number(next.selectedIndex ?? 0);
    const checkable = next.checkable !== false;
    const draggable = next.draggable !== false;
    if (rows.length === 0) {
      const empty = document.createElement("div");
      empty.className = "vf-std-tree-empty";
      empty.textContent = String(next.emptyText ?? "暂无内容");
      container.append(empty);
      return;
    }
    let collapsedDepth = null;
    for (const r of rows) {
      if (collapsedDepth !== null) {
        if (r.depth > collapsedDepth) continue;
        collapsedDepth = null;
      }
      if (r.isGroup && r.collapsed) collapsedDepth = r.depth;
      const row = document.createElement("div");
      row.dataset.vfRow = String(r.index);
      row.dataset.vfItemId = r.id;
      row.className = "vf-std-tree-row" + (r.index === selected ? " selected" : "") + (!r.checked ? " unchecked" : "");
      row.style.paddingLeft = `${6 + r.depth * 14}px`;

      const caret = document.createElement("span");
      caret.className = "vf-std-tree-caret";
      if (r.isGroup) {
        caret.dataset.vfAction = "collapse";
        caret.textContent = r.collapsed ? "▸" : "▾";
        caret.addEventListener("click", (ev) => {
          ev.stopPropagation();
          if (disabled) return;
          emit("toggleCollapse", { index: r.index, id: r.id, collapsed: !r.collapsed });
        });
      }
      row.append(caret);

      if (checkable) {
        const chk = document.createElement("input");
        chk.type = "checkbox";
        chk.dataset.vfAction = "check";
        chk.className = "vf-std-tree-check";
        chk.checked = r.checked;
        chk.addEventListener("click", (ev) => ev.stopPropagation());
        chk.addEventListener("change", (ev) => {
          ev.stopPropagation();
          if (disabled) return;
          emit("toggleCheck", { index: r.index, id: r.id, checked: chk.checked });
        });
        row.append(chk);
      }

      const label = document.createElement("span");
      label.className = "vf-std-tree-text" + (r.isGroup ? " group" : "");
      label.textContent = r.label;
      row.append(label);

      if (r.kind) {
        const badge = document.createElement("span");
        badge.className = "vf-std-tree-badge";
        badge.textContent = r.kind;
        row.append(badge);
      }

      row.addEventListener("click", () => {
        if (disabled) return;
        if (r.index !== selected) emit("change", r.index);
        emit("select", r.index);
      });

      if (draggable) {
        row.draggable = true;
        row.addEventListener("dragstart", (ev) => {
          dragFrom = r.index;
          if (ev.dataTransfer) {
            ev.dataTransfer.effectAllowed = "move";
            ev.dataTransfer.setData("text/plain", String(r.index));
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
          if (from >= 0 && from !== r.index) {
            emit("reorder", { from, to: r.index });
          }
        });
      }
      container.append(row);
    }
  }
  update(props);

  return { update, destroy: () => container.remove() };
}
