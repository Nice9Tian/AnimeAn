// A push button as the Qt build draws it.
export default function create(el, { props }) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "qbutton";
  el.append(button);

  function update(next) {
    button.textContent = next.label;
    button.disabled = next.enabled === false;
    button.classList.toggle("checked", Boolean(next.checked));
  }
  update(props);

  return { update, destroy: () => button.remove() };
}
