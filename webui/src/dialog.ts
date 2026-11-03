// The page of one dialog window (dialog.html?id=<id>&owner=<label>). It draws
// the dialog Python declared under that id, sizes its window to it, and says
// what the user did. The main page opens and closes the window as the
// "dialogs" state says; this page decides nothing.
import { invoke } from "@tauri-apps/api/core";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { RelayClient } from "./relay.ts";
import { createStore } from "./ui/app.js";
import { createDialogHost, DIALOG_CHANNEL, dialogLabel } from "./ui/dialoghost.js";
import { renderDialog } from "./ui/dialogwindow.js";
import "./ui/qt.css";
import "./ui/qt3.css";
import "./ui/shell.css";

const params = new URLSearchParams(window.location.search);
const id = params.get("id") ?? "";
const owner = params.get("owner") ?? "main";
const root = document.getElementById("dialog")!;

// An application window, not a browser tab: no reload keys, no browser menu.
window.addEventListener("contextmenu", (e) => {
  if (!(e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement)) e.preventDefault();
});
window.addEventListener("keydown", (e) => {
  const typing = e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement;
  if ((e.ctrlKey || e.altKey || /^F\d+$/.test(e.key)) && !typing) e.preventDefault();
});

const client = new RelayClient();
const store = createStore(client);
const failed = (error: unknown) =>
  store.call("dialog.result", { id, action: "error", value: String((error as Error)?.message ?? error) });
window.addEventListener("error", (e) => void failed(e.error ?? e.message));
window.addEventListener("unhandledrejection", (e) => void failed(e.reason));

store.on("theme", (value: { mode?: string } | undefined) => {
  document.documentElement.dataset.theme = value?.mode === "dark" ? "dark" : "light";
});
createDialogHost(store, { role: "dialog", ownId: id });

let view: { el: HTMLElement | null; update(spec: unknown): void } | null = null;
store.on("dialogs", (list: Array<{ id: string }> | undefined) => {
  const spec = (list ?? []).find((entry) => entry.id === id);
  if (!spec) return; // The main page closes this window.
  if (view) {
    view.update(spec);
    return;
  }
  view = renderDialog(root, spec, {
    call: store.call,
    onTitleDrag: () => void getCurrentWindow().startDragging(),
  });
  // The window takes the size of what it shows, sits over its owner, and only
  // then appears.
  requestAnimationFrame(() => {
    const box = view?.el?.getBoundingClientRect();
    void invoke("show_dialog_window", {
      label: dialogLabel(id),
      owner,
      width: Math.ceil(box?.width ?? 320),
      height: Math.ceil(box?.height ?? 200),
    }).then(() => {
      new BroadcastChannel(DIALOG_CHANNEL).postMessage({ type: "shown", id });
      (view?.el as HTMLElement | null)?.focus?.({ preventScroll: true });
    }, failed);
  });
});

void client.connect("");
