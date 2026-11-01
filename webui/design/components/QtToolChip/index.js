// One tool button of the Tools rail. The glyphs are the same strokes as drawGlyph
// in childrenpanel/toolspanel.cpp, on a 24x24 sheet, inked with currentColor.
const line = 'fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"';

const GLYPHS = {
  arrow: `<g ${line} transform="translate(0.8 0.5)"><path d="M5.5 3 L5.5 18 L9.8 14 L12.4 20.5 L14.9 19.4 L12.3 13 L17.6 13 Z"/></g>`,
  pen: `<g ${line}><path d="M3 21 L4.8 16.2 L16.2 4.8 L19.2 7.8 L7.8 19.2 Z"/><line x1="14.4" y1="6.6" x2="17.4" y2="9.6"/></g>`,
  eraser: `<g ${line}><g transform="rotate(-40 12 11)"><rect x="4.5" y="7.5" width="15" height="7"/><line x1="11" y1="7.5" x2="11" y2="14.5"/></g><line x1="4" y1="21" x2="20" y2="21"/></g>`,
  fill: `<g transform="rotate(-35 12 12)"><path ${line} d="M7.7 8.1 C8.4 4.4 15.6 4.4 16.3 8.1"/>`
    + `<path fill="currentColor" d="M7.3 13.2 L16.7 13.2 L15.9 18.4 A1.7 1.7 0 0 1 14.2 19.9 L9.8 19.9 A1.7 1.7 0 0 1 8.1 18.4 Z"/>`
    + `<line ${line} x1="5.2" y1="8.6" x2="18.8" y2="8.6"/>`
    + `<path ${line} d="M6.6 8.6 L8.1 18.4 A1.7 1.7 0 0 0 9.8 19.9 L14.2 19.9 A1.7 1.7 0 0 0 15.9 18.4 L17.4 8.6"/></g>`,
  transfer: `<defs><mask id="transfer-hole" maskUnits="userSpaceOnUse" x="-2" y="-2" width="28" height="28"><rect x="-2" y="-2" width="28" height="28" fill="white"/><circle cx="16.8" cy="16.8" r="8.1" fill="black"/></mask></defs>`
    + `<g mask="url(#transfer-hole)"><rect x="4.5" y="4.5" width="15" height="15" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="1.5 1.5" stroke-dashoffset="2.25"/>`
    + `<g fill="currentColor"><rect x="3.2" y="3.2" width="2.6" height="2.6"/><rect x="18.2" y="3.2" width="2.6" height="2.6"/><rect x="3.2" y="18.2" width="2.6" height="2.6"/></g></g>`
    + `<g ${line}><line x1="16.8" y1="13.9" x2="16.8" y2="19.7"/><line x1="13.9" y1="16.8" x2="19.7" y2="16.8"/></g>`
    + `<g fill="currentColor"><polygon points="16.8,10.4 19.1,13.9 14.5,13.9"/><polygon points="16.8,23.2 14.5,19.7 19.1,19.7"/><polygon points="10.4,16.8 13.9,14.5 13.9,19.1"/><polygon points="23.2,16.8 19.7,14.5 19.7,19.1"/></g>`,
  connect: `<path fill="currentColor" stroke="currentColor" stroke-width=".6" stroke-linejoin="round" d="M15.8 6.9 L15.4 8.2 L14.7 9.4 L13.6 10.5 L12.4 11.5 L11 12.4 L9.6 13.3 L8.6 11.6 L7 15.8 L10.8 15.4 L9.8 13.7 L11.3 13 L12.9 12.4 L14.4 11.6 L15.8 10.5 L17.1 9.2 L17.9 7.5 Z"/>`
    + `<circle cx="6.8" cy="19.9" r="1.9" fill="none" stroke="currentColor" stroke-width="1.2"/><circle cx="17.2" cy="4.7" r="1.9" fill="none" stroke="currentColor" stroke-width="1.2"/>`
    + `<circle cx="17.2" cy="4.7" r="0.85" fill="currentColor"/>`,
  h_center_line: `<g ${line}><path d="M2.8 12 L21 12"/><path d="M17.6 9.1 L21 12 L17.6 14.9"/>`
    + `<path stroke-width="1.2" stroke-dasharray="1.3 2.7" d="M16.8 3.2 C16.8 10.4 7.2 13.6 7.2 20.8"/></g>`,
  v_center_line: `<g ${line}><path d="M12 2.8 L12 21"/><path d="M9.1 17.6 L12 21 L14.9 17.6"/>`
    + `<path stroke-width="1.2" stroke-dasharray="1.3 2.7" d="M3.2 16.8 C10.4 16.8 13.6 7.2 20.8 7.2"/></g>`,
  mapping_area: `<g ${line}><path d="M3.7 5.2 L13.5 3.3 L20.9 9.5 L17.5 20.7 L9.5 15.8 L3.1 15.4 Z"/>`
    + `<path stroke-width="1.2" d="M3.5 8.9 L8 4.4 M3.2 14.3 L13.9 3.6 M6.9 15.6 L16.6 5.9 M11 16.7 L19.4 8.3 M14.1 18.6 L19.9 12.9"/></g>`,
  additional_line: `<g ${line}><path stroke-width="1" stroke-dasharray="1.4 2.85" d="M2.8 3.5 H21.2 M2.8 12 H21.2 M2.8 20.5 H21.2 M3.5 2.8 V21.2 M12 2.8 V21.2 M20.5 2.8 V21.2"/>`
    + `<path d="M3.5 20.5 A8.5 17 0 0 1 20.5 20.5"/></g>`
    + `<g fill="currentColor"><circle cx="3.5" cy="20.5" r="1.6"/><circle cx="20.5" cy="20.5" r="1.6"/></g>`,
  auto_mapping: `<g ${line}><path d="M5.4 3.2 L17 3.2 L13.8 12.7 L2.2 12.7 Z"/><path d="M5.8 11.1 L10.6 4.9 L11.3 11.1 M7.6 8.7 L11.1 8.7"/><path d="M4.4 14.1 L2.2 20.7 L13.8 20.7 L16 14.1"/><path fill="currentColor" stroke="currentColor" stroke-width=".6" stroke-linejoin="round" d="M18.8 3.2 L19 5 L19.1 6.7 L19.3 8.5 L19.5 10.2 L19.6 12 L19.8 13.7 L17.6 13.7 L20 17.7 L22.4 13.7 L20.2 13.7 L20.4 12 L20.5 10.2 L20.7 8.5 L20.9 6.7 L21 5 L21.2 3.2 Z"/>`
    + `<path stroke-width="1.2" d="M8.3 14.1 L6.1 20.7 M12.2 14.1 L9.9 20.7 M3.4 17.3 L15 17.3"/></g>`,
};

// A tip waits for the pointer to settle, unless one was showing a moment ago:
// sliding along the chips then reads each tip without waiting again.
const TIP_DELAY = 450;
const TIP_GRACE = 300;
let tipClosedAt = -Infinity;

export default function create(el, { props }) {
  const chip = document.createElement("div");
  chip.className = "chip";
  el.append(chip);
  let shown = null;
  let current = props;
  let tip = null;
  let timer = 0;

  function showTip() {
    if (!current.tip && !current.tipTitle) return;
    tip = document.createElement("div");
    tip.className = "chip-tip";
    const title = document.createElement("b");
    title.textContent = current.tipTitle;
    tip.append(title, current.tip);
    el.append(tip);
    const box = chip.getBoundingClientRect();
    const view = el.ownerDocument.defaultView;
    const fitsRight = box.right + 8 + tip.offsetWidth <= view.innerWidth - 4;
    tip.style.left = `${fitsRight ? box.right + 8 : box.left - 8 - tip.offsetWidth}px`;
    tip.style.top = `${Math.max(4, Math.min(box.top, view.innerHeight - 4 - tip.offsetHeight))}px`;
  }

  function hideTip() {
    clearTimeout(timer);
    if (!tip) return;
    tip.remove();
    tip = null;
    tipClosedAt = performance.now();
  }

  chip.addEventListener("mouseenter", () => {
    timer = setTimeout(showTip, performance.now() - tipClosedAt < TIP_GRACE ? 0 : TIP_DELAY);
  });
  chip.addEventListener("mouseleave", hideTip);
  chip.addEventListener("mousedown", hideTip);

  function update(next) {
    current = next;
    if (next.glyph !== shown) {
      shown = next.glyph;
      chip.innerHTML = `<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">${GLYPHS[shown] ?? ""}</svg>`;
    }
    chip.classList.toggle("on", Boolean(next.armed));
    chip.classList.toggle("locked", next.enabled === false);
  }
  update(props);

  return { update, destroy() { hideTip(); chip.remove(); } };
}
