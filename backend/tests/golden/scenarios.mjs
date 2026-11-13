// Qt modifier bitmasks expected by BackendSession::handleInputMessage.
export const MOD_NONE = 0;
export const MOD_SHIFT = 0x02000000;
export const MOD_CTRL = 0x04000000;
export const MOD_ALT = 0x08000000;

export const BTN_NONE = 0;
export const BTN_LEFT = 1;
export const BTN_RIGHT = 2;
export const BTN_MIDDLE = 4;

export function callStep(name, method, params = {}) {
  return { name, kind: "call", method, params };
}

export function inputStep(name, events) {
  return { name, kind: "inputs", events };
}

export function strokeStep(name, view, points, opts = {}) {
  const button = opts.button ?? BTN_LEFT;
  const modifiers = opts.modifiers ?? MOD_NONE;
  const pressures = opts.pressures ?? null;
  const moveWaitMs = opts.moveWaitMs ?? 45;
  const events = [];

  for (let i = 0; i < points.length; i++) {
    const [x, y] = points[i];
    const pressure = pressures ? pressures[Math.min(i, pressures.length - 1)] : (opts.pressure ?? 0.7);
    if (i === 0) {
      events.push({
        view,
        kind: "press",
        x,
        y,
        button,
        buttons: button,
        modifiers,
        pressure,
        waitAfterMs: moveWaitMs,
      });
    } else {
      events.push({
        view,
        kind: "move",
        x,
        y,
        button: BTN_NONE,
        buttons: button,
        modifiers,
        pressure,
        waitAfterMs: moveWaitMs,
      });
    }
  }

  const [lastX, lastY] = points[points.length - 1];
  const lastPressure = pressures
    ? pressures[pressures.length - 1]
    : (opts.pressure ?? 0.5);
  events.push({
    view,
    kind: "release",
    x: lastX,
    y: lastY,
    button,
    buttons: BTN_NONE,
    modifiers,
    pressure: lastPressure,
    waitAfterMs: 20,
  });

  return { name, kind: "inputs", events };
}

export function holdStrokeStep(name, view, preHoldPoints, postHoldPoints, opts = {}) {
  const button = BTN_LEFT;
  const preMod = opts.preModifiers ?? MOD_NONE;
  const postMod = opts.postModifiers ?? preMod;
  const holdMs = opts.holdMs ?? 1620;
  const moveWaitMs = opts.moveWaitMs ?? 45;
  const events = [];

  for (let i = 0; i < preHoldPoints.length; i++) {
    const [x, y] = preHoldPoints[i];
    const isLastPre = i === preHoldPoints.length - 1;
    events.push({
      view,
      kind: i === 0 ? "press" : "move",
      x,
      y,
      button: i === 0 ? button : BTN_NONE,
      buttons: button,
      modifiers: preMod,
      pressure: 0.75,
      waitAfterMs: isLastPre ? holdMs : moveWaitMs,
    });
  }

  for (let i = 0; i < postHoldPoints.length; i++) {
    const [x, y] = postHoldPoints[i];
    events.push({
      view,
      kind: "move",
      x,
      y,
      button: BTN_NONE,
      buttons: button,
      modifiers: postMod,
      pressure: 0.75,
      waitAfterMs: moveWaitMs,
    });
  }

  const endPt = postHoldPoints.length > 0
    ? postHoldPoints[postHoldPoints.length - 1]
    : preHoldPoints[preHoldPoints.length - 1];
  events.push({
    view,
    kind: "release",
    x: endPt[0],
    y: endPt[1],
    button,
    buttons: BTN_NONE,
    modifiers: postMod,
    pressure: 0.5,
    waitAfterMs: 20,
  });

  return { name, kind: "inputs", events };
}

export function clickStep(name, view, x, y, opts = {}) {
  const button = opts.button ?? BTN_LEFT;
  const modifiers = opts.modifiers ?? MOD_NONE;
  return {
    name,
    kind: "inputs",
    events: [
      {
        view,
        kind: "press",
        x,
        y,
        button,
        buttons: button,
        modifiers,
        pressure: 0.8,
        waitAfterMs: 45,
      },
      {
        view,
        kind: "release",
        x,
        y,
        button,
        buttons: BTN_NONE,
        modifiers,
        pressure: 0.5,
        waitAfterMs: 20,
      },
    ],
  };
}

export function hoverStep(name, view, points, opts = {}) {
  const modifiers = opts.modifiers ?? MOD_NONE;
  const moveWaitMs = opts.moveWaitMs ?? 45;
  return {
    name,
    kind: "inputs",
    events: points.map(([x, y]) => ({
      view,
      kind: "move",
      x,
      y,
      button: BTN_NONE,
      buttons: BTN_NONE,
      modifiers,
      pressure: 0.0,
      waitAfterMs: moveWaitMs,
    })),
  };
}

export function buildScenarios(ctx) {
  const { tmpDir, repoRoot } = ctx;
  const savedProjectPath = `${tmpDir}/golden_project.anproj`;
  const savedTexturePath = `${tmpDir}/golden_texture.textureview`;
  const exportedTexturePng = `${tmpDir}/golden_texture_export.png`;
  const generatedRasterPng = `${tmpDir}/golden_raster_sample.png`;
  const samplePliPath = `${repoRoot}/test_document/A.pli`;
  const sampleClipPath = `${repoRoot}/test_document/bigA.clip`;
  const sampleTextureViewPath = `${repoRoot}/test_document/texture.textureview`;

  return [
    {
      name: "startup",
      isStartup: true,
      steps: [],
    },

    {
      name: "pen",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("select_pen", "tool.select", { tool: "Pen" }),
        callStep("set_color_red", "color.set", { hex: "#e53935", a: 255 }),
        callStep("set_width_4", "draw.setPenWidth", { width: 4 }),
        callStep("set_smooth_20", "draw.setSmooth", { value: 20 }),
        callStep("set_fit_sharp", "draw.setSettings", { simplify: 20, corner: 80 }),
        strokeStep(
          "draw_curve_pressure",
          "main",
          [
            [80, 120],
            [140, 90],
            [210, 130],
            [280, 100],
            [350, 125],
          ],
          { pressures: [0.25, 0.55, 0.85, 0.95, 0.4] }
        ),
        callStep("set_color_blue", "color.set", { r: 30, g: 136, b: 229, a: 220 }),
        callStep("set_width_10", "draw.setPenWidth", { width: 10 }),
        callStep("set_smooth_75", "draw.setSmooth", { value: 75 }),
        callStep("set_fit_smooth", "draw.setSettings", { simplify: 85, corner: 15 }),
        strokeStep(
          "draw_heavy_stabilized_wave",
          "main",
          [
            [80, 220],
            [150, 180],
            [220, 250],
            [290, 185],
            [360, 230],
          ],
          { pressures: [0.4, 0.7, 0.9, 0.7, 0.3] }
        ),
        callStep("set_color_green", "color.set", { hex: "#43a047", a: 255 }),
        callStep("set_width_3", "draw.setPenWidth", { width: 3 }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        strokeStep(
          "draw_shift_horizontal_lock",
          "main",
          [
            [100, 320],
            [150, 323],
            [230, 328],
            [320, 315],
          ],
          { modifiers: MOD_SHIFT }
        ),
        strokeStep(
          "draw_alt_vertical_lock",
          "main",
          [
            [450, 120],
            [453, 180],
            [447, 260],
            [455, 340],
          ],
          { modifiers: MOD_ALT }
        ),
        callStep("new_independent_fill_layer", "layer.contextAction", {
          view: "main",
          layer: 0,
          id: "script:new_fill_layer",
        }),
        callStep("set_fill_brush_color", "color.set", { hex: "#ab47bc", a: 200 }),
        callStep("set_width_12", "draw.setPenWidth", { width: 12 }),
        strokeStep(
          "draw_pen_on_fill_layer",
          "main",
          [
            [520, 180],
            [600, 180],
            [600, 260],
            [520, 260],
          ]
        ),
      ],
    },

    {
      name: "pen_hold_line",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("select_pen", "tool.select", { tool: "Pen" }),
        callStep("set_color_dark", "color.set", { hex: "#222222", a: 255 }),
        callStep("set_width_4", "draw.setPenWidth", { width: 4 }),
        callStep("set_smooth_30", "draw.setSmooth", { value: 30 }),
        holdStrokeStep(
          "hold_still_collapse_to_straight_line",
          "main",
          [
            [100, 180],
            [180, 110],
            [260, 210],
          ],
          [
            [360, 160],
            [440, 190],
          ],
          { holdMs: 1620 }
        ),
        holdStrokeStep(
          "hold_still_with_shift_snap",
          "main",
          [
            [100, 340],
            [180, 280],
            [260, 370],
          ],
          [
            [420, 350],
          ],
          { holdMs: 1620, postModifiers: MOD_SHIFT }
        ),
      ],
    },

    {
      name: "eraser",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("select_pen", "tool.select", { tool: "Pen" }),
        callStep("set_width_4", "draw.setPenWidth", { width: 4 }),
        strokeStep("draw_line_for_area_erase", "main", [
          [100, 100],
          [250, 100],
          [400, 100],
        ], { modifiers: MOD_SHIFT }),
        strokeStep("draw_line_for_line_erase", "main", [
          [100, 180],
          [250, 180],
          [400, 180],
        ], { modifiers: MOD_SHIFT }),
        strokeStep("draw_grid_h1", "main", [
          [120, 280],
          [280, 280],
          [440, 280],
        ], { modifiers: MOD_SHIFT }),
        strokeStep("draw_grid_h2", "main", [
          [120, 380],
          [280, 380],
          [440, 380],
        ], { modifiers: MOD_SHIFT }),
        strokeStep("draw_grid_v1", "main", [
          [200, 220],
          [200, 330],
          [200, 440],
        ], { modifiers: MOD_ALT }),
        strokeStep("draw_grid_v2", "main", [
          [340, 220],
          [340, 330],
          [340, 440],
        ], { modifiers: MOD_ALT }),
        callStep("select_eraser", "tool.select", { tool: "Eraser" }),
        callStep("set_eraser_area_mode", "tool.eraserMode", { mode: "AreaMode" }),
        callStep("set_eraser_size_16", "tool.option", {
          name: "eraser_size",
          type: "int",
          value: 16,
          hook: "tool_settings",
        }),
        strokeStep("erase_area_across_first_line", "main", [
          [250, 70],
          [250, 100],
          [250, 130],
        ]),
        callStep("set_eraser_line_mode", "tool.eraserMode", { mode: "LineMode" }),
        strokeStep("erase_whole_second_line", "main", [
          [250, 160],
          [250, 180],
          [250, 200],
        ]),
        callStep("set_eraser_cut_mode", "tool.eraserMode", { mode: "CutMode" }),
        clickStep("cut_overhang_left_h1", "main", 150, 280),
        clickStep("cut_interior_span_v1", "main", 200, 330),
        callStep("select_delete_line_direct", "tool.select", { tool: "DeleteLine" }),
        clickStep("delete_grid_h2_direct", "main", 270, 380),
        callStep("select_cut_line_direct", "tool.select", { tool: "CutLine" }),
        clickStep("cut_overhang_top_v2_direct", "main", 340, 240),
      ],
    },

    {
      name: "fill",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("select_pen", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        callStep("set_width_3", "draw.setPenWidth", { width: 3 }),
        strokeStep("draw_closed_box", "main", [
          [150, 150],
          [350, 150],
          [350, 330],
          [150, 330],
          [150, 150],
        ]),
        callStep("select_fill", "tool.select", { tool: "Fill" }),
        callStep("set_fill_scope_current", "fill.setScope", { scope: "current" }),
        callStep("set_fill_orange", "color.set", { hex: "#ff7043", a: 255 }),
        clickStep("fill_closed_box_auto_creates_fill_layer", "main", 250, 240),
        callStep("set_fill_cyan", "color.set", { hex: "#29b6f6", a: 255 }),
        clickStep("fill_repeat_click_same_region_recolors", "main", 250, 240),
        callStep("add_second_line_layer", "layer.add", { view: "main" }),
        callStep("select_pen_for_divider", "tool.select", { tool: "Pen" }),
        callStep("set_line_black", "color.set", { hex: "#111111", a: 255 }),
        strokeStep("draw_vertical_divider_on_layer2", "main", [
          [250, 130],
          [250, 240],
          [250, 350],
        ], { modifiers: MOD_ALT }),
        callStep("select_fill_all_scope", "tool.select", { tool: "Fill" }),
        callStep("set_fill_scope_all", "fill.setScope", { scope: "all" }),
        callStep("set_fill_green", "color.set", { hex: "#66bb6a", a: 255 }),
        clickStep("fill_left_half_all_layers", "main", 200, 240),
        callStep("set_fill_yellow", "color.set", { hex: "#ffee58", a: 255 }),
        clickStep("fill_right_half_all_layers", "main", 300, 240),
        callStep("context_menu_on_tracked_fill", "layer.contextMenu", {
          view: "main",
          layer: 3,
        }),
        callStep("detach_fill_to_independent_layer", "layer.contextAction", {
          view: "main",
          layer: 3,
          id: "script:fill_to_independent",
        }),
        callStep("select_independent_fill_layer", "layer.select", { view: "main", index: 3 }),
        callStep("select_eraser_on_fill_layer", "tool.select", { tool: "Eraser" }),
        clickStep("erase_region_on_independent_fill_layer", "main", 250, 240),
      ],
    },

    {
      name: "arrow",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("select_pen", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        callStep("set_width_4", "draw.setPenWidth", { width: 4 }),
        strokeStep("draw_curve_for_arrow", "main", [
          [150, 260],
          [230, 170],
          [320, 260],
          [410, 180],
        ]),
        callStep("select_arrow", "tool.select", { tool: "Arrow" }),
        callStep("set_arrow_default_mode", "tool.option", {
          name: "edit_mode",
          type: "combo",
          value: "Default",
          hook: "tool_settings",
        }),
        strokeStep("pick_and_drag_stroke_default_mode", "main", [
          [230, 170],
          [250, 195],
          [270, 210],
        ]),
        callStep("set_arrow_artist_mode", "tool.option", {
          name: "edit_mode",
          type: "combo",
          value: "Artist",
          hook: "tool_settings",
        }),
        clickStep("pick_stroke_artist_mode", "main", 270, 210),
        strokeStep("drag_start_anchor_artist_mode", "main", [
          [190, 300],
          [175, 325],
          [160, 340],
        ]),
        inputStep("zoom_in_artist_mode_adapts_handles", [
          {
            view: "main",
            kind: "wheel",
            x: 270,
            y: 210,
            dx: 0,
            dy: 120,
            buttons: BTN_NONE,
            modifiers: MOD_NONE,
            waitAfterMs: 45,
          },
        ]),
        callStep("reset_view_after_artist_zoom", "board.resetView", { view: "main" }),
        callStep("set_arrow_debug_mode", "tool.option", {
          name: "edit_mode",
          type: "combo",
          value: "Debug",
          hook: "tool_settings",
        }),
        clickStep("pick_stroke_debug_mode", "main", 270, 210),
        strokeStep("drag_start_anchor_debug_mode", "main", [
          [160, 340],
          [145, 315],
          [135, 295],
        ]),
      ],
    },

    {
      name: "transfer",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("select_pen", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        callStep("set_width_3", "draw.setPenWidth", { width: 3 }),
        strokeStep("draw_top_bar", "main", [
          [200, 150],
          [300, 150],
          [400, 150],
        ], { modifiers: MOD_SHIFT }),
        strokeStep("draw_bottom_bar", "main", [
          [200, 350],
          [300, 350],
          [400, 350],
        ], { modifiers: MOD_SHIFT }),
        callStep("select_transfer", "tool.select", { tool: "Transfer" }),
        hoverStep("hover_grip_body_and_rotate_ring", "main", [
          [400, 350],
          [300, 250],
          [412, 362],
        ]),
        strokeStep("scale_from_se_grip", "main", [
          [400, 350],
          [425, 375],
          [450, 400],
        ]),
        callStep("enable_transfer_constrain", "tool.option", {
          name: "transfer_constrain",
          type: "bool",
          value: true,
          hook: "tool_settings",
        }),
        strokeStep("translate_inside_box", "main", [
          [325, 275],
          [350, 290],
          [375, 305],
        ]),
        callStep("disable_transfer_constrain", "tool.option", {
          name: "transfer_constrain",
          type: "bool",
          value: false,
          hook: "tool_settings",
        }),
        strokeStep("rotate_from_outer_se_ring", "main", [
          [512, 442],
          [490, 460],
          [460, 470],
        ]),
      ],
    },

    {
      name: "connect",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("select_pen", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        callStep("set_width_3", "draw.setPenWidth", { width: 3 }),
        strokeStep("draw_left_segment", "main", [
          [120, 200],
          [190, 200],
          [260, 200],
        ], { modifiers: MOD_SHIFT }),
        strokeStep("draw_right_segment", "main", [
          [310, 220],
          [385, 220],
          [460, 220],
        ], { modifiers: MOD_SHIFT }),
        callStep("select_connect", "tool.select", { tool: "Connect" }),
        callStep("set_connect_poly_mode", "tool.option", {
          name: "connect_mode",
          type: "mode",
          value: "PolyMode",
          hook: "tool_settings",
        }),
        hoverStep("hover_first_endpoint_autosnap", "main", [[263, 202]]),
        clickStep("pick_first_endpoint", "main", 263, 202),
        hoverStep("hover_second_endpoint_autosnap", "main", [[307, 219]]),
        clickStep("pick_second_endpoint_creates_pending", "main", 307, 219),
        callStep("switch_pending_to_curve_mode", "tool.option", {
          name: "connect_mode",
          type: "mode",
          value: "CurveMode",
          hook: "tool_settings",
        }),
        callStep("switch_pending_to_smooth_mode", "tool.option", {
          name: "connect_mode",
          type: "mode",
          value: "SmoothMode",
          hook: "tool_settings",
        }),
        callStep("adjust_connect_smooth_slider", "tool.option", {
          name: "connect_smooth",
          type: "int",
          value: 80,
          hook: "tool_settings",
        }),
        clickStep("click_connect_accept_badge", "main", 267, 192),
        callStep("disable_connect_auto_snap", "tool.option", {
          name: "connect_auto_snap",
          type: "bool",
          value: false,
          hook: "tool_settings",
        }),
        callStep("enable_connect_auto_accept", "tool.option", {
          name: "connect_auto_accept",
          type: "bool",
          value: true,
          hook: "tool_settings",
        }),
        clickStep("pick_free_point_a", "main", 180, 320),
        clickStep("pick_free_point_b_auto_accepted", "main", 340, 340),
        callStep("restore_connect_auto_snap", "tool.option", {
          name: "connect_auto_snap",
          type: "bool",
          value: true,
          hook: "tool_settings",
        }),
        callStep("restore_connect_auto_accept", "tool.option", {
          name: "connect_auto_accept",
          type: "bool",
          value: false,
          hook: "tool_settings",
        }),
      ],
    },

    {
      name: "history_and_alternating_views",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("select_pen", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        strokeStep("main_stroke_1", "main", [
          [100, 120],
          [240, 120],
        ]),
        callStep("activate_child_view", "view.setActive", { view: "child" }),
        strokeStep("child_stroke_1", "child", [
          [60, 80],
          [200, 80],
        ]),
        callStep("activate_main_view", "view.setActive", { view: "main" }),
        strokeStep("main_stroke_2", "main", [
          [100, 200],
          [240, 200],
        ]),
        callStep("activate_child_view_again", "view.setActive", { view: "child" }),
        strokeStep("child_stroke_2", "child", [
          [60, 160],
          [200, 160],
        ]),
        callStep("undo_1_child", "history.undo", {}),
        callStep("undo_2_main", "history.undo", {}),
        callStep("redo_1_main", "history.redo", {}),
        callStep("redo_2_child", "history.redo", {}),
        callStep("history_goto_main_index_1", "history.goTo", { view: "main", index: 1 }),
        callStep("history_goto_child_index_1", "history.goTo", { view: "child", index: 1 }),
        callStep("history_goto_main_index_2", "history.goTo", { view: "main", index: 2 }),
      ],
    },

    {
      name: "layers",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("layer_add_1", "layer.add", { view: "main" }),
        callStep("layer_add_2", "layer.add", { view: "main" }),
        callStep("layer_add_3", "layer.add", { view: "main" }),
        callStep("layer_select_0", "layer.select", { view: "main", index: 0 }),
        callStep("layer_set_invisible_1", "layer.setVisible", {
          view: "main",
          index: 1,
          visible: false,
        }),
        callStep("layer_set_visible_1", "layer.setVisible", {
          view: "main",
          index: 1,
          visible: true,
        }),
        callStep("layer_move_2_to_0", "layer.move", {
          view: "main",
          fromIndex: 2,
          toIndex: 0,
        }),
        callStep("layer_delete_1", "layer.delete", { view: "main", index: 1 }),
        callStep("layer_unselect", "layer.unselect", { view: "main" }),
        callStep("layer_select_0_again", "layer.select", { view: "main", index: 0 }),
        callStep("layer_context_menu_0", "layer.contextMenu", {
          view: "main",
          layer: 0,
        }),
        callStep("context_action_new_line_layer", "layer.contextAction", {
          view: "main",
          layer: 0,
          id: "script:new_line_layer",
        }),
        callStep("context_action_new_fill_layer", "layer.contextAction", {
          view: "main",
          layer: 0,
          id: "script:new_fill_layer",
        }),
        callStep("context_action_new_am_layer_creates_group", "layer.contextAction", {
          view: "main",
          layer: 0,
          id: "script:new_automapping_layer",
        }),
        callStep("collapse_group", "layer.setCollapsed", {
          view: "main",
          groupId: 1,
          collapsed: true,
        }),
        callStep("expand_group", "layer.setCollapsed", {
          view: "main",
          groupId: 1,
          collapsed: false,
        }),
        callStep("context_menu_group_header", "layer.contextMenu", {
          view: "main",
          groupId: 1,
          groupName: "Auto-Mapping Layer",
        }),
        callStep("dissolve_group", "layer.dissolveGroup", {
          view: "main",
          groupId: 1,
        }),
        callStep("create_second_am_group", "layer.contextAction", {
          view: "main",
          layer: 0,
          id: "script:new_automapping_layer",
        }),
        callStep("delete_group", "layer.deleteGroup", {
          view: "main",
          groupId: 2,
        }),
      ],
    },

    {
      name: "frames_timeline_onion",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("select_pen", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        strokeStep("draw_frame_0_art", "main", [
          [120, 180],
          [240, 180],
        ]),
        callStep("duplicate_frame_0_creates_named_1a", "frame.duplicate", { index: 0 }),
        strokeStep("draw_frame_1_extra_art", "main", [
          [120, 240],
          [240, 240],
        ]),
        callStep("add_hold_frame", "frame.addHold", { index: 1 }),
        callStep("add_blank_frame", "frame.add", { index: 2 }),
        strokeStep("draw_frame_3_art", "main", [
          [120, 300],
          [240, 300],
        ]),
        callStep("move_frame_1_to_3", "frame.move", { fromIndex: 1, toIndex: 3 }),
        callStep("select_frame_0", "frame.select", { index: 0 }),
        callStep("step_frame_forward", "frame.step", { delta: 1 }),
        callStep("step_frame_backward", "frame.step", { delta: -1 }),
        callStep("delete_frame_2", "frame.delete", { index: 2 }),
        callStep("set_fps_1_for_deterministic_playback", "frame.setFps", { fps: 1 }),
        callStep("set_playback_loop_false", "playback.setLoop", { loop: false }),
        callStep("set_playback_loop_true", "playback.setLoop", { loop: true }),
        callStep("start_playback", "playback.start", {}),
        callStep("stop_playback", "playback.stop", {}),
        callStep("set_fps_12", "frame.setFps", { fps: 12 }),
        callStep("set_fps_24", "frame.setFps", { fps: 24 }),
        callStep("select_frame_1_for_onion", "frame.select", { index: 1 }),
        callStep("enable_onion_skin", "onion.setEnabled", { enabled: true }),
        callStep("toggle_onion_frame_0_on", "onion.toggleFrame", { index: 0, pinned: true }),
        callStep("toggle_onion_frame_2_on", "onion.toggleFrame", { index: 2, pinned: true }),
        callStep("onion_set_lines_off", "onion.setLines", { enabled: false }),
        callStep("onion_set_lines_on", "onion.setLines", { enabled: true }),
        callStep("onion_set_fills_off", "onion.setFills", { enabled: false }),
        callStep("onion_set_fills_on", "onion.setFills", { enabled: true }),
        callStep("onion_set_am_layers_off", "onion.setAmLayers", { enabled: false }),
        callStep("onion_set_am_layers_on", "onion.setAmLayers", { enabled: true }),
        callStep("onion_set_guide_lines_on", "onion.setGuideLines", { enabled: true }),
        callStep("onion_set_guide_lines_off", "onion.setGuideLines", { enabled: false }),
        callStep("toggle_onion_frame_0_off", "onion.toggleFrame", { index: 0, pinned: false }),
        callStep("disable_onion_skin", "onion.setEnabled", { enabled: false }),
      ],
    },

    {
      name: "view_and_cursor",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("select_pen", "tool.select", { tool: "Pen" }),
        inputStep("enter_and_focus_main", [
          { view: "main", kind: "enter", waitAfterMs: 20 },
          { view: "main", kind: "focus", waitAfterMs: 20 },
        ]),
        hoverStep("hover_pen_cursor", "main", [[250, 250]]),
        callStep("set_huge_pen_width_triggers_canvas_ring", "draw.setPenWidth", { width: 120 }),
        hoverStep("hover_huge_pen_cursor", "main", [[260, 260]]),
        callStep("restore_pen_width_4", "draw.setPenWidth", { width: 4 }),
        inputStep("wheel_zoom_in_main", [
          {
            view: "main",
            kind: "wheel",
            x: 400,
            y: 300,
            dx: 0,
            dy: 120,
            buttons: BTN_NONE,
            modifiers: MOD_NONE,
            waitAfterMs: 30,
          },
          {
            view: "main",
            kind: "wheel",
            x: 400,
            y: 300,
            dx: 0,
            dy: 120,
            buttons: BTN_NONE,
            modifiers: MOD_NONE,
            waitAfterMs: 30,
          },
        ]),
        inputStep("middle_button_pan_main", [
          {
            view: "main",
            kind: "press",
            x: 400,
            y: 300,
            button: BTN_MIDDLE,
            buttons: BTN_MIDDLE,
            modifiers: MOD_NONE,
            waitAfterMs: 30,
          },
          {
            view: "main",
            kind: "move",
            x: 340,
            y: 260,
            button: BTN_NONE,
            buttons: BTN_MIDDLE,
            modifiers: MOD_NONE,
            waitAfterMs: 30,
          },
          {
            view: "main",
            kind: "release",
            x: 340,
            y: 260,
            button: BTN_MIDDLE,
            buttons: BTN_NONE,
            modifiers: MOD_NONE,
            waitAfterMs: 20,
          },
        ]),
        callStep("scroll_viewport_h", "board.scroll", {
          view: "main",
          axis: "h",
          value: 40,
        }),
        callStep("scroll_viewport_v", "board.scroll", {
          view: "main",
          axis: "v",
          value: 30,
        }),
        inputStep("resize_main_dpr_1_25", [
          {
            view: "main",
            kind: "resize",
            w: 960,
            h: 640,
            dpr: 1.25,
            waitAfterMs: 40,
          },
        ]),
        inputStep("resize_main_dpr_1_0", [
          {
            view: "main",
            kind: "resize",
            w: 800,
            h: 600,
            dpr: 1.0,
            waitAfterMs: 40,
          },
        ]),
        callStep("reset_main_view", "board.resetView", { view: "main" }),
        inputStep("leave_and_blur_main", [
          { view: "main", kind: "leave", waitAfterMs: 20 },
          { view: "main", kind: "blur", waitAfterMs: 20 },
        ]),
      ],
    },

    {
      name: "texture_board",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("toggle_texture_view_window_off", "menu.trigger", {
          id: "windows.texture_view",
        }),
        callStep("toggle_texture_view_window_on", "menu.trigger", {
          id: "windows.texture_view",
        }),
        callStep("toggle_python_debug_window_on", "menu.trigger", {
          id: "windows.python_debug",
        }),
        callStep("toggle_python_debug_window_off", "menu.trigger", {
          id: "windows.python_debug",
        }),
        callStep("toggle_changeable_timeline_on", "menu.trigger", {
          id: "child.setting.changeableTimeline",
        }),
        callStep("toggle_changeable_timeline_off", "menu.trigger", {
          id: "child.setting.changeableTimeline",
        }),
        callStep("toggle_changeable_texture_off", "menu.trigger", {
          id: "child.setting.changeableTexture",
        }),
        callStep("toggle_changeable_texture_on", "menu.trigger", {
          id: "child.setting.changeableTexture",
        }),
        callStep("set_child_bg_black", "menu.trigger", {
          id: "child.setting.background.black",
        }),
        callStep("set_child_bg_transparent", "menu.trigger", {
          id: "child.setting.background.transparent",
        }),
        callStep("set_child_bg_white", "menu.trigger", {
          id: "child.setting.background.white",
        }),
        callStep("activate_child_view", "view.setActive", { view: "child" }),
        callStep("select_pen_on_child", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        strokeStep("draw_on_child_board", "child", [
          [80, 100],
          [180, 140],
          [280, 100],
        ]),
        inputStep("wheel_zoom_child_board", [
          {
            view: "child",
            kind: "wheel",
            x: 200,
            y: 200,
            dx: 0,
            dy: 120,
            buttons: BTN_NONE,
            modifiers: MOD_NONE,
            waitAfterMs: 30,
          },
        ]),
        inputStep("middle_pan_child_board", [
          {
            view: "child",
            kind: "press",
            x: 200,
            y: 200,
            button: BTN_MIDDLE,
            buttons: BTN_MIDDLE,
            modifiers: MOD_NONE,
            waitAfterMs: 30,
          },
          {
            view: "child",
            kind: "move",
            x: 160,
            y: 170,
            button: BTN_NONE,
            buttons: BTN_MIDDLE,
            modifiers: MOD_NONE,
            waitAfterMs: 30,
          },
          {
            view: "child",
            kind: "release",
            x: 160,
            y: 170,
            button: BTN_MIDDLE,
            buttons: BTN_NONE,
            modifiers: MOD_NONE,
            waitAfterMs: 20,
          },
        ]),
        callStep("reset_child_view", "board.resetView", { view: "child" }),
      ],
    },

    {
      name: "tool_options_and_menus",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("select_pen", "tool.select", { tool: "Pen" }),
        callStep("pen_option_size", "tool.option", {
          name: "pen_size",
          type: "int",
          value: 7,
          hook: "tool_settings",
        }),
        callStep("select_eraser_check_options", "tool.select", { tool: "Eraser" }),
        callStep("eraser_option_size", "tool.option", {
          name: "eraser_size",
          type: "int",
          value: 22,
          hook: "tool_settings",
        }),
        callStep("select_fill_check_options", "tool.select", { tool: "Fill" }),
        callStep("fill_option_refer_all", "tool.option", {
          name: "refer_mode",
          type: "combo",
          value: "ALL",
          hook: "tool_settings",
        }),
        callStep("fill_option_refer_current", "tool.option", {
          name: "refer_mode",
          type: "combo",
          value: "Current",
          hook: "tool_settings",
        }),
        callStep("select_arrow_check_options", "tool.select", { tool: "Arrow" }),
        callStep("select_connect_check_options", "tool.select", { tool: "Connect" }),
        callStep("select_transfer_check_options", "tool.select", { tool: "Transfer" }),
        callStep("menu_view_show_refer_rect_main", "menu.trigger", {
          id: "script:main:View:show_refer_rect",
        }),
        callStep("menu_view_refer_3x3_main", "menu.trigger", {
          id: "script:main:View:refer_3x3",
        }),
        callStep("menu_view_show_refer_rect_main_off", "menu.trigger", {
          id: "script:main:View:show_refer_rect",
        }),
        callStep("menu_view_show_refer_rect_child", "menu.trigger", {
          id: "script:child:View:show_refer_rect",
        }),
        callStep("menu_view_refer_4x4_child", "menu.trigger", {
          id: "script:child:View:refer_4x4",
        }),
        callStep("menu_view_show_refer_rect_child_off", "menu.trigger", {
          id: "script:child:View:show_refer_rect",
        }),
        callStep("menu_am_calc_distance", "menu.trigger", {
          id: "script:main:Auto Mapping:calc_distance",
        }),
        callStep("menu_am_calc_harmonic", "menu.trigger", {
          id: "script:main:Auto Mapping:calc_harmonic",
        }),
        callStep("menu_am_falloff_linear", "menu.trigger", {
          id: "script:main:Auto Mapping:falloff_linear",
        }),
        callStep("menu_am_falloff_smooth", "menu.trigger", {
          id: "script:main:Auto Mapping:falloff_smooth",
        }),
        callStep("menu_am_constrain_outline_toggle", "menu.trigger", {
          id: "script:main:Auto Mapping:constrain_outline",
        }),
        callStep("menu_am_constrain_outline_restore", "menu.trigger", {
          id: "script:main:Auto Mapping:constrain_outline",
        }),
        callStep("settings_open_draw_settings", "menu.trigger", {
          id: "settings:draw_settings",
        }),
        callStep("settings_option_draw_simplify", "settings.option", {
          id: "draw_settings",
          name: "simplify",
          type: "int",
          value: 60,
          hook: "draw_settings",
        }),
        callStep("settings_option_draw_corner", "settings.option", {
          id: "draw_settings",
          name: "corner",
          type: "int",
          value: 40,
          hook: "draw_settings",
        }),
        callStep("settings_open_auto_mapping_lines", "menu.trigger", {
          id: "settings:auto_mapping_lines",
        }),
        callStep("settings_option_am_line_width", "settings.option", {
          id: "auto_mapping_lines",
          name: "guide_line_width",
          type: "int",
          value: 3,
          hook: "auto_mapping_line_settings",
        }),
        callStep("settings_open_outline_settings", "menu.trigger", {
          id: "settings:outline_settings",
        }),
      ],
    },

    {
      name: "auto_mapping",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("create_boundary_line_layer_on_main", "layer.contextAction", {
          view: "main",
          layer: -1,
          id: "script:new_line_layer",
        }),
        callStep("select_pen_for_main_boundary", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        callStep("set_width_3", "draw.setPenWidth", { width: 3 }),
        strokeStep("draw_closed_boundary_on_main", "main", [
          [220, 150],
          [580, 150],
          [580, 450],
          [220, 450],
          [220, 150],
        ]),
        callStep("create_auto_mapping_layer", "layer.contextAction", {
          view: "main",
          layer: 0,
          id: "script:new_automapping_layer",
        }),
        callStep("activate_child_for_texture", "view.setActive", { view: "child" }),
        callStep("select_pen_on_child", "tool.select", { tool: "Pen" }),
        callStep("set_pattern_color_blue", "color.set", { hex: "#1e88e5", a: 255 }),
        strokeStep("draw_closed_pattern_on_child", "child", [
          [120, 120],
          [280, 120],
          [280, 280],
          [120, 280],
          [120, 120],
        ]),
        strokeStep("draw_inner_cross_on_child", "child", [
          [150, 200],
          [250, 200],
        ]),
        callStep("select_fill_on_child", "tool.select", { tool: "Fill" }),
        callStep("set_fill_color_amber", "color.set", { hex: "#ffb300", a: 255 }),
        clickStep("fill_pattern_on_child", "child", 200, 165),
        callStep("select_h_center_line_child", "tool.selectExtra", {
          name: "h_center_line",
        }),
        strokeStep("draw_h_axis_on_child", "child", [
          [80, 200],
          [200, 200],
          [320, 200],
        ]),
        callStep("select_v_center_line_child", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_child", "child", [
          [200, 80],
          [200, 200],
          [200, 320],
        ]),
        callStep("select_mapping_area_child", "tool.selectExtra", {
          name: "mapping_area",
        }),
        clickStep("click_mapping_area_inside_child_pattern", "child", 200, 165),
        callStep("activate_main_for_axes", "view.setActive", { view: "main" }),
        callStep("select_am_unit_layer_on_main", "layer.select", {
          view: "main",
          index: 0,
        }),
        callStep("select_h_center_line_main", "tool.selectExtra", {
          name: "h_center_line",
        }),
        strokeStep("draw_h_axis_on_main", "main", [
          [240, 300],
          [400, 290],
          [560, 300],
        ]),
        callStep("select_v_center_line_main", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_main_triggers_automap", "main", [
          [400, 170],
          [400, 290],
          [400, 430],
        ]),
        callStep("select_mapping_area_main", "tool.selectExtra", {
          name: "mapping_area",
        }),
        clickStep("click_mapping_area_inside_main_boundary", "main", 350, 250),
        callStep("select_additional_line_tool", "tool.selectExtra", {
          name: "additional_line",
        }),
        strokeStep("draw_additional_line_on_child", "child", [
          [150, 150],
          [200, 220],
          [250, 150],
        ]),
        strokeStep("draw_additional_line_on_main_triggers_rerun", "main", [
          [300, 220],
          [400, 350],
          [500, 220],
        ]),
        strokeStep("drag_main_h_axis_triggers_live_rerun", "main", [
          [300, 296],
          [300, 315],
          [300, 330],
        ]),
        callStep("explicit_auto_mapping_action", "tool.selectExtra", {
          name: "auto_mapping_2",
        }),
        callStep("duplicate_auto_mapping_unit", "layer.contextAction", {
          view: "main",
          groupId: 1,
          id: "script:duplicate_automapping_unit",
        }),
        callStep("bake_auto_mapping_unit_to_editable", "layer.contextAction", {
          view: "main",
          groupId: 1,
          id: "script:to_editable_layer",
        }),
      ],
    },

    {
      name: "files",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("select_pen", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        strokeStep("draw_main_before_save", "main", [
          [100, 150],
          [300, 250],
        ]),
        strokeStep("draw_child_before_save", "child", [
          [80, 80],
          [220, 220],
        ]),
        callStep("save_project_as", "project.saveAs", { path: savedProjectPath }),
        callStep("save_texture_as", "file.texture", {
          kind: "saveAs",
          path: savedTexturePath,
        }),
        callStep("export_texture_image", "file.texture", {
          kind: "exportImage",
          path: exportedTexturePng,
        }),
        strokeStep("draw_extra_stroke_before_reopen", "main", [
          [400, 150],
          [550, 300],
        ]),
        callStep("reopen_saved_project", "project.open", { path: savedProjectPath }),
        callStep("reopen_saved_texture", "file.texture", {
          kind: "open",
          path: savedTexturePath,
        }),
        callStep("open_sample_textureview", "file.texture", {
          kind: "open",
          path: sampleTextureViewPath,
        }),
        callStep("import_generated_raster_png", "file.import", {
          view: "main",
          kind: "raster",
          path: generatedRasterPng,
        }),
        callStep("import_sample_pli", "file.import", {
          view: "main",
          kind: "pli",
          path: samplePliPath,
        }),
        callStep("import_sample_clip", "file.import", {
          view: "main",
          kind: "clip",
          path: sampleClipPath,
        }),
      ],
    },

    {
      name: "theme",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("theme_set_light", "theme.set", { mode: "light" }),
        callStep("theme_set_dark", "theme.set", { mode: "dark" }),
        callStep("menu_theme_light", "menu.trigger", { id: "theme.light" }),
        callStep("menu_theme_dark", "menu.trigger", { id: "theme.dark" }),
      ],
    },

    {
      name: "free_out_line",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("create_boundary_line_layer_on_main", "layer.contextAction", {
          view: "main",
          layer: -1,
          id: "script:new_line_layer",
        }),
        callStep("select_pen_for_main_boundary", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        callStep("set_width_3", "draw.setPenWidth", { width: 3 }),
        strokeStep("draw_closed_boundary_on_main", "main", [
          [220, 150],
          [580, 150],
          [580, 450],
          [220, 450],
          [220, 150],
        ]),
        callStep("create_auto_mapping_layer", "layer.contextAction", {
          view: "main",
          layer: 0,
          id: "script:new_automapping_layer",
        }),
        callStep("activate_child_for_texture", "view.setActive", { view: "child" }),
        callStep("select_pen_on_child", "tool.select", { tool: "Pen" }),
        callStep("set_pattern_color_blue", "color.set", { hex: "#1e88e5", a: 255 }),
        strokeStep("draw_closed_pattern_on_child", "child", [
          [120, 120],
          [280, 120],
          [280, 280],
          [120, 280],
          [120, 120],
        ]),
        callStep("select_h_center_line_child", "tool.selectExtra", {
          name: "h_center_line",
        }),
        strokeStep("draw_h_axis_on_child", "child", [
          [80, 200],
          [200, 200],
          [320, 200],
        ]),
        callStep("select_v_center_line_child", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_child", "child", [
          [200, 80],
          [200, 200],
          [200, 320],
        ]),
        callStep("select_free_out_line_on_child", "tool.selectExtra", {
          name: "free_out_line",
        }),
        strokeStep("draw_contour_on_child", "child", [
          [100, 100],
          [300, 100],
          [300, 300],
          [100, 300],
          [100, 100],
        ]),
        callStep("activate_main_for_contour", "view.setActive", { view: "main" }),
        callStep("select_am_unit_layer_on_main", "layer.select", {
          view: "main",
          index: 0,
        }),
        callStep("select_free_out_line_on_main", "tool.selectExtra", {
          name: "free_out_line",
        }),
        strokeStep("draw_contour_on_main_pairs_on_release", "main", [
          [210, 140],
          [590, 140],
          [590, 460],
          [210, 460],
          [210, 140],
        ]),
        clickStep("remove_main_contour_via_badge", "main", 601, 129),
        strokeStep("redraw_contour_on_main_pairs_again", "main", [
          [215, 145],
          [585, 145],
          [585, 455],
          [215, 455],
          [215, 145],
        ]),
      ],
    },

    {
      name: "choice_line",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("create_auto_mapping_layer", "layer.contextAction", {
          view: "main",
          layer: -1,
          id: "script:new_automapping_layer",
        }),
        callStep("activate_child_for_texture", "view.setActive", { view: "child" }),
        callStep("select_pen_on_child", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        strokeStep("draw_pattern_on_child", "child", [
          [120, 120],
          [280, 120],
          [280, 280],
          [120, 280],
          [120, 120],
        ]),
        callStep("select_h_center_line_child", "tool.selectExtra", {
          name: "h_center_line",
        }),
        strokeStep("draw_h_axis_on_child", "child", [
          [80, 200],
          [200, 200],
          [320, 200],
        ]),
        callStep("select_v_center_line_child", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_child", "child", [
          [200, 80],
          [200, 200],
          [200, 320],
        ]),
        callStep("select_free_out_line_child", "tool.selectExtra", {
          name: "free_out_line",
        }),
        strokeStep("draw_contour_on_child", "child", [
          [100, 100],
          [300, 100],
          [300, 300],
          [100, 300],
          [100, 100],
        ]),
        callStep("activate_main", "view.setActive", { view: "main" }),
        callStep("select_free_out_line_main", "tool.selectExtra", {
          name: "free_out_line",
        }),
        strokeStep("draw_contour_on_main", "main", [
          [210, 140],
          [590, 140],
          [590, 460],
          [210, 460],
          [210, 140],
        ]),
        callStep("select_additional_line_tool", "tool.selectExtra", {
          name: "additional_line",
        }),
        strokeStep("draw_additional_line_on_main", "main", [
          [280, 220],
          [400, 235],
          [520, 220],
        ]),
        callStep("select_choice_line_tool", "tool.selectExtra", {
          name: "choice_line",
        }),
        clickStep("select_h_axis_on_main", "main", 300, 300),
        callStep("set_ground_line_on_h_axis", "tool.option", {
          name: "ground_line",
          type: "check",
          value: "on",
          hook: "ground_line",
        }),
        callStep("unset_ground_line_on_h_axis", "tool.option", {
          name: "ground_line",
          type: "check",
          value: "off",
          hook: "ground_line",
        }),
        clickStep("select_additional_line_on_main", "main", 400, 235),
        callStep("set_ground_line_on_additional_line", "tool.option", {
          name: "ground_line",
          type: "check",
          value: "on",
          hook: "ground_line",
        }),
        clickStep("select_contour_on_main", "main", 210, 300),
        callStep("unset_ground_line_on_contour", "tool.option", {
          name: "ground_line",
          type: "check",
          value: "off",
          hook: "ground_line",
        }),
        callStep("restore_ground_line_on_contour", "tool.option", {
          name: "ground_line",
          type: "check",
          value: "on",
          hook: "ground_line",
        }),
        clickStep("click_blank_space_keeps_choice", "main", 50, 50),
        callStep("switch_to_pen_disarms_choice_halo", "tool.select", {
          tool: "Pen",
        }),
        callStep("switch_back_to_choice_line_restores_halo", "tool.selectExtra", {
          name: "choice_line",
        }),
        clickStep("reselect_additional_line_on_main", "main", 400, 235),
        clickStep("delete_selected_additional_line_clears_selection", "main", 531, 209),
      ],
    },

    {
      name: "deform_line",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("create_auto_mapping_layer", "layer.contextAction", {
          view: "main",
          layer: -1,
          id: "script:new_automapping_layer",
        }),
        callStep("activate_child_for_texture", "view.setActive", { view: "child" }),
        callStep("select_pen_on_child", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        strokeStep("draw_pattern_on_child", "child", [
          [120, 120],
          [280, 120],
          [280, 280],
          [120, 280],
          [120, 120],
        ]),
        callStep("select_h_center_line_child", "tool.selectExtra", {
          name: "h_center_line",
        }),
        strokeStep("draw_h_axis_on_child", "child", [
          [80, 200],
          [200, 200],
          [320, 200],
        ]),
        callStep("select_v_center_line_child", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_child", "child", [
          [200, 80],
          [200, 200],
          [200, 320],
        ]),
        callStep("select_free_out_line_child", "tool.selectExtra", {
          name: "free_out_line",
        }),
        strokeStep("draw_contour_on_child", "child", [
          [100, 100],
          [300, 100],
          [300, 300],
          [100, 300],
          [100, 100],
        ]),
        callStep("activate_main", "view.setActive", { view: "main" }),
        callStep("select_free_out_line_main", "tool.selectExtra", {
          name: "free_out_line",
        }),
        strokeStep("draw_contour_on_main_generates_auto_axes", "main", [
          [210, 140],
          [590, 140],
          [590, 460],
          [210, 460],
          [210, 140],
        ]),
        callStep("select_deform_line_tool", "tool.selectExtra", {
          name: "deform_line",
        }),
        clickStep("single_click_h_axis_does_not_alter_line", "main", 300, 300),
        callStep("set_deform_radius_90", "tool.option", {
          name: "deform_radius",
          type: "int",
          value: 90,
          hook: "deform_radius",
        }),
        strokeStep("drag_deform_h_axis_on_main", "main", [
          [320, 300],
          [320, 325],
          [320, 345],
        ]),
        strokeStep("drag_deform_contour_on_main", "main", [
          [400, 140],
          [400, 125],
          [400, 115],
        ]),
        inputStep("begin_drag_v_axis_without_release", "main", [
          { type: "press", x: 400, y: 220, button: "left", pressure: 1, modifiers: 0 },
          { type: "move", x: 435, y: 220, button: "left", buttons: 1, pressure: 1, modifiers: 0 },
        ]),
        callStep("undo_interrupts_active_deform_drag", "history.undo", {
          view: "main",
        }),
        inputStep("release_after_undo_interrupted_drag", "main", [
          { type: "release", x: 435, y: 220, button: "left", buttons: 0, pressure: 0, modifiers: 0 },
        ]),
      ],
    },

    {
      name: "additional_line",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("create_auto_mapping_layer", "layer.contextAction", {
          view: "main",
          layer: -1,
          id: "script:new_automapping_layer",
        }),
        callStep("activate_child_for_texture", "view.setActive", { view: "child" }),
        callStep("select_pen_on_child", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        strokeStep("draw_pattern_on_child", "child", [
          [120, 120],
          [280, 120],
          [280, 280],
          [120, 280],
          [120, 120],
        ]),
        callStep("select_h_center_line_child", "tool.selectExtra", {
          name: "h_center_line",
        }),
        strokeStep("draw_h_axis_on_child", "child", [
          [80, 200],
          [200, 200],
          [320, 200],
        ]),
        callStep("select_v_center_line_child", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_child", "child", [
          [200, 80],
          [200, 200],
          [200, 320],
        ]),
        callStep("activate_main", "view.setActive", { view: "main" }),
        callStep("select_h_center_line_main", "tool.selectExtra", {
          name: "h_center_line",
        }),
        strokeStep("draw_h_axis_on_main", "main", [
          [240, 300],
          [400, 300],
          [560, 300],
        ]),
        callStep("select_v_center_line_main", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_main", "main", [
          [400, 170],
          [400, 300],
          [400, 430],
        ]),
        callStep("select_additional_line_tool", "tool.selectExtra", {
          name: "additional_line",
        }),
        strokeStep("draw_additional_line_0_on_child", "child", [
          [140, 150],
          [200, 165],
          [260, 150],
        ]),
        strokeStep("redraw_additional_line_0_on_main", "main", [
          [280, 210],
          [400, 240],
          [520, 210],
        ]),
        strokeStep("draw_additional_line_1_on_main", "main", [
          [280, 390],
          [400, 365],
          [520, 390],
        ]),
        clickStep("delete_additional_line_0_via_main_badge", "main", 531, 199),
        clickStep("delete_additional_line_1_via_main_badge", "main", 531, 379),
        strokeStep("draw_additional_line_2_on_child", "child", [
          [145, 245],
          [200, 230],
          [255, 245],
        ]),
        callStep("run_auto_mapping_explicitly", "tool.selectExtra", {
          name: "auto_mapping_2",
        }),
      ],
    },

    {
      name: "mapping_area",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("create_boundary_line_layer_on_main", "layer.contextAction", {
          view: "main",
          layer: -1,
          id: "script:new_line_layer",
        }),
        callStep("select_pen_for_main_boundary", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        callStep("set_width_3", "draw.setPenWidth", { width: 3 }),
        strokeStep("draw_closed_boundary_on_main", "main", [
          [220, 150],
          [580, 150],
          [580, 450],
          [220, 450],
          [220, 150],
        ]),
        callStep("create_auto_mapping_layer", "layer.contextAction", {
          view: "main",
          layer: 0,
          id: "script:new_automapping_layer",
        }),
        callStep("activate_child_for_texture", "view.setActive", { view: "child" }),
        callStep("select_pen_on_child", "tool.select", { tool: "Pen" }),
        strokeStep("draw_closed_pattern_on_child", "child", [
          [120, 120],
          [280, 120],
          [280, 280],
          [120, 280],
          [120, 120],
        ]),
        callStep("select_h_center_line_child", "tool.selectExtra", {
          name: "h_center_line",
        }),
        strokeStep("draw_h_axis_on_child", "child", [
          [80, 200],
          [200, 200],
          [320, 200],
        ]),
        callStep("select_v_center_line_child", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_child", "child", [
          [200, 80],
          [200, 200],
          [200, 320],
        ]),
        callStep("select_mapping_area_child", "tool.selectExtra", {
          name: "mapping_area",
        }),
        clickStep("set_mapping_area_on_child", "child", 200, 165),
        callStep("activate_main_for_axes", "view.setActive", { view: "main" }),
        callStep("select_am_unit_layer_on_main", "layer.select", {
          view: "main",
          index: 0,
        }),
        callStep("select_h_center_line_main", "tool.selectExtra", {
          name: "h_center_line",
        }),
        strokeStep("draw_h_axis_on_main", "main", [
          [240, 300],
          [400, 300],
          [560, 300],
        ]),
        callStep("select_v_center_line_main", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_main", "main", [
          [400, 170],
          [400, 300],
          [400, 430],
        ]),
        callStep("select_mapping_area_main", "tool.selectExtra", {
          name: "mapping_area",
        }),
        clickStep("set_mapping_area_on_main", "main", 350, 250),
        callStep("run_mapping_with_areas", "tool.selectExtra", {
          name: "auto_mapping_2",
        }),
        clickStep("remove_main_mapping_area_via_badge", "main", 591, 139),
        clickStep("remove_child_mapping_area_via_badge", "child", 291, 109),
        callStep("run_mapping_after_removing_areas", "tool.selectExtra", {
          name: "auto_mapping_2",
        }),
      ],
    },

    {
      name: "fold_bubble",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("create_auto_mapping_layer", "layer.contextAction", {
          view: "main",
          layer: -1,
          id: "script:new_automapping_layer",
        }),
        callStep("activate_child_for_texture", "view.setActive", { view: "child" }),
        callStep("select_pen_on_child", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        strokeStep("draw_pattern_on_child", "child", [
          [120, 120],
          [280, 120],
          [280, 280],
          [120, 280],
          [120, 120],
        ]),
        callStep("select_h_center_line_child", "tool.selectExtra", {
          name: "h_center_line",
        }),
        strokeStep("draw_h_axis_on_child", "child", [
          [80, 200],
          [200, 200],
          [320, 200],
        ]),
        callStep("select_v_center_line_child", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_child", "child", [
          [200, 80],
          [200, 200],
          [200, 320],
        ]),
        callStep("activate_main", "view.setActive", { view: "main" }),
        callStep("select_v_center_line_main", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_main", "main", [
          [400, 150],
          [400, 300],
          [400, 450],
        ]),
        callStep("select_h_center_line_main", "tool.selectExtra", {
          name: "h_center_line",
        }),
        callStep("set_smooth_0_on_main", "draw.setSmooth", { value: 0 }),
        strokeStep("draw_folded_h_axis_on_main_shows_bubble", "main", [
          [240, 300],
          [400, 300],
          [540, 300],
          [560, 300],
          [560, 330],
          [520, 330],
          [480, 330],
          [430, 330],
        ]),
        callStep("run_auto_mapping_on_folded_axes", "tool.selectExtra", {
          name: "auto_mapping_2",
        }),
      ],
    },

    {
      name: "horizon_line",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("create_auto_mapping_layer", "layer.contextAction", {
          view: "main",
          layer: -1,
          id: "script:new_automapping_layer",
        }),
        callStep("trigger_horizon_line_on", "menu.trigger", {
          id: "script:main:auto_mapping:horizon",
        }),
        strokeStep("drag_horizon_line_down", "main", [
          [400, 300],
          [400, 260],
          [400, 220],
        ]),
        callStep("trigger_horizon_line_off", "menu.trigger", {
          id: "script:main:auto_mapping:horizon",
        }),
        callStep("trigger_horizon_line_on_again", "menu.trigger", {
          id: "script:main:auto_mapping:horizon",
        }),
      ],
    },

    {
      name: "to_3d",
      steps: [
        callStep("new_project", "project.new", { width: 800, height: 600 }),
        callStep("create_auto_mapping_layer", "layer.contextAction", {
          view: "main",
          layer: -1,
          id: "script:new_automapping_layer",
        }),
        callStep("activate_child_for_texture", "view.setActive", { view: "child" }),
        callStep("select_pen_on_child", "tool.select", { tool: "Pen" }),
        callStep("set_smooth_0", "draw.setSmooth", { value: 0 }),
        strokeStep("draw_pattern_on_child", "child", [
          [120, 120],
          [280, 120],
          [280, 280],
          [120, 280],
          [120, 120],
        ]),
        callStep("select_h_center_line_child", "tool.selectExtra", {
          name: "h_center_line",
        }),
        strokeStep("draw_h_axis_on_child", "child", [
          [80, 200],
          [200, 200],
          [320, 200],
        ]),
        callStep("select_v_center_line_child", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_child", "child", [
          [200, 80],
          [200, 200],
          [200, 320],
        ]),
        callStep("activate_main", "view.setActive", { view: "main" }),
        callStep("select_h_center_line_main", "tool.selectExtra", {
          name: "h_center_line",
        }),
        strokeStep("draw_h_axis_on_main", "main", [
          [240, 300],
          [400, 280],
          [560, 300],
        ]),
        callStep("select_v_center_line_main", "tool.selectExtra", {
          name: "v_center_line",
        }),
        strokeStep("draw_v_axis_on_main", "main", [
          [400, 170],
          [400, 280],
          [400, 430],
        ]),
        callStep("trigger_horizon_for_3d", "menu.trigger", {
          id: "script:main:auto_mapping:horizon",
        }),
        callStep("trigger_to_3d_menu", "menu.trigger", {
          id: "script:main:auto_mapping:to_3d",
        })
      ],
    },
  ];
}
