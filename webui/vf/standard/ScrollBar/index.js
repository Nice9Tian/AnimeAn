// 标准滚动条控件。
export default function create(el, { props, emit }) {
  const track = document.createElement("div");
  track.className = "vf-std-scrollbar";
  const thumb = document.createElement("div");
  thumb.className = "vf-std-scrollbar-thumb";
  track.append(thumb);
  el.append(track);
  let current = props;

  function update(next) {
    current = next;
    const min = Number(next.min ?? 0);
    const max = Math.max(min, Number(next.max ?? 100));
    const page = Math.max(1, Number(next.pageStep ?? 10));
    const val = Math.min(max, Math.max(min, Number(next.value ?? 0)));
    const range = Math.max(1, max - min);
    const ratio = Math.min(0.85, Math.max(0.15, page / (range + page)));
    const posRatio = max > min ? (val - min) / (max - min) : 0;
    const vertical = next.orientation === "vertical";
    track.dataset.orientation = vertical ? "vertical" : "horizontal";
    track.dataset.disabled = String(next.enabled === false);
    if (vertical) {
      thumb.style.left = "1px";
      thumb.style.right = "1px";
      thumb.style.width = "auto";
      thumb.style.height = `${ratio * 100}%`;
      thumb.style.top = `${(1 - ratio) * posRatio * 100}%`;
    } else {
      thumb.style.top = "1px";
      thumb.style.bottom = "1px";
      thumb.style.height = "auto";
      thumb.style.width = `${ratio * 100}%`;
      thumb.style.left = `${(1 - ratio) * posRatio * 100}%`;
    }
  }

  function pick(clientX, clientY) {
    if (current.enabled === false) return;
    const rect = track.getBoundingClientRect();
    const min = Number(current.min ?? 0);
    const max = Math.max(min, Number(current.max ?? 100));
    const step = Math.max(1, Number(current.step ?? 1));
    const frac =
      current.orientation === "vertical"
        ? (clientY - rect.top) / Math.max(1, rect.height)
        : (clientX - rect.left) / Math.max(1, rect.width);
    const clamped = Math.min(1, Math.max(0, frac));
    const snapped = Math.min(max, Math.max(min, Math.round((min + clamped * (max - min)) / step) * step));
    emit("change", snapped);
  }

  track.addEventListener("mousedown", (ev) => {
    pick(ev.clientX, ev.clientY);
  });

  update(props);
  return { update, destroy: () => track.remove() };
}
