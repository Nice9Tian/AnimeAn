// 在历史面板中执行撤销、重做或点击某条历史记录回溯，并同步 mutedFrom 置灰分界线。
export default function rewindHistory(state, args) {
  const items = Array.isArray(state.history?.items) ? state.history.items : [];
  if (items.length === 0) return {};
  const cur = Number(state.history?.selectedIndex ?? items.length - 1);
  const mode = String(args?.mode ?? "select");
  if (mode === "clear") {
    return {
      "history.items": [{ id: 1, name: "初始化画布 (1920x1080)", badge: "#1" }],
      "history.selectedIndex": 0,
      "history.mutedFrom": -1,
      "history.nextId": 2,
      "project.statusMessage": "已清空历史记录",
    };
  }
  let target = cur;
  if (mode === "undo") {
    target = Math.max(0, cur - 1);
  } else if (mode === "redo") {
    target = Math.min(items.length - 1, cur + 1);
  } else {
    const rawIdx = Number(args?.index ?? args?.value ?? cur);
    if (Number.isInteger(rawIdx) && rawIdx >= 0 && rawIdx < items.length) {
      target = rawIdx;
    }
  }
  const mutedFrom = target < items.length - 1 ? target + 1 : -1;
  const entryName = items[target]?.name ?? ("步骤 " + (target + 1));
  return {
    "history.selectedIndex": target,
    "history.mutedFrom": mutedFrom,
    "project.statusMessage": "历史回溯：" + entryName,
  };
}
