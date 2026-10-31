// 标准按钮控件。支持普通点击、图标显示以及可勾选（checkable）切换。
const ICONS = {
  arrow: '<path d="M5 3l12 7-5 1.5L9.5 16 5 3z" fill="currentColor"/>',
  pen: '<path d="M14.5 2.5l3 3-9.5 9.5-3.5.5.5-3.5 9.5-9.5zm-8.5 11l-1 1" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  brush: '<path d="M14.5 2.5l3 3-8.5 8.5c-1.5 1.5-3.5 2-5 1.5.5-1.5 0-3.5 1.5-5l8.5-8.5z" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  eraser: '<path d="M11 4l6 6-7 7H5l-2-2 8-11zm-4 13h10" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  fill: '<path d="M9 3l6 6-6 6-6-6 6-6zm7 9c1.2 1.5 2 2.7 2 3.8a2 2 0 1 1-4 0c0-1.1.8-2.3 2-3.8z" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  bucket: '<path d="M9 3l6 6-6 6-6-6 6-6zm7 9c1.2 1.5 2 2.7 2 3.8a2 2 0 1 1-4 0c0-1.1.8-2.3 2-3.8z" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  shape: '<rect x="3.5" y="3.5" width="8.5" height="8.5" rx="1" stroke="currentColor" stroke-width="1.5" fill="none"/><circle cx="13" cy="13" r="4" stroke="currentColor" stroke-width="1.5" fill="none"/>',
  move: '<path d="M10 3v14M3 10h14M10 3l-2.5 2.5M10 3l2.5 2.5M10 17l-2.5-2.5M10 17l2.5-2.5M3 10l2.5-2.5M3 10l2.5 2.5M17 10l-2.5-2.5M17 10l-2.5 2.5" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  lasso: '<path d="M10 4c4 0 6.5 2.2 6.5 5s-2.5 5-6.5 5c-1.8 0-3.3-.4-4.5-1.2M6 13a2 2 0 1 0 0 4 2 2 0 0 0 0-4z" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
  eyedropper: '<path d="M13.5 3.5l3 3-2 2-3-3 2-2zm-3 3l3 3-6.5 6.5H4v-3l6.5-6.5z" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linejoin="round"/>',
  transfer: '<path d="M4 7h11m-3-3l3 3-3 3M16 13H5m3-3l-3 3 3 3" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  remap: '<path d="M4 7h11m-3-3l3 3-3 3M16 13H5m3-3l-3 3 3 3" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  connect: '<path d="M5 15c2-6 8-6 10-10M5 15a1.5 1.5 0 1 0 0 .1zm10-10a1.5 1.5 0 1 0 0 .1z" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
  hline: '<path d="M3 10h14M6 6v8m8-8v8" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
  vline: '<path d="M10 3v14M6 6h8m-8 8h8" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
  area: '<rect x="3.5" y="4.5" width="13" height="11" rx="1.5" stroke="currentColor" stroke-width="1.5" stroke-dasharray="2.5 2" fill="none"/>',
  addline: '<path d="M4 14L14 4m-3 11h5m-2.5-2.5v5" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
  automap: '<path d="M10 3l2 4.5L16.5 9 12 11l-2 4.5L8 11 3.5 9 8 7.5 10 3z" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linejoin="round"/>',
  plus: '<path d="M10 4v12M4 10h12" stroke="currentColor" stroke-width="1.7" fill="none" stroke-linecap="round"/>',
  add: '<path d="M10 4v12M4 10h12" stroke="currentColor" stroke-width="1.7" fill="none" stroke-linecap="round"/>',
  minus: '<path d="M4 10h12" stroke="currentColor" stroke-width="1.7" fill="none" stroke-linecap="round"/>',
  remove: '<path d="M4 10h12" stroke="currentColor" stroke-width="1.7" fill="none" stroke-linecap="round"/>',
  duplicate: '<rect x="6.5" y="6.5" width="9" height="9" rx="1.2" stroke="currentColor" stroke-width="1.4" fill="none"/><path d="M4.5 12.5V5a1 1 0 0 1 1-1h7.5" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round"/>',
  folder: '<path d="M3 6a1.5 1.5 0 0 1 1.5-1.5h3.5l1.5 2h6A1.5 1.5 0 0 1 17 8v6.5a1.5 1.5 0 0 1-1.5 1.5h-11A1.5 1.5 0 0 1 3 14.5V6z" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linejoin="round"/>',
  eye: '<path d="M2.5 10s3-5 7.5-5 7.5 5 7.5 5-3 5-7.5 5-7.5-5-7.5-5z" stroke="currentColor" stroke-width="1.4" fill="none"/><circle cx="10" cy="10" r="2.2" stroke="currentColor" stroke-width="1.4" fill="none"/>',
  lock: '<rect x="4.5" y="9" width="11" height="7.5" rx="1.5" stroke="currentColor" stroke-width="1.4" fill="none"/><path d="M7 9V6.5a3 3 0 0 1 6 0V9" stroke="currentColor" stroke-width="1.4" fill="none"/>',
  unselect: '<rect x="4" y="4" width="12" height="12" rx="1.5" stroke="currentColor" stroke-width="1.4" stroke-dasharray="2.5 2" fill="none"/><path d="M7 7l6 6M13 7l-6 6" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>',
  to_layer: '<path d="M4 13l6 3 6-3M4 9l6 3 6-3M10 3v6m-2.5-2L10 9.5 12.5 7" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  float: '<rect x="3.5" y="4.5" width="13" height="11" rx="1.5" stroke="currentColor" stroke-width="1.4" fill="none"/><path d="M11 4.5v4.5h5.5" stroke="currentColor" stroke-width="1.4" fill="none"/>',
  play: '<path d="M6 4.5l10 5.5-10 5.5V4.5z" fill="currentColor"/>',
  pause: '<path d="M6.5 4.5h2.5v11H6.5zm4.5 0h2.5v11H11z" fill="currentColor"/>',
  prev: '<path d="M5.5 5v10M14.5 5l-7 5 7 5V5z" stroke="currentColor" stroke-width="1.4" fill="currentColor" stroke-linecap="round" stroke-linejoin="round"/>',
  next: '<path d="M14.5 5v10M5.5 5l7 5-7 5V5z" stroke="currentColor" stroke-width="1.4" fill="currentColor" stroke-linecap="round" stroke-linejoin="round"/>',
  loop: '<path d="M14 6l2 2-2 2M6 14l-2-2 2-2M4 10V8a2 2 0 0 1 2-2h10m0 4v2a2 2 0 0 1-2 2H4" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  onion: '<circle cx="8.5" cy="10" r="4.5" stroke="currentColor" stroke-width="1.4" fill="none"/><circle cx="11.5" cy="10" r="4.5" stroke="currentColor" stroke-width="1.4" stroke-dasharray="2 1.5" fill="none"/>',
  clear: '<path d="M5 5l10 10M15 5L5 15" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
  hold: '<path d="M5 5v10m4-10v10m3-5h4m-2-2v4" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
};

export default function create(el, { props, emit }) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "vf-std-button";
  let current = props;

  button.addEventListener("click", () => {
    if (current.enabled === false) return;
    if (current.checkable) {
      emit("toggle", !current.checked);
    }
  });
  el.append(button);

  function update(next) {
    current = next;
    const iconName = String(next.icon ?? "").trim();
    const text = String(next.text ?? "");
    if (iconName && ICONS[iconName]) {
      const svg = `<svg viewBox="0 0 20 20" width="14" height="14" style="flex-shrink:0;display:block">${ICONS[iconName]}</svg>`;
      const span = text ? `<span style="overflow:hidden;text-overflow:ellipsis">${text.replace(/&/g, "&amp;").replace(/</g, "&lt;")}</span>` : "";
      button.innerHTML = svg + span;
      button.dataset.hasIcon = "true";
    } else {
      button.textContent = text;
      delete button.dataset.hasIcon;
    }
    button.dataset.variant = String(next.variant ?? "secondary");
    button.dataset.checked = String(Boolean(next.checked));
    button.disabled = next.enabled === false;
  }
  update(props);

  return { update, destroy: () => button.remove() };
}
