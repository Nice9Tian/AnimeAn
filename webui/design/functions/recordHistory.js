// 记录一条新的历史操作步骤；若当前处于回退状态（mutedFrom >= 0），先截断被撤销的后续分支。
export default function recordHistory(state, args) {
  const rawItems = Array.isArray(state.history?.items) ? state.history.items : [];
  const mutedFrom = Number(state.history?.mutedFrom ?? -1);
  const nextId = Number(state.history?.nextId ?? rawItems.length + 1);
  const label = String(args?.label ?? "画布操作");
  const base = mutedFrom >= 0 && mutedFrom < rawItems.length ? rawItems.slice(0, mutedFrom) : rawItems.slice();
  base.push({
    id: nextId,
    name: label,
    badge: "#" + nextId,
  });
  return {
    "history.items": base,
    "history.selectedIndex": base.length - 1,
    "history.mutedFrom": -1,
    "history.nextId": nextId + 1,
  };
}
