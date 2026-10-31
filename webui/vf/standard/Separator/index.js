// 标准分隔线。
export default function create(el, { props }) {
  const line = document.createElement("div");
  line.className = "vf-std-separator";
  el.append(line);

  function update(next) {
    line.dataset.orientation = String(next.orientation ?? "horizontal");
  }
  update(props);

  return { update, destroy: () => line.remove() };
}
