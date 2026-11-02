// A strip that scrolls what does not fit. The chevron matches the tool glyphs:
// same stroke, round ends, inked with currentColor.
export default function create(el, { props }) {
  const strip = document.createElement("div");
  strip.className = "qt-scroll-arrow";
  strip.innerHTML = `<svg viewBox="0 0 12 8" xmlns="http://www.w3.org/2000/svg">`
    + `<path d="M2 2.25 L6 5.75 L10 2.25" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg>`;
  el.append(strip);

  function update(next) {
    strip.classList.toggle("up", next.direction === "up");
  }
  update(props);

  return { update, destroy: () => strip.remove() };
}
