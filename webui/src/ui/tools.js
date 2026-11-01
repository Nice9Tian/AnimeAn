// Joins the Tools nodes of the design to the backend: the business events they
// raise, the tool state they read, and which of the two groups is open.
function fail(error) {
  console.error(error);
}

// Business events of the design logic and the backend calls they become.
const BUSINESS_CALLS = { toolSelect: "tool.select", toolSelectExtra: "tool.selectExtra" };

export function raiseBusiness(store, name, args) {
  if (name === "toolGroupToggle") {
    store.toolGroups?.toggle(args.group);
    return true;
  }
  const method = BUSINESS_CALLS[name];
  if (!method) return false;
  store.call(method, args);
  return true;
}

const TOOL_GROUPS_KEY = "animean.toolGroups";

function savedToolGroups() {
  try {
    return { painting: true, mapping: true, ...JSON.parse(localStorage.getItem(TOOL_GROUPS_KEY)) };
  } catch {
    return { painting: true, mapping: true };
  }
}

// Which Tools groups are open. Like the board renderer it belongs to this
// machine, not the document, so the backend never hears of it.
function createToolGroups(write) {
  const open = savedToolGroups();
  const show = (group) => write(`tools.${group}Open`, Boolean(open[group]));
  const set = (group, value) => {
    if (open[group] === value) return;
    open[group] = value;
    show(group);
    try {
      localStorage.setItem(TOOL_GROUPS_KEY, JSON.stringify(open));
    } catch (error) {
      fail(error);
    }
  };
  show("painting");
  show("mapping");
  return { toggle: (group) => set(group, !open[group]), expand: (group) => set(group, true) };
}

const ERASERS = new Set(["eraser", "delete_line", "cut_line"]);

// Writes the backend's tool state into the state the Tools nodes read. The
// lockable names are the design logic's, so a tool needs a node to show up.
export function syncTools(runtime, store, lockable) {
  const write = (path, value) => {
    const error = runtime.setBusinessState(path, value);
    if (error) fail(error);
  };
  const groups = createToolGroups(write);
  store.toolGroups = groups;
  let armed = null;
  let page = null;
  store.on("tools", (state) => {
    if (!state) return;
    const next = state.activeExtraTool || (ERASERS.has(state.current) ? "eraser" : state.current ?? "");
    write("tools.armed", next);
    // Only a new tool opens its group: closing the group of the armed tool stays closed.
    if (armed !== null && next !== armed) {
      const extra = state.extraTools?.find((tool) => tool.name === state.activeExtraTool);
      groups.expand(state.activeExtraTool ? extra?.page ?? "mapping" : "painting");
    }
    armed = next;
    const locked = new Set(state.lockedTools ?? []);
    for (const name of lockable) write(`tools.locked.${name}`, locked.has(name));
    for (const tool of state.extraTools ?? []) {
      if (!lockable.includes(tool.name)) fail(`Tools 面板没有 ${tool.name} 的节点`);
    }
  });
  // The layer policy still names a page; a page is a group now.
  store.on("windows", (value) => {
    const next = value?.tools?.current;
    if (page !== null && next && next !== page) groups.expand(next);
    page = next ?? page;
  });
}
