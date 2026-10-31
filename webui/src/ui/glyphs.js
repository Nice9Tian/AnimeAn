// The tool glyphs on their 24x24 sheet, the same strokes as drawGlyph in
// childrenpanel/toolspanel.cpp. Everything inks with currentColor.
const line = 'fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"';

export const GLYPHS = {
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
  connect: `<path ${line} d="M17.2 4.7 C17.2 12.4 9.4 12 6.8 16"/>`
    + `<path fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round" d="M9.2 15.89 L6.8 16 L7.11 13.62"/>`
    + `<circle cx="6.8" cy="19.9" r="1.9" fill="none" stroke="currentColor" stroke-width="1.2"/><circle cx="17.2" cy="4.7" r="1.9" fill="none" stroke="currentColor" stroke-width="1.2"/>`
    + `<circle cx="17.2" cy="4.7" r="0.85" fill="currentColor"/>`,
};

export function glyphSvg(name) {
  return `<svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">${GLYPHS[name]}</svg>`;
}

// Fusion's dock title buttons and scrollbar arrows.
export const DOCK_FLOAT = `<svg width="18" height="14" viewBox="0 0 18 14" xmlns="http://www.w3.org/2000/svg"><rect x="4" y="2" width="10" height="10" rx="1" fill="#c9c9ca"/><rect x="6" y="4" width="6" height="6" fill="#b3b3b4"/><rect x="7" y="5" width="3" height="3" fill="#f0f0f1"/></svg>`;
export const DOCK_CLOSE = `<svg width="18" height="14" viewBox="0 0 18 14" xmlns="http://www.w3.org/2000/svg"><rect x="4" y="2" width="10" height="10" rx="1" fill="#c9c9ca"/><rect x="6" y="4" width="6" height="6" fill="#ececed"/><path d="M6.5 4.5 L11.5 9.5 M11.5 4.5 L6.5 9.5" stroke="#6d6e6f" stroke-width="1.6"/></svg>`;
