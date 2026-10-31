// 根据当前激活的工具类型，返回工具选项面板（DataForm）的表单结构 schema 与默认参数值 values。
// 严格对齐 animean/childrenpanel/tool_controls/*.json 与 animean/pyfile/toolcontrol.py 的 options_for_tool_json / options_for_extra_tool_json。

function automappingControls(withStrokeSliders) {
  const controls = [
    {
      name: "fold_split",
      type: "check",
      title: "Front/Back Split",
      hook: "fold_split",
      value: "on",
      row: 0,
      start_column: 0,
      end_column: 2,
    },
    {
      name: "fold_seal",
      type: "check",
      title: "Crease Line",
      hook: "fold_seal",
      value: "on",
      row: 1,
      start_column: 0,
      end_column: 2,
      visible_when: { name: "fold_split", values: ["on"] },
    },
    {
      name: "bridge_topology",
      type: "check",
      title: "补全拓扑",
      hook: "bridge_topology",
      value: "off",
      row: 2,
      start_column: 0,
      end_column: 2,
    },
    {
      name: "bridge_tension",
      type: "slider",
      title: "Bridge k (% |AB|)",
      hook: "bridge_tension",
      min: 5,
      max: 100,
      value: 33,
      row: 3,
      start_column: 0,
      end_column: 2,
      visible_when: { name: "bridge_topology", values: ["on"] },
    },
  ];
  let row = 4;
  if (withStrokeSliders) {
    controls.push(
      { name: "smooth", type: "slider", title: "Stabilizer", hook: "smooth", min: 0, max: 100, value: 50, row: row, start_column: 0, end_column: 2 },
      { name: "pen_width", type: "slider", title: "Width", hook: "pen_width", min: 1, max: 50, value: 5, row: row + 1, start_column: 0, end_column: 2 },
    );
    row += 2;
  }
  controls.push({
    name: "texture_view",
    type: "subwindow",
    title: "Texture",
    hook: "toggleTextureHost",
    row: row,
    start_column: 0,
    end_column: 2,
  });
  return {
    row_spacing: 4,
    column_spacing: 6,
    controls,
  };
}

const SCHEMAS = {
  arrow: {
    row_spacing: 4,
    column_spacing: 6,
    controls: [
      {
        name: "edit_mode",
        type: "list",
        title: "Edit Mode",
        hook: "edit_mode",
        value: "default",
        row: 0,
        start_column: 0,
        end_column: 2,
        options: [
          { title: "Default", value: "default" },
          { title: "Artist", value: "artist" },
          { title: "Debug", value: "debug" },
        ],
      },
    ],
  },
  brush: {
    row_spacing: 4,
    column_spacing: 6,
    controls: [
      { name: "color", type: "button", title: "Black", hook: "color", value: "black", row: 0, column: 0, state: { color: "black" } },
      { name: "color", type: "button", title: "Blue", hook: "color", value: "blue", row: 0, column: 1, state: { color: "blue" } },
      { name: "color", type: "button", title: "Green", hook: "color", value: "green", row: 0, column: 2, state: { color: "green" } },
      { name: "smooth", type: "slider", title: "Smooth", hook: "smooth", min: 0, max: 100, value: 50, row: 1, start_column: 0, end_column: 2 },
      { name: "pen_width", type: "slider", title: "Width", hook: "pen_width", min: 1, max: 50, value: 5, row: 2, start_column: 0, end_column: 2 },
      { name: "openAdvBrush", type: "subwindow", title: "压感曲线...", hook: "openDrawSettings", row: 3, start_column: 0, end_column: 2 },
    ],
  },
  eraser: {
    row_spacing: 4,
    column_spacing: 6,
    controls: [
      {
        name: "eraser_mode",
        type: "list",
        title: "Eraser Mode",
        hook: "eraser_mode",
        value: "area",
        row: 0,
        start_column: 0,
        end_column: 2,
        options: [
          { title: "LineMode", value: "line" },
          { title: "AreaMode", value: "area" },
          { title: "CutMode", value: "cut" },
        ],
      },
    ],
  },
  fill: {
    row_spacing: 4,
    column_spacing: 6,
    controls: [
      { name: "color", type: "button", title: "Black", hook: "color", value: "black", row: 0, column: 0, state: { color: "black" } },
      { name: "color", type: "button", title: "Blue", hook: "color", value: "blue", row: 0, column: 1, state: { color: "blue" } },
      { name: "color", type: "button", title: "Green", hook: "color", value: "green", row: 0, column: 2, state: { color: "green" } },
      {
        name: "fill_scope",
        type: "list",
        title: "Fill Scope",
        hook: "fill_scope",
        value: "current",
        row: 1,
        start_column: 0,
        end_column: 2,
        options: [
          { title: "ALL", value: "all" },
          { title: "Current", value: "current" },
        ],
      },
    ],
  },
  shape: {
    row_spacing: 4,
    column_spacing: 6,
    controls: [
      {
        name: "connect_auto_snap",
        type: "check",
        title: "Auto Snap",
        hook: "connect_auto_snap",
        value: "on",
        row: 0,
        start_column: 0,
        end_column: 2,
      },
      {
        name: "connect_mode",
        type: "list",
        title: "Connect Mode",
        hook: "connect_mode",
        value: "poly",
        row: 1,
        start_column: 0,
        end_column: 2,
        options: [
          { title: "PolyMode", value: "poly" },
          { title: "CurveMode", value: "curve" },
          { title: "SmoothMode", value: "smooth" },
        ],
      },
      {
        name: "connect_smooth",
        type: "slider",
        title: "Smooth",
        hook: "connect_smooth",
        min: 0,
        max: 100,
        value: 50,
        row: 2,
        start_column: 0,
        end_column: 2,
        visible_when: { name: "connect_mode", values: ["smooth"] },
      },
      {
        name: "connect_auto_accept",
        type: "check",
        title: "Auto Accept",
        hook: "connect_auto_accept",
        value: "off",
        row: 3,
        start_column: 0,
        end_column: 2,
      },
    ],
  },
  select: {
    row_spacing: 4,
    column_spacing: 6,
    controls: [
      {
        name: "select_mode",
        type: "list",
        title: "Lasso Mode",
        hook: "select_mode",
        value: "free",
        row: 0,
        start_column: 0,
        end_column: 2,
        options: [
          { title: "FreeLasso", value: "free" },
          { title: "PolyLasso", value: "poly" },
          { title: "RectSelect", value: "rect" },
        ],
      },
      { name: "feather_px", type: "slider", title: "Feather", hook: "feather_px", min: 0, max: 50, value: 0, row: 1, start_column: 0, end_column: 2 },
    ],
  },
  move: {
    row_spacing: 4,
    column_spacing: 6,
    controls: [
      {
        name: "transfer_constrain",
        type: "list",
        title: "Alt / Ctrl / Shift",
        hook: "transfer_constrain",
        value: "on",
        row: 0,
        start_column: 0,
        end_column: 2,
        options: [
          { title: "Keep ratio / lock axis", value: "on" },
          { title: "No constraint", value: "off" },
        ],
      },
    ],
  },
  eyedropper: {
    row_spacing: 4,
    column_spacing: 6,
    controls: [
      {
        name: "palette_color",
        type: "palette",
        title: "Palette",
        hook: "palette_color",
        value: "#111827",
        swatches: ["#111827", "#2563eb", "#16a34a", "#dc2626", "#f59e0b"],
        row: 0,
        start_column: 0,
        end_column: 2,
      },
      {
        name: "sample_source",
        type: "list",
        title: "Sample Source",
        hook: "sample_source",
        value: "all",
        row: 1,
        start_column: 0,
        end_column: 2,
        options: [
          { title: "AllLayers", value: "all" },
          { title: "CurrentLayer", value: "current" },
        ],
      },
    ],
  },
  h_center_line: automappingControls(true),
  v_center_line: automappingControls(true),
  additional_line: automappingControls(true),
  mapping_area: automappingControls(false),
  auto_mapping_2: automappingControls(false),
};

export default function syncToolOptions(state) {
  const tool = state.project?.activeTool ?? "brush";
  const schema = SCHEMAS[tool] ?? SCHEMAS.brush;
  const values = {};
  for (const ctrl of schema.controls) {
    if (ctrl.value !== undefined && values[ctrl.name] === undefined) {
      values[ctrl.name] = ctrl.value;
    }
  }
  return {
    "toolOptions.schema": schema,
    "toolOptions.values": values,
    "toolOptions.lastHook": "switch:" + tool,
  };
}
