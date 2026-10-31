// 处理图层面板中的图层分组操作（解组保留图层、删除分组及成员、素材转图层）。
export default function manageLayerGroup(state, args) {
  const mode = String(args?.mode ?? "");
  const rawItems = Array.isArray(state.layers?.items) ? state.layers.items.slice() : [];
  const sel = Number(state.layers?.selectedIndex ?? 0);
  const nextId = Number(state.layers?.nextId ?? rawItems.length + 1);

  if (mode === "assetToLayer") {
    const assets = Array.isArray(state.assets?.items) ? state.assets.items : [];
    const assetIdx = Number(state.assets?.selectedIndex ?? 0);
    const asset = assets[assetIdx] ?? assets[0] ?? { id: 1, name: "默认素材.png" };
    const newLayer = {
      id: "lyr_asset_" + nextId,
      name: "素材层：" + String(asset.name ?? "素材"),
      kind: "Asset",
      isGroup: false,
      depth: 0,
      collapsed: false,
      checked: true,
      visible: true,
      locked: false,
    };
    rawItems.push(newLayer);
    return {
      "layers.items": rawItems,
      "layers.selectedIndex": rawItems.length - 1,
      "layers.nextId": nextId + 1,
      "project.statusMessage": "已将素材转为图层：" + newLayer.name,
    };
  }

  let groupIdx = -1;
  if (sel >= 0 && sel < rawItems.length && rawItems[sel]?.isGroup) {
    groupIdx = sel;
  } else if (sel >= 0 && sel < rawItems.length && Number(rawItems[sel]?.depth ?? 0) > 0) {
    for (let i = sel - 1; i >= 0; i--) {
      if (rawItems[i]?.isGroup) {
        groupIdx = i;
        break;
      }
    }
  }
  if (groupIdx < 0) {
    groupIdx = rawItems.findIndex((it) => Boolean(it?.isGroup));
  }
  if (groupIdx < 0) {
    return {
      "project.statusMessage": "当前没有可操作的图层组",
    };
  }

  const groupDepth = Number(rawItems[groupIdx]?.depth ?? 0);
  const groupName = String(rawItems[groupIdx]?.name ?? "图层组");
  let endChildIdx = groupIdx + 1;
  while (endChildIdx < rawItems.length && Number(rawItems[endChildIdx]?.depth ?? 0) > groupDepth) {
    endChildIdx++;
  }

  if (mode === "dissolve") {
    const updated = [];
    for (let i = 0; i < rawItems.length; i++) {
      if (i === groupIdx) continue;
      const item = Object.assign({}, rawItems[i]);
      if (i > groupIdx && i < endChildIdx) {
        item.depth = Math.max(0, Number(item.depth ?? 1) - 1);
      }
      updated.push(item);
    }
    const nextSel = Math.min(groupIdx, Math.max(0, updated.length - 1));
    return {
      "layers.items": updated,
      "layers.selectedIndex": nextSel,
      "project.statusMessage": "已解组并保留成员图层：" + groupName,
    };
  }

  if (mode === "deleteGroup") {
    const updated = rawItems.filter((_, i) => i < groupIdx || i >= endChildIdx);
    if (updated.length === 0) {
      updated.push({
        id: "lyr_default",
        name: "默认背景层",
        kind: "Raster",
        isGroup: false,
        depth: 0,
        collapsed: false,
        checked: true,
        visible: true,
        locked: false,
      });
    }
    const nextSel = Math.min(groupIdx, Math.max(0, updated.length - 1));
    return {
      "layers.items": updated,
      "layers.selectedIndex": nextSel,
      "project.statusMessage": "已删除图层组及成员：" + groupName,
    };
  }

  return {};
}
