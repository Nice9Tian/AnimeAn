// Asks the real backend for everything it would put on screen: the menus, the
// options of every tool, every settings window and the layer context menus.
// Fails if a string has no translation. --list prints the strings instead.
//
// Usage: node backend/tests/i18n_coverage.mjs <path-to-AnimeAnBackend.exe> [--list]
import { spawn } from "node:child_process";
import path from "node:path";
import { language, LANGUAGES, missing, setLanguage, t } from "../../webui/src/i18n/index.js";

const exePath = process.argv[2];
if (!exePath) {
  console.error("Usage: node backend/tests/i18n_coverage.mjs <path-to-AnimeAnBackend.exe> [--list]");
  process.exit(1);
}
const listOnly = process.argv.includes("--list");

const TOOLS = ["arrow", "pen", "eraser", "fill", "transfer", "connect"];
const token = `i18n_${Date.now()}`;
const child = spawn(exePath, ["--port", "0", "--token", token], {
  stdio: ["ignore", "pipe", "pipe"],
  env: {
    ...process.env,
    PATH: ["C:/Qt/6.9.1/mingw_64/bin", "C:/Qt/Tools/mingw1310_64/bin", path.dirname(path.resolve(exePath)), process.env.PATH].join(";"),
  },
});

function finish(code, error) {
  if (error) console.error(error);
  child.kill();
  process.exit(code);
}
setTimeout(() => finish(1, new Error("Timed out")), 60000);

const port = await new Promise((resolve, reject) => {
  let text = "";
  child.stdout.on("data", (chunk) => {
    text += chunk.toString("utf8");
    const found = text.match(/ANIMEAN_BACKEND_PORT=(\d+)/);
    if (found) resolve(Number(found[1]));
  });
  child.once("exit", (code) => reject(new Error(`Backend exited with ${code}`)));
});

const states = new Map();
const events = [];
const pending = new Map();
let nextId = 1;
const socket = new WebSocket(`ws://127.0.0.1:${port}/?token=${encodeURIComponent(token)}`);
socket.binaryType = "arraybuffer";
socket.addEventListener("message", (event) => {
  if (typeof event.data !== "string") return;
  const message = JSON.parse(event.data);
  if (message.t === "state") {
    states.set(message.key, message.value);
    if (message.key === "event") events.push(message.value);
  } else if (message.t === "reply") {
    const waiter = pending.get(message.id);
    pending.delete(message.id);
    if (message.ok) waiter?.resolve(message.result);
    else waiter?.reject(new Error(message.error));
  }
});
await new Promise((resolve) => socket.addEventListener("open", resolve));

const call = (method, params = {}) => new Promise((resolve, reject) => {
  const id = nextId++;
  pending.set(id, { resolve, reject });
  socket.send(JSON.stringify({ t: "call", id, method, params }));
});
const settle = () => new Promise((resolve) => setTimeout(resolve, 150));

const seen = new Set();
function collect(value) {
  if (Array.isArray(value)) {
    value.forEach(collect);
  } else if (value && typeof value === "object") {
    for (const [key, item] of Object.entries(value)) {
      if ((key === "title" || key === "text") && typeof item === "string") seen.add(item);
      else collect(item);
    }
  }
}
const menuIds = (items, out = []) => {
  for (const item of items ?? []) {
    if (item.id) out.push(item.id);
    menuIds(item.children, out);
  }
  return out;
};

try {
  await settle();
  await call("project.new", { width: 1280, height: 720 });
  await settle();
  collect(states.get("menus"));
  for (const tool of TOOLS) {
    await call("tool.select", { tool });
    await settle();
    collect(states.get("toolOptions"));
  }
  for (const tool of states.get("tools")?.extraTools ?? []) {
    seen.add(tool.title);
    await call("tool.selectExtra", { name: tool.name });
    await settle();
    collect(states.get("toolOptions"));
  }

  const target = { view: "main", groupId: 0, groupName: "", layer: -1, members: [] };
  const empty = await call("layer.contextMenu", target);
  collect(empty.items);
  const newUnit = empty.items.find((item) => /Auto-Mapping/.test(item.text ?? ""));
  if (newUnit) {
    await call("layer.contextAction", { ...target, id: newUnit.id, title: newUnit.text });
    await settle();
  }
  for (const row of states.get("layers/main")?.rows ?? []) {
    const rowTarget = { view: "main", groupId: row.group ? row.group_id : 0, groupName: row.group ? row.name : "", layer: row.group ? -1 : row.layer, members: [] };
    const reply = await call("layer.contextMenu", rowTarget);
    collect(reply.items);
    for (const item of reply.items) {
      if (String(item.id).startsWith("settings:")) await call("layer.contextAction", { ...rowTarget, id: item.id, title: item.text });
    }
  }
  await settle();
  collect(states.get("menus"));
  collect(states.get("toolOptions"));

  for (const id of menuIds(states.get("menus"))) {
    if (id.startsWith("settings:")) await call("menu.trigger", { id });
  }
  await settle();
  for (const event of events) {
    if (event?.name === "settings.open") collect(event.args);
  }
} catch (error) {
  finish(1, error);
}

if (listOnly) {
  for (const text of [...seen].sort()) console.log(JSON.stringify(text));
  finish(0);
}

globalThis.localStorage = { setItem() {} };
let failed = 0;
for (const { id } of LANGUAGES) {
  if (id === "en") continue;
  missing.clear();
  setLanguage(id);
  for (const text of seen) t(text);
  for (const text of missing) {
    console.error(`${language()}: no translation for ${JSON.stringify(text)}`);
    failed += 1;
  }
}
console.log(`${seen.size} strings from the backend, ${failed} untranslated`);
finish(failed ? 1 : 0);
