// What the "dialogs" state (pyfile/dialogs.py) means for this page. Which
// dialog shows and whether it is modal is Python's; nothing here decides either.
//
//   main       opens a window per dialog (dialog.html) or a native picker,
//              closes them as they leave the state, and locks the other AnimeAn
//              windows while a modal one is open: the dialog's owner is disabled
//              (Windows' own modality brings the dialog forward when the owner
//              is clicked) and the panel windows cannot be activated.
//   satellite  a panel in a window of its own; it only blocks.
//   dialog     the page of one dialog; it blocks while a modal one sits above it.
//
// Every page under a modal dialog swallows its own input, and a press on it
// brings the dialog forward. Without the shell (a plain browser) the dialogs
// are drawn over the main page instead and pickers answer Cancel.
import { t } from "../i18n/index.js";
import { renderDialog } from "./dialogwindow.js";

// Dialog pages say here that they are on screen.
export const DIALOG_CHANNEL = "animean.dialog";
// Long enough for a cold WebView2 to load its first dialog page at startup.
const SHOW_TIMEOUT_MS = 30000;
const SWALLOWED = ["keydown", "keyup", "pointermove", "pointerup", "mousedown", "mousemove", "mouseup", "click", "dblclick", "contextmenu", "wheel"];

export function tauriShell(tauri = typeof window !== "undefined" ? window.__TAURI__ : undefined) {
  const invoke = tauri?.core?.invoke;
  if (typeof invoke !== "function") return null;
  return {
    openDialogWindow: (args) => invoke("open_dialog_window", args),
    closeDialogWindow: (label) => invoke("close_dialog_window", { label }),
    setWindowEnabled: (label, enabled) => invoke("set_window_enabled", { label, enabled }),
    setPanelsFocusable: (focusable) => invoke("set_panels_focusable", { focusable }),
    focusModal: () => invoke("focus_modal"),
    pickFiles: (args) => invoke("pick_files", args),
    onDialogCloseRequested: (fn) => tauri.event?.listen?.("dialog-close-requested", (event) => fn(event.payload)),
  };
}

export const dialogLabel = (id) => `dialog-${id}`;

export function createDialogHost(store, options = {}) {
  const role = options.role ?? "main";
  const shell = options.shell === undefined ? tauriShell() : options.shell;
  const showTimeoutMs = options.showTimeoutMs ?? SHOW_TIMEOUT_MS;
  const doc = options.document ?? document;
  const win = options.window ?? window;
  const isModal = (spec) => spec.modal !== false;
  let list = [];

  // ---- blocking ----------------------------------------------------------------
  // Over the main page itself the drawn dialogs carry their own shade instead.
  const blocks = Boolean(shell) || role !== "main";
  let blocker = null;
  const underModal = () => {
    if (role !== "dialog") return list.some(isModal);
    const at = list.findIndex((spec) => spec.id === options.ownId);
    return at >= 0 && list.slice(at + 1).some(isModal);
  };
  const swallow = (e) => {
    if (!blocker) return;
    e.stopImmediatePropagation?.();
    e.stopPropagation?.();
    e.preventDefault?.();
  };
  if (blocks) {
    for (const type of SWALLOWED) win.addEventListener(type, swallow, true);
    win.addEventListener("pointerdown", (e) => {
      if (!blocker) return;
      swallow(e);
      void Promise.resolve(shell?.focusModal?.()).catch(() => {});
    }, true);
  }
  const showBlocked = () => {
    const on = blocks && underModal();
    if (on && !blocker) {
      blocker = doc.createElement("div");
      blocker.className = "modal-blocker";
      doc.body.append(blocker);
    } else if (!on && blocker) {
      blocker.remove();
      blocker = null;
    }
  };

  const answer = (id, action, value) =>
    store.call("dialog.result", value === undefined ? { id, action } : { id, action, value });

  // ---- main: windows, pickers and locks ------------------------------------------
  const opened = new Map(); // id -> { kind, label?, shade?, view? }
  const locked = { main: false, owners: new Set(), panels: false };
  const waitingToShow = new Map(); // id -> timer
  let chain = Promise.resolve();
  const enqueue = (work) => {
    chain = chain.then(work).catch((error) => console.error(error));
    return chain;
  };
 const quietly = (promise) => Promise.resolve(promise).catch(() => {});

  const ownerOf = (next, index) => {
    for (let i = index - 1; i >= 0; i -= 1) {
      if (next[i].kind !== "file") return dialogLabel(next[i].id);
    }
    return "main";
  };

  // What has to be locked while `next` is on screen.
  const locksFor = (next) => {
    let top = -1;
    next.forEach((spec, i) => { if (isModal(spec)) top = i; });
    if (top < 0) return { main: false, owners: new Set(), panels: false };
    return {
      // A native picker disables its owner itself and enables it when it closes.
      main: next[top].kind !== "file",
      owners: new Set(next.slice(0, top).filter((spec) => spec.kind !== "file").map((spec) => dialogLabel(spec.id))),
      panels: true,
    };
  };

  const unlockAllBut = async (want) => {
    if (locked.main && !want.main) { await quietly(shell.setWindowEnabled("main", true)); locked.main = false; }
    for (const label of [...locked.owners]) {
      if (want.owners.has(label)) continue;
      await quietly(shell.setWindowEnabled(label, true));
      locked.owners.delete(label);
    }
    if (locked.panels && !want.panels) { await quietly(shell.setPanelsFocusable(true)); locked.panels = false; }
  };

  const lock = async (want) => {
    if (want.panels && !locked.panels) { await quietly(shell.setPanelsFocusable(false)); locked.panels = true; }
    for (const label of want.owners) {
      if (locked.owners.has(label) || !opened.has(label.slice("dialog-".length))) continue;
      await quietly(shell.setWindowEnabled(label, false));
      locked.owners.add(label);
    }
    if (want.main && !locked.main) { await quietly(shell.setWindowEnabled("main", false)); locked.main = true; }
  };

  const openPicker = (spec) => {
    const request = {
      mode: spec.mode,
      title: t(spec.title ?? ""),
      filters: (spec.filters ?? []).map((filter) => ({ ...filter, name: t(filter.name) })),
      multiple: Boolean(spec.multiple),
      defaultName: spec.defaultName ?? "",
    };
    const picked = shell ? shell.pickFiles(request) : Promise.resolve([]);
    void Promise.resolve(picked)
      .then((paths) => {
        const chosen = Array.isArray(paths) ? paths.filter(Boolean) : [];
        return answer(spec.id, chosen.length ? "accept" : "cancel", chosen);
      })
      .catch((error) => answer(spec.id, "error", String(error?.message ?? error)));
  };

  const open = async (spec, owner) => {
    if (spec.kind === "file") {
      opened.set(spec.id, { kind: "file" });
      openPicker(spec);
      return;
    }
    if (!shell) {
      const shade = doc.createElement("div");
      shade.className = "qdialog-shade";
      doc.body.append(shade);
      const view = renderDialog(shade, spec, { call: store.call });
      opened.set(spec.id, { kind: spec.kind, shade, view });
      return;
    }
    const label = dialogLabel(spec.id);
    opened.set(spec.id, { kind: spec.kind, label });
    const url = `dialog.html?id=${encodeURIComponent(spec.id)}&owner=${encodeURIComponent(owner)}`;
    try {
      await shell.openDialogWindow({ label, url, owner, title: t(spec.title ?? "") });
    } catch (error) {
      opened.delete(spec.id);
      void answer(spec.id, "error", String(error?.message ?? error));
      return;
    }
    // A page that never shows itself would leave the windows locked for good.
    waitingToShow.set(spec.id, setTimeout(() => {
      waitingToShow.delete(spec.id);
      if (opened.has(spec.id)) void answer(spec.id, "error", "The dialog window did not show.");
    }, showTimeoutMs));
  };

  const close = async (id) => {
    const entry = opened.get(id);
    opened.delete(id);
    clearTimeout(waitingToShow.get(id));
    waitingToShow.delete(id);
    if (entry?.label) await quietly(shell.closeDialogWindow(entry.label));
    entry?.shade?.remove();
  };

  const realize = async (next) => {
    const removed = [...opened.keys()].filter((id) => !next.some((spec) => spec.id === id)).reverse();
    const want = locksFor(next);
    if (shell) {
      // A window destroyed while its owner is disabled hands the focus to
      // another program, so nothing stays locked while anything closes.
      await unlockAllBut(removed.length ? locksFor([]) : want);
    }
    for (const [index, spec] of next.entries()) {
      if (opened.has(spec.id)) opened.get(spec.id).view?.update(spec);
      else await open(spec, ownerOf(next, index));
    }
    for (const id of removed) await close(id);
    if (shell) await lock(want);
  };

  if (role === "main") {
    const channel = options.channel !== undefined
      ? options.channel
      : typeof BroadcastChannel !== "undefined" ? new BroadcastChannel(DIALOG_CHANNEL) : null;
    channel?.unref?.(); // Node's keeps a test run alive; browsers have no unref.
    channel?.addEventListener?.("message", (event) => {
      const message = event?.data ?? event;
      if (message?.type !== "shown") return;
      clearTimeout(waitingToShow.get(message.id));
      waitingToShow.delete(message.id);
    });
    // Closing a dialog window from Windows is an answer like any other; the
    // window goes when the state says so, after its owner is enabled again.
    void Promise.resolve(shell?.onDialogCloseRequested?.((label) => {
      const id = String(label ?? "").slice("dialog-".length);
      if (opened.has(id)) void answer(id, "closed");
    })).catch(() => {});
    // No backend, no answers: nothing may stay open or locked.
    store.onDisconnect?.(() => {
      list = [];
      showBlocked();
      void enqueue(() => realize([]));
    });
  }

  store.on("dialogs", (value) => {
    list = Array.isArray(value) ? value : [];
    showBlocked();
    if (role === "main") {
      const next = list;
      void enqueue(() => realize(next));
    }
  });

  return {
    isBlocked: () => Boolean(blocker),
    settled: () => chain,
  };
}
