// The title row of a collapsible group. A collapsed group holding the armed
// tool keeps a dot on its row, so the tool in use never goes out of sight.
export default function create(el, { props }) {
  const row = document.createElement("div");
  row.className = "qt-group";
  row.innerHTML = `<svg class="qt-group-arrow" viewBox="0 0 10 10" xmlns="http://www.w3.org/2000/svg">`
    + `<path d="M2 3.5 L5 6.5 L8 3.5" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg>`
    + `<span class="qt-group-label"></span><span class="qt-group-dot"></span>`;
  const label = row.querySelector(".qt-group-label");
  el.append(row);

  function update(next) {
    label.textContent = next.label;
    row.classList.toggle("closed", !next.open);
    row.classList.toggle("active", !next.open && Boolean(next.active));
  }
  update(props);

  return { update, destroy: () => row.remove() };
}
