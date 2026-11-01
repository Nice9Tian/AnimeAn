// 浮动嵌入框标准容器：支持嵌入父布局或浮出为窗口内可拖动面板。
export default function create(el, { props, emit, mode }) {
  const root = document.createElement("div");
  root.className = "vf-std-floatbox";
  const header = document.createElement("div");
  header.className = "vf-std-floatbox-header";
  const title = document.createElement("span");
  title.className = "vf-std-floatbox-title";
  const toggleBtn = document.createElement("button");
  toggleBtn.type = "button";
  toggleBtn.className = "vf-std-floatbox-toggle";
  header.append(title, toggleBtn);
  const body = document.createElement("div");
  body.className = "vf-std-floatbox-body";
  root.append(header, body);
  el.append(root);

  let current = props;

  toggleBtn.addEventListener("click", (e) => {
    e.stopPropagation();
    if (current.enabled === false) return;
    const next = !Boolean(current.floating);
    emit("toggle", next);
    emit("change", next);
  });

  header.addEventListener("pointerdown", (e) => {
    if (mode !== "preview" || current.enabled === false || !Boolean(current.floating)) return;
    if (e.button !== 0 || e.target === toggleBtn) return;
    e.stopPropagation();
    const ownerDoc = root.ownerDocument || document;
    const ownerWin = ownerDoc.defaultView || window;
    const frameEl = ownerWin !== window ? ownerWin.frameElement : null;
    const rect = root.getBoundingClientRect();
    const innerScale = rect.width > 0 && root.offsetWidth > 0 ? rect.width / root.offsetWidth : 1;
    const frameRect = frameEl ? frameEl.getBoundingClientRect() : null;
    const outerScale =
      frameEl && frameRect && frameEl.offsetWidth > 0 && frameRect.width > 0
        ? frameRect.width / frameEl.offsetWidth
        : 1;
    const hdrRect = header.getBoundingClientRect();
    const inInner =
      e.clientX >= hdrRect.left - 4 &&
      e.clientX <= hdrRect.right + 4 &&
      e.clientY >= hdrRect.top - 4 &&
      e.clientY <= hdrRect.bottom + 4;
    const useOuter =
      Boolean(frameEl && frameRect) &&
      (e.view === window ||
        (!inInner &&
          (Math.abs(outerScale - 1) > 0.01 || Math.abs(frameRect.left) > 1 || Math.abs(frameRect.top) > 1)));
    const toStageXY = (pe, fromOuter = false) => {
      let cx = pe.clientX;
      let cy = pe.clientY;
      if (frameEl && (useOuter || fromOuter || pe.view === window)) {
        const fr = frameEl.getBoundingClientRect();
        const os = frameEl.offsetWidth > 0 && fr.width > 0 ? fr.width / frameEl.offsetWidth : 1;
        cx = (cx - fr.left) / os;
        cy = (cy - fr.top) / os;
      }
      return { x: cx / innerScale, y: cy / innerScale };
    };
    const startPt = toStageXY(e, false);
    const baseX = Number(current.floatX ?? 60);
    const baseY = Number(current.floatY ?? 80);
    const onMove = (me, fromOuter = false) => {
      const pt = toStageXY(me, fromOuter);
      const dx = Math.round(pt.x - startPt.x);
      const dy = Math.round(pt.y - startPt.y);
      emit("move", { x: Math.max(0, baseX + dx), y: Math.max(0, baseY + dy) });
    };
    const moveInner = (me) => onMove(me, false);
    const moveOuter = (me) => onMove(me, true);
    const onUp = () => {
      ownerWin.removeEventListener("pointermove", moveInner);
      ownerWin.removeEventListener("pointerup", onUp);
      if (ownerWin !== window) {
        window.removeEventListener("pointermove", moveOuter);
        window.removeEventListener("pointerup", onUp);
      }
    };
    ownerWin.addEventListener("pointermove", moveInner);
    ownerWin.addEventListener("pointerup", onUp);
    if (ownerWin !== window) {
      window.addEventListener("pointermove", moveOuter);
      window.addEventListener("pointerup", onUp);
    }
  });

  function update(next) {
    current = next;
    const floating = Boolean(next.floating);
    const hdrH = Number(next.headerHeight ?? 24);
    header.style.height = `${hdrH}px`;
    root.dataset.floating = String(floating);
    root.dataset.disabled = String(next.enabled === false);
    title.textContent = String(next.title ?? "浮动嵌入框");
    toggleBtn.textContent = floating ? "嵌入" : "浮动";
    toggleBtn.title = floating ? "嵌入回原布局" : "脱离布局浮动";
    toggleBtn.disabled = next.enabled === false;
  }
  update(props);

  return { update, destroy: () => root.remove() };
}
