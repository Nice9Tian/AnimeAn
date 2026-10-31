// 标准对话框按钮组控件。
export default function create(el, { props, emit }) {
  const bar = document.createElement("div");
  bar.className = "vf-std-buttonbox";
  el.append(bar);

  function parseButtons(raw) {
    if (Array.isArray(raw)) return raw.map(String);
    return String(raw ?? "OK,Cancel")
      .split(",")
      .map((s) => s.trim())
      .filter(Boolean);
  }

  function update(next) {
    bar.replaceChildren();
    const list = parseButtons(next.buttons);
    const disabled = next.enabled === false;
    for (const role of list) {
      const isAccept = /^(ok|accept|yes|apply|create|save|confirm)$/i.test(role);
      const isReject = /^(cancel|reject|no|close)$/i.test(role);
      const text =
        isAccept && next.okText && next.okText !== "OK"
          ? String(next.okText)
          : isReject && next.cancelText && next.cancelText !== "Cancel"
            ? String(next.cancelText)
            : role;
      const btn = document.createElement("button");
      btn.type = "button";
      btn.dataset.vfRole = isAccept ? "accept" : isReject ? "reject" : role.toLowerCase();
      btn.className = "vf-std-buttonbox-btn" + (isAccept ? " primary" : "");
      btn.textContent = text;
      btn.disabled = disabled;
      btn.addEventListener("click", () => {
        if (disabled) return;
        if (isAccept) emit("accept");
        else if (isReject) emit("reject");
      });
      bar.append(btn);
    }
  }
  update(props);

  return { update, destroy: () => bar.remove() };
}
