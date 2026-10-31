// 由结构化数据生成的动态表单控件。
function parseSchema(raw) {
  if (typeof raw === "string") {
    try {
      raw = JSON.parse(raw);
    } catch {
      return { row_spacing: 8, column_spacing: 6, controls: [] };
    }
  }
  if (Array.isArray(raw)) {
    return { row_spacing: 8, column_spacing: 6, controls: raw };
  }
  if (typeof raw === "object" && raw !== null) {
    return {
      row_spacing: Number(raw.row_spacing ?? 8),
      column_spacing: Number(raw.column_spacing ?? 6),
      controls: Array.isArray(raw.controls) ? raw.controls : [],
    };
  }
  return { row_spacing: 8, column_spacing: 6, controls: [] };
}

function parseValues(raw) {
  if (typeof raw === "string") {
    try {
      const parsed = JSON.parse(raw);
      return typeof parsed === "object" && parsed !== null ? parsed : {};
    } catch {
      return {};
    }
  }
  if (typeof raw === "object" && raw !== null && !Array.isArray(raw)) return raw;
  return {};
}

export function isCheckValueOn(val) {
  if (typeof val === "boolean") return val;
  if (typeof val === "number") return val !== 0;
  if (typeof val === "string") {
    const s = val.trim().toLowerCase();
    return s === "on" || s === "true" || s === "1" || s === "yes";
  }
  return false;
}

function evalStringVisibleWhen(expr, valMap) {
  const s = String(expr ?? "").trim();
  if (!s) return true;
  if (s.includes("||")) return s.split("||").some((p) => evalStringVisibleWhen(p, valMap));
  if (s.includes("&&")) return s.split("&&").every((p) => evalStringVisibleWhen(p, valMap));
  const neIdx = s.indexOf("!=");
  if (neIdx > 0) {
    const key = s.slice(0, neIdx).trim();
    const rhs = s.slice(neIdx + 2).trim().replace(/^['"]|['"]$/g, "");
    return String(valMap[key] ?? "") !== rhs;
  }
  const eqIdx = s.indexOf("==");
  if (eqIdx > 0) {
    const key = s.slice(0, eqIdx).trim();
    const rhs = s.slice(eqIdx + 2).trim().replace(/^['"]|['"]$/g, "");
    return String(valMap[key] ?? "") === rhs;
  }
  if (s.startsWith("!")) return !isCheckValueOn(valMap[s.slice(1).trim()]);
  return isCheckValueOn(valMap[s]);
}

export function isControlVisible(ctrl, valMap) {
  const cond = ctrl.visible_when;
  if (!cond) return true;
  if (typeof cond === "string") return evalStringVisibleWhen(cond, valMap);
  if (typeof cond !== "object") return true;
  const target = String(cond.name ?? cond.control ?? cond.field ?? "");
  if (!target) return true;
  const actual = valMap[target];
  if (Array.isArray(cond.values)) return cond.values.some((v) => String(v) === String(actual));
  if (cond.value !== undefined) return String(actual) === String(cond.value);
  if (cond.equals !== undefined) return String(actual) === String(cond.equals);
  if (cond.not_equals !== undefined) return String(actual) !== String(cond.not_equals);
  if (Array.isArray(cond.in)) return cond.in.some((v) => String(v) === String(actual));
  return true;
}

function resolveColorHex(raw) {
  const s = String(raw ?? "").trim().toLowerCase();
  if (s.startsWith("#") && (s.length === 4 || s.length === 7)) return s;
  const map = {
    black: "#1e1e1e",
    blue: "#2563eb",
    green: "#16a34a",
    red: "#dc2626",
    white: "#f8fafc",
    yellow: "#eab308",
  };
  return map[s] ?? "#4a90d9";
}

export default function create(el, { props, emit }) {
  const root = document.createElement("div");
  root.className = "vf-std-dataform";
  el.append(root);
  let localOverrides = {};
  let lastSchemaKey = "";
  let entries = [];
  let currentProps = props;

  function computeValMap(schema, next) {
    const extValues = parseValues(next.values);
    const valMap = {};
    for (const c of schema.controls) {
      if (!c || typeof c !== "object") continue;
      const name = String(c.name ?? "");
      if (!name) continue;
      if (extValues[name] !== undefined) valMap[name] = extValues[name];
      else if (localOverrides[name] !== undefined) valMap[name] = localOverrides[name];
      else if (valMap[name] === undefined) valMap[name] = c.value;
    }
    for (const [k, v] of Object.entries(extValues)) {
      if (valMap[k] === undefined) valMap[k] = v;
    }
    return valMap;
  }

  function syncAll(schema, next) {
    const valMap = computeValMap(schema, next);
    const formEnabled = next.enabled !== false;
    for (const entry of entries) {
      entry.sync(valMap, formEnabled);
    }
  }

  function emitField(c, val) {
    const name = String(c.name ?? "");
    localOverrides[name] = val;
    emit("change", {
      name,
      value: val,
      hook: String(c.hook ?? ""),
      type: String(c.type ?? ""),
    });
    const schema = parseSchema(currentProps.schema);
    syncAll(schema, currentProps);
  }

  function buildDom(schema, next) {
    root.replaceChildren();
    entries = [];
    if (schema.controls.length === 0) {
      const empty = document.createElement("div");
      empty.className = "vf-std-dataform-empty";
      empty.textContent = String(next.emptyText ?? "当前工具无可调选项");
      root.append(empty);
      return;
    }

    let maxCol = 1;
    const rowCounts = new Map();
    for (const c of schema.controls) {
      if (!c || typeof c !== "object") continue;
      const r = Number(c.row ?? 0);
      rowCounts.set(r, (rowCounts.get(r) ?? 0) + 1);
      const ec = Number(c.end_column ?? c.start_column ?? c.column ?? 1);
      if (ec > maxCol) maxCol = ec;
    }
    const colCount = Math.max(2, maxCol + 1);

    const grid = document.createElement("div");
    grid.className = "vf-std-dataform-grid";
    grid.style.gridTemplateColumns = `repeat(${colCount}, minmax(0, 1fr))`;
    grid.style.rowGap = `${Math.max(4, schema.row_spacing)}px`;
    grid.style.columnGap = `${Math.max(4, schema.column_spacing)}px`;

    for (const c of schema.controls) {
      if (!c || typeof c !== "object") continue;
      const stateStr = typeof c.state === "string" ? c.state : "normal";
      const stateColor =
        typeof c.state === "object" && c.state !== null && !Array.isArray(c.state)
          ? String(c.state.color ?? "")
          : "";
      const name = String(c.name ?? "");
      const title = String(c.title ?? name);
      const type = String(c.type ?? "button");
      const rawRow = Number(c.row ?? 0);
      const row = rawRow + 1;
      const startCol = Number(c.start_column ?? c.column ?? 0) + 1;
      let endCol = Number(c.end_column ?? c.start_column ?? c.column ?? 1) + 2;
      if ((rowCounts.get(rawRow) ?? 0) === 1 && startCol === 1 && endCol >= 3) {
        endCol = colCount + 1;
      }

      const cell = document.createElement("div");
      cell.className = "vf-std-dataform-cell";
      cell.dataset.vfField = name;
      if (c.value !== undefined && c.type === "button") {
        cell.dataset.vfFieldValueOption = String(c.value);
      }
      cell.style.gridRow = String(row);
      cell.style.gridColumn = `${startCol} / ${Math.max(startCol + 1, endCol)}`;

      let syncControl = () => {};

      if (type === "slider") {
        const rowBox = document.createElement("div");
        rowBox.className = "vf-std-dataform-row";
        const lbl = document.createElement("span");
        lbl.textContent = title;
        const num = document.createElement("span");
        num.className = "vf-std-dataform-val";
        num.dataset.vfFieldValue = name;

        const input = document.createElement("input");
        input.type = "range";
        input.className = "vf-std-dataform-slider";
        input.min = String(c.min ?? 0);
        input.max = String(c.max ?? 100);
        input.addEventListener("input", () => {
          num.textContent = input.value;
          emitField(c, Number(input.value));
        });
        input.addEventListener("change", () => {
          num.textContent = input.value;
          emitField(c, Number(input.value));
        });
        rowBox.append(lbl, input, num);
        cell.append(rowBox);

        syncControl = (val, disabled) => {
          const v = String(val ?? c.min ?? 0);
          num.textContent = v;
          if (document.activeElement !== input && input.value !== v) {
            input.value = v;
          }
          input.disabled = disabled;
        };
      } else if (type === "list" || type === "select") {
        const rowBox = document.createElement("div");
        rowBox.className = "vf-std-dataform-row";
        const lbl = document.createElement("span");
        lbl.textContent = title;
        const sel = document.createElement("select");
        sel.className = "vf-std-dataform-select";
        const opts = Array.isArray(c.options) ? c.options : [];
        for (const opt of opts) {
          const isObj = typeof opt === "object" && opt !== null && !Array.isArray(opt);
          const optVal = isObj ? String(opt.value ?? opt.title ?? opt.label ?? "") : String(opt);
          const optTitle = isObj ? String(opt.title ?? opt.label ?? opt.value ?? "") : String(opt);
          const o = document.createElement("option");
          o.value = optVal;
          o.textContent = optTitle;
          sel.append(o);
        }
        sel.addEventListener("change", () => emitField(c, sel.value));
        rowBox.append(lbl, sel);
        cell.append(rowBox);

        syncControl = (val, disabled) => {
          sel.disabled = disabled;
          const v = String(val ?? "");
          if (v && sel.value !== v) sel.value = v;
        };
      } else if (type === "check" || type === "checkbox") {
        const lbl = document.createElement("label");
        lbl.className = "vf-std-dataform-check";
        const chk = document.createElement("input");
        chk.type = "checkbox";
        const usesOnOff =
          typeof c.value === "string" &&
          (c.value.toLowerCase() === "on" || c.value.toLowerCase() === "off");
        chk.addEventListener("change", () => {
          emitField(c, usesOnOff ? (chk.checked ? "on" : "off") : chk.checked);
        });
        const span = document.createElement("span");
        span.textContent = title;
        lbl.append(chk, span);
        cell.append(lbl);

        syncControl = (val, disabled) => {
          chk.checked = isCheckValueOn(val);
          chk.disabled = disabled;
        };
      } else if (type === "color" || type === "palette") {
        const palBox = document.createElement("div");
        palBox.className = "vf-std-dataform-palette";
        const rowBox = document.createElement("div");
        rowBox.className = "vf-std-dataform-row";
        const lbl = document.createElement("span");
        lbl.textContent = title;
        const sw = document.createElement("input");
        sw.type = "color";
        sw.addEventListener("change", () => emitField(c, sw.value));
        rowBox.append(lbl, sw);
        palBox.append(rowBox);

        const swatches = Array.isArray(c.swatches)
          ? [...c.swatches.map(String)]
          : type === "palette"
            ? ["black", "blue", "green", "#dc2626"]
            : [];
        const swatchRow = document.createElement("div");
        swatchRow.className = "vf-std-dataform-swatches";
        const renderSwatches = (activeVal, disabled) => {
          swatchRow.replaceChildren();
          for (const sc of swatches) {
            const sb = document.createElement("button");
            sb.type = "button";
            sb.className = "vf-std-dataform-swatch-btn";
            sb.dataset.vfSwatch = sc;
            sb.dataset.active = String(String(activeVal) === sc);
            sb.style.background = resolveColorHex(sc);
            sb.title = sc;
            sb.disabled = disabled;
            sb.addEventListener("click", () => emitField(c, sc));
            swatchRow.append(sb);
          }
        };
        if (swatches.length > 0) palBox.append(swatchRow);
        cell.append(palBox);

        syncControl = (val, disabled) => {
          sw.disabled = disabled;
          sw.value = resolveColorHex(val);
          if (swatches.length > 0) renderSwatches(val, disabled);
        };
      } else if (type === "subwindow") {
        const sub = document.createElement("div");
        sub.className = "vf-std-dataform-subwindow";
        sub.dataset.vfSubwindow = name;
        const head = document.createElement("div");
        head.className = "vf-std-dataform-subwindow-head";
        const lbl = document.createElement("span");
        lbl.textContent = `${title} (SubControlFrame)`;
        const status = document.createElement("span");
        status.textContent = "Embedded";
        head.append(lbl, status);
        const body = document.createElement("div");
        body.textContent = "点击切换/定位局部纹理子视图";
        sub.append(head, body);
        sub.addEventListener("click", () => {
          emitField(c, c.value !== undefined && c.value !== "" ? c.value : "embedded");
        });
        cell.append(sub);

        syncControl = (val) => {
          if (val !== undefined && val !== null && val !== "") {
            status.textContent = String(val);
          }
        };
      } else {
        const btn = document.createElement("button");
        btn.type = "button";
        btn.className = "vf-std-dataform-btn";
        if (c.value !== undefined && c.value !== "") {
          btn.dataset.vfBtnValue = String(c.value);
        }
        if (stateColor) {
          const dot = document.createElement("span");
          dot.className = "vf-std-dataform-swatch-dot";
          dot.style.background = resolveColorHex(stateColor);
          btn.append(dot);
        }
        const textSpan = document.createElement("span");
        textSpan.textContent = title;
        btn.append(textSpan);
        btn.addEventListener("click", () => {
          const emitVal =
            c.value !== undefined && c.value !== "" && c.value !== null ? c.value : true;
          emitField(c, emitVal);
        });
        cell.append(btn);

        syncControl = (val, disabled) => {
          btn.disabled = disabled;
          const isActive =
            c.value !== undefined && c.value !== "" && String(val) === String(c.value);
          btn.dataset.active = String(isActive);
        };
      }

      grid.append(cell);
      entries.push({
        spec: c,
        name,
        stateStr,
        cell,
        sync: (valMap, formEnabled) => {
          const visible = stateStr !== "hidden" && isControlVisible(c, valMap);
          cell.style.display = visible ? "" : "none";
          cell.hidden = !visible;
          const disabled = !formEnabled || stateStr === "disabled";
          syncControl(valMap[name], disabled);
        },
      });
    }
    root.append(grid);
  }

  function update(next) {
    currentProps = next;
    const schema = parseSchema(next.schema);
    const schemaKey = JSON.stringify({
      row_spacing: schema.row_spacing,
      column_spacing: schema.column_spacing,
      controls: schema.controls,
      emptyText: next.emptyText ?? "",
    });
    if (schemaKey !== lastSchemaKey) {
      localOverrides = {};
      lastSchemaKey = schemaKey;
      buildDom(schema, next);
    }
    syncAll(schema, next);
  }
  update(props);

  return { update, destroy: () => root.remove() };
}
