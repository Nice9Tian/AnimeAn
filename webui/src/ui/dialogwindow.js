// One dialog Python declared (pyfile/dialogs.py), drawn by the page of its own
// window (dialog.html), or over the page in a browser without the shell. It
// decides nothing: what the user does goes back as dialog.result, and the page
// that hosts it opens and closes it as the "dialogs" state says.
//
// renderDialog(root, spec, { call, onTitleDrag }) -> { el, update(spec) }
//   call(method, params)  sends to the backend (dialog.result, settings.option, dialog.request)
//   onTitleDrag(event)    the title bar was pressed (the window moves itself)
import { label as labelled } from "../i18n/index.js";
import { createToolOptions } from "./options.js";
import { createPalette, toHexArgb } from "./palette.js";
import { createNewProjectDialog, messageBox } from "./panels2.js";

function div(cls, text) {
  const d = document.createElement("div");
  d.className = cls;
  if (text !== undefined) d.textContent = text;
  return d;
}

function pushButton(text, onClick, isDefault = false) {
  const b = document.createElement("button");
  b.type = "button";
  b.className = isDefault ? "qbutton qdefault" : "qbutton";
  labelled(b, text);
  b.addEventListener("click", onClick);
  return b;
}

// The title bar and the close box, as the other dialogs draw them.
function framed(root, cls, title, { onClose, onTitleDrag }) {
  const dialog = div(`qdialog in-window ${cls}`);
  const head = div("qdialog-title");
  head.append(labelled(div("qdialog-caption"), String(title ?? "")));
  const close = div("qdialog-close");
  close.innerHTML = `<svg width="10" height="10"><path d="M0.5 0.5 L9.5 9.5 M9.5 0.5 L0.5 9.5" stroke="currentColor" fill="none"/></svg>`;
  close.addEventListener("click", onClose);
  head.append(close);
  head.addEventListener("pointerdown", (e) => {
    if (e.target?.closest?.(".qdialog-close")) return;
    onTitleDrag?.(e);
  });
  const body = div("dialog-window-body");
  dialog.append(head, body);
  root.append(dialog);
  return { dialog, body };
}

function colorDialog(root, spec, answer, onTitleDrag) {
  let answered = false;
  const reply = (action, value) => {
    if (answered) return;
    answered = true;
    answer(action, value);
  };
  const { dialog, body } = framed(root, "color-window", spec.title || "Select Color", { onClose: () => reply("cancel"), onTitleDrag });
  const picker = createPalette(() => {}, { pickersOnly: true });
  const row = div("qdialog-buttons");
  const accept = () => reply("accept", toHexArgb(picker.color()));
  row.append(pushButton(spec.accept || "OK", accept, true), pushButton("Cancel", () => reply("cancel")));
  body.append(picker.el, row);
  dialog.tabIndex = -1;
  dialog.addEventListener("keydown", (e) => {
    if (e.key === "Enter") { e.preventDefault(); accept(); }
    if (e.key === "Escape") { e.preventDefault(); reply("cancel"); }
  });
  picker.update({ value: spec.value, swatches: [] });
  return { el: dialog, update() {} };
}

function settingsDialog(root, spec, call, answer, onTitleDrag) {
  const { dialog, body } = framed(root, "settings-window", spec.title || spec.name, { onClose: () => answer("cancel"), onTitleDrag });
  const controls = div("settings-window-controls");
  const panel = createToolOptions(controls, {
    onOption: (option) => call("settings.option", { ...option, menu: spec.name }),
    onDialog: (request) => call("dialog.request", { ...request, method: "settings.option", menu: spec.name }),
    onSubWindow: () => null,
  });
  const row = div("qdialog-buttons");
  row.append(pushButton("Close", () => answer("cancel")));
  body.append(controls, row);
  dialog.tabIndex = -1;
  dialog.addEventListener("keydown", (e) => {
    if (e.key === "Escape") { e.preventDefault(); answer("cancel"); }
  });
  let shown = null;
  const build = (layout) => {
    const text = JSON.stringify(layout ?? {});
    if (text === shown) return;
    shown = text;
    panel.build(layout ?? {});
  };
  build(spec.layout);
  // Python sends the layout again when a value in it changed from here (a colour).
  return { el: dialog, update: (next) => build(next.layout) };
}

export function renderDialog(root, spec, { call, onTitleDrag } = {}) {
  const answer = (action, value) => call?.("dialog.result", value === undefined ? { id: spec.id, action } : { id: spec.id, action, value });
  switch (spec.kind) {
    case "message": {
      const buttons = Array.isArray(spec.buttons) && spec.buttons.length ? spec.buttons : ["OK"];
      void messageBox({ title: spec.title, text: spec.text, buttons }, { root, inWindow: true, onTitleDrag })
        .then((button) => (button === null ? answer("cancel") : answer("button", button)));
      return { el: root.querySelector(".qdialog.in-window"), update() {} };
    }
    case "canvasSize": {
      const dialog = createNewProjectDialog(root, {
        inWindow: true,
        onTitleDrag,
        onCreate: (size) => answer("accept", size),
        onCancel: () => answer("cancel"),
      });
      dialog.update({ width: spec.width, height: spec.height, open: true });
      return { el: dialog.el, update() {} };
    }
    case "color":
      return colorDialog(root, spec, answer, onTitleDrag);
    case "settings":
      return settingsDialog(root, spec, call, answer, onTitleDrag);
    default:
      answer("error", `Unknown dialog kind: ${spec.kind}`);
      return { el: null, update() {} };
  }
}
