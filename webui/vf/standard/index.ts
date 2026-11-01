// 工具自带的 17 种标准控件与标准容器：文字、按钮、输入框、多行文本、数字框、滑条、滚动条、下拉框、复选框、页签栏、列表、树、对话框按钮组、分隔线、占位区、数据生成表单、浮动嵌入框。
// 每种包含共用声明（component.json）、网页实现（index.js / style.css）和对应的 Qt 映射。
import type { Value } from "../src/expr.ts";
import {
  normalizeStateImageSlice,
  resolveStateImage,
  STATE_IMAGE_KEYS,
  type ComponentCreate,
  type ComponentDecl,
  type ComponentInstance,
  type LoadedComponent,
  type StateImagesDecl,
} from "../src/model.ts";
import buttonDecl from "./Button/component.json" with { type: "json" };
import checkBoxDecl from "./CheckBox/component.json" with { type: "json" };
import comboBoxDecl from "./ComboBox/component.json" with { type: "json" };
import dataFormDecl from "./DataForm/component.json" with { type: "json" };
import dialogButtonBoxDecl from "./DialogButtonBox/component.json" with { type: "json" };
import floatBoxDecl from "./FloatBox/component.json" with { type: "json" };
import labelDecl from "./Label/component.json" with { type: "json" };
import listDecl from "./List/component.json" with { type: "json" };
import scrollBarDecl from "./ScrollBar/component.json" with { type: "json" };
import separatorDecl from "./Separator/component.json" with { type: "json" };
import sliderDecl from "./Slider/component.json" with { type: "json" };
import spacerDecl from "./Spacer/component.json" with { type: "json" };
import spinBoxDecl from "./SpinBox/component.json" with { type: "json" };
import tabBarDecl from "./TabBar/component.json" with { type: "json" };
import textAreaDecl from "./TextArea/component.json" with { type: "json" };
import textInputDecl from "./TextInput/component.json" with { type: "json" };
import treeDecl from "./Tree/component.json" with { type: "json" };

export interface StandardComponent extends LoadedComponent {
  styleText: string;
}

const ICONS: Record<string, string> = {
  arrow: '<path d="M5 3l12 7-5 1.5L9.5 16 5 3z" fill="currentColor"/>',
  pen: '<path d="M14.5 2.5l3 3-9.5 9.5-3.5.5.5-3.5 9.5-9.5zm-8.5 11l-1 1" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  brush: '<path d="M14.5 2.5l3 3-8.5 8.5c-1.5 1.5-3.5 2-5 1.5.5-1.5 0-3.5 1.5-5l8.5-8.5z" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  eraser: '<path d="M11 4l6 6-7 7H5l-2-2 8-11zm-4 13h10" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  fill: '<path d="M9 3l6 6-6 6-6-6 6-6zm7 9c1.2 1.5 2 2.7 2 3.8a2 2 0 1 1-4 0c0-1.1.8-2.3 2-3.8z" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  bucket: '<path d="M9 3l6 6-6 6-6-6 6-6zm7 9c1.2 1.5 2 2.7 2 3.8a2 2 0 1 1-4 0c0-1.1.8-2.3 2-3.8z" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  shape: '<rect x="3.5" y="3.5" width="8.5" height="8.5" rx="1" stroke="currentColor" stroke-width="1.5" fill="none"/><circle cx="13" cy="13" r="4" stroke="currentColor" stroke-width="1.5" fill="none"/>',
  move: '<path d="M10 3v14M3 10h14M10 3l-2.5 2.5M10 3l2.5 2.5M10 17l-2.5-2.5M10 17l2.5-2.5M3 10l2.5-2.5M3 10l2.5 2.5M17 10l-2.5-2.5M17 10l-2.5 2.5" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  lasso: '<path d="M10 4c4 0 6.5 2.2 6.5 5s-2.5 5-6.5 5c-1.8 0-3.3-.4-4.5-1.2M6 13a2 2 0 1 0 0 4 2 2 0 0 0 0-4z" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
  eyedropper: '<path d="M13.5 3.5l3 3-2 2-3-3 2-2zm-3 3l3 3-6.5 6.5H4v-3l6.5-6.5z" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linejoin="round"/>',
  transfer: '<path d="M4 7h11m-3-3l3 3-3 3M16 13H5m3-3l-3 3 3 3" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  remap: '<path d="M4 7h11m-3-3l3 3-3 3M16 13H5m3-3l-3 3 3 3" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  connect: '<path d="M5 15c2-6 8-6 10-10M5 15a1.5 1.5 0 1 0 0 .1zm10-10a1.5 1.5 0 1 0 0 .1z" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
  hline: '<path d="M3 10h14M6 6v8m8-8v8" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
  vline: '<path d="M10 3v14M6 6h8m-8 8h8" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
  area: '<rect x="3.5" y="4.5" width="13" height="11" rx="1.5" stroke="currentColor" stroke-width="1.5" stroke-dasharray="2.5 2" fill="none"/>',
  addline: '<path d="M4 14L14 4m-3 11h5m-2.5-2.5v5" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
  automap: '<path d="M10 3l2 4.5L16.5 9 12 11l-2 4.5L8 11 3.5 9 8 7.5 10 3z" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linejoin="round"/>',
  plus: '<path d="M10 4v12M4 10h12" stroke="currentColor" stroke-width="1.7" fill="none" stroke-linecap="round"/>',
  add: '<path d="M10 4v12M4 10h12" stroke="currentColor" stroke-width="1.7" fill="none" stroke-linecap="round"/>',
  minus: '<path d="M4 10h12" stroke="currentColor" stroke-width="1.7" fill="none" stroke-linecap="round"/>',
  remove: '<path d="M4 10h12" stroke="currentColor" stroke-width="1.7" fill="none" stroke-linecap="round"/>',
  duplicate: '<rect x="6.5" y="6.5" width="9" height="9" rx="1.2" stroke="currentColor" stroke-width="1.4" fill="none"/><path d="M4.5 12.5V5a1 1 0 0 1 1-1h7.5" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round"/>',
  folder: '<path d="M3 6a1.5 1.5 0 0 1 1.5-1.5h3.5l1.5 2h6A1.5 1.5 0 0 1 17 8v6.5a1.5 1.5 0 0 1-1.5 1.5h-11A1.5 1.5 0 0 1 3 14.5V6z" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linejoin="round"/>',
  eye: '<path d="M2.5 10s3-5 7.5-5 7.5 5 7.5 5-3 5-7.5 5-7.5-5-7.5-5z" stroke="currentColor" stroke-width="1.4" fill="none"/><circle cx="10" cy="10" r="2.2" stroke="currentColor" stroke-width="1.4" fill="none"/>',
  lock: '<rect x="4.5" y="9" width="11" height="7.5" rx="1.5" stroke="currentColor" stroke-width="1.4" fill="none"/><path d="M7 9V6.5a3 3 0 0 1 6 0V9" stroke="currentColor" stroke-width="1.4" fill="none"/>',
  unselect: '<rect x="4" y="4" width="12" height="12" rx="1.5" stroke="currentColor" stroke-width="1.4" stroke-dasharray="2.5 2" fill="none"/><path d="M7 7l6 6M13 7l-6 6" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>',
  to_layer: '<path d="M4 13l6 3 6-3M4 9l6 3 6-3M10 3v6m-2.5-2L10 9.5 12.5 7" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  float: '<rect x="3.5" y="4.5" width="13" height="11" rx="1.5" stroke="currentColor" stroke-width="1.4" fill="none"/><path d="M11 4.5v4.5h5.5" stroke="currentColor" stroke-width="1.4" fill="none"/>',
  play: '<path d="M6 4.5l10 5.5-10 5.5V4.5z" fill="currentColor"/>',
  pause: '<path d="M6.5 4.5h2.5v11H6.5zm4.5 0h2.5v11H11z" fill="currentColor"/>',
  prev: '<path d="M5.5 5v10M14.5 5l-7 5 7 5V5z" stroke="currentColor" stroke-width="1.4" fill="currentColor" stroke-linecap="round" stroke-linejoin="round"/>',
  next: '<path d="M14.5 5v10M5.5 5l7 5-7 5V5z" stroke="currentColor" stroke-width="1.4" fill="currentColor" stroke-linecap="round" stroke-linejoin="round"/>',
  loop: '<path d="M14 6l2 2-2 2M6 14l-2-2 2-2M4 10V8a2 2 0 0 1 2-2h10m0 4v2a2 2 0 0 1-2 2H4" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
  onion: '<circle cx="8.5" cy="10" r="4.5" stroke="currentColor" stroke-width="1.4" fill="none"/><circle cx="11.5" cy="10" r="4.5" stroke="currentColor" stroke-width="1.4" stroke-dasharray="2 1.5" fill="none"/>',
  clear: '<path d="M5 5l10 10M15 5L5 15" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
  hold: '<path d="M5 5v10m4-10v10m3-5h4m-2-2v4" stroke="currentColor" stroke-width="1.5" fill="none" stroke-linecap="round"/>',
};

const createLabel: ComponentCreate = (el, { props }) => {
  const label = document.createElement("div");
  label.className = "vf-std-label";
  el.append(label);
  const update = (next: Record<string, Value>) => {
    label.textContent = String(next.text ?? "");
    label.dataset.tone = String(next.tone ?? "normal");
    label.dataset.align = String(next.align ?? "left");
  };
  update(props);
  return { update, destroy: () => label.remove() };
};

const createButton: ComponentCreate = (el, { props, emit }) => {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "vf-std-button";
  let current = props;
  button.addEventListener("click", () => {
    if (current.enabled === false) return;
    if (current.checkable) emit("toggle", !current.checked);
  });
  el.append(button);
  const update = (next: Record<string, Value>) => {
    current = next;
    const iconName = String(next.icon ?? "").trim();
    const text = String(next.text ?? "");
    if (iconName && ICONS[iconName]) {
      const svg = `<svg viewBox="0 0 20 20" width="14" height="14" style="flex-shrink:0;display:block">${ICONS[iconName]}</svg>`;
      const span = text ? `<span style="overflow:hidden;text-overflow:ellipsis">${text.replace(/&/g, "&amp;").replace(/</g, "&lt;")}</span>` : "";
      button.innerHTML = svg + span;
      button.dataset.hasIcon = "true";
    } else {
      button.textContent = text;
      delete button.dataset.hasIcon;
    }
    button.dataset.variant = String(next.variant ?? "secondary");
    button.dataset.checked = String(Boolean(next.checked));
    button.disabled = next.enabled === false;
  };
  update(props);
  return { update, destroy: () => button.remove() };
};

const createTextInput: ComponentCreate = (el, { props, emit }) => {
  const input = document.createElement("input");
  input.type = "text";
  input.className = "vf-std-input";
  input.addEventListener("input", () => emit("change", input.value));
  el.append(input);
  const update = (next: Record<string, Value>) => {
    input.placeholder = String(next.placeholder ?? "");
    input.disabled = next.enabled === false;
    const value = String(next.value ?? "");
    if (input.value !== value) input.value = value;
  };
  update(props);
  return { update, destroy: () => input.remove() };
};

const createTextArea: ComponentCreate = (el, { props, emit }) => {
  const area = document.createElement("textarea");
  area.className = "vf-std-textarea";
  area.addEventListener("input", () => emit("change", area.value));
  el.append(area);
  const update = (next: Record<string, Value>) => {
    area.placeholder = String(next.placeholder ?? "");
    area.readOnly = Boolean(next.readOnly);
    area.disabled = next.enabled === false;
    const value = String(next.value ?? "");
    if (area.value !== value) area.value = value;
  };
  update(props);
  return { update, destroy: () => area.remove() };
};

const createSpinBox: ComponentCreate = (el, { props, emit }) => {
  const wrap = document.createElement("div");
  wrap.className = "vf-std-spinbox";
  const input = document.createElement("input");
  input.type = "number";
  input.className = "vf-std-spinbox-input";
  const suffix = document.createElement("span");
  suffix.className = "vf-std-spinbox-suffix";
  wrap.append(input, suffix);
  el.append(wrap);
  let min = 0;
  let max = 9999;
  const clamp = (n: number) => Math.max(min, Math.min(max, n));
  input.addEventListener("input", () => {
    const n = Number(input.value);
    if (Number.isFinite(n)) {
      const clamped = clamp(n);
      if (input.value !== String(clamped)) input.value = String(clamped);
      emit("change", clamped);
    }
  });
  const update = (next: Record<string, Value>) => {
    min = Number(next.min ?? 0);
    max = Number(next.max ?? 9999);
    input.min = String(min);
    input.max = String(max);
    input.step = String(next.step ?? 1);
    input.disabled = next.enabled === false;
    const raw = Number(next.value ?? 0);
    const val = String(Number.isFinite(raw) ? clamp(raw) : min);
    if (input.value !== val) input.value = val;
    const sfx = String(next.suffix ?? "");
    suffix.textContent = sfx;
    suffix.hidden = !sfx;
  };
  update(props);
  return { update, destroy: () => wrap.remove() };
};

const createSlider: ComponentCreate = (el, { props, emit }) => {
  const wrap = document.createElement("div");
  wrap.className = "vf-std-slider";
  const input = document.createElement("input");
  input.type = "range";
  input.className = "vf-std-slider-input";
  wrap.append(input);
  el.append(wrap);
  input.addEventListener("input", () => emit("change", Number(input.value)));
  const update = (next: Record<string, Value>) => {
    const min = Number(next.min ?? 0);
    const max = Number(next.max ?? 100);
    const step = Number(next.step ?? 1);
    const val = Number(next.value ?? 0);
    input.min = String(min);
    input.max = String(max);
    input.step = String(step > 0 ? step : 1);
    if (Number(input.value) !== val) input.value = String(val);
    input.disabled = next.enabled === false;
    wrap.dataset.orientation = String(next.orientation ?? "horizontal");
  };
  update(props);
  return { update, destroy: () => wrap.remove() };
};

const createScrollBar: ComponentCreate = (el, { props, emit }) => {
  const track = document.createElement("div");
  track.className = "vf-std-scrollbar";
  const thumb = document.createElement("div");
  thumb.className = "vf-std-scrollbar-thumb";
  track.append(thumb);
  el.append(track);
  let current = props;
  const update = (next: Record<string, Value>) => {
    current = next;
    const min = Number(next.min ?? 0);
    const max = Math.max(min, Number(next.max ?? 100));
    const page = Math.max(1, Number(next.pageStep ?? 10));
    const val = Math.min(max, Math.max(min, Number(next.value ?? 0)));
    const range = Math.max(1, max - min);
    const ratio = Math.min(0.85, Math.max(0.15, page / (range + page)));
    const posRatio = max > min ? (val - min) / (max - min) : 0;
    const vertical = next.orientation === "vertical";
    track.dataset.orientation = vertical ? "vertical" : "horizontal";
    track.dataset.disabled = String(next.enabled === false);
    if (vertical) {
      thumb.style.left = "1px";
      thumb.style.right = "1px";
      thumb.style.width = "auto";
      thumb.style.height = `${ratio * 100}%`;
      thumb.style.top = `${(1 - ratio) * posRatio * 100}%`;
    } else {
      thumb.style.top = "1px";
      thumb.style.bottom = "1px";
      thumb.style.height = "auto";
      thumb.style.width = `${ratio * 100}%`;
      thumb.style.left = `${(1 - ratio) * posRatio * 100}%`;
    }
  };
  track.addEventListener("mousedown", (ev) => {
    if (current.enabled === false) return;
    const rect = track.getBoundingClientRect();
    const min = Number(current.min ?? 0);
    const max = Math.max(min, Number(current.max ?? 100));
    const step = Math.max(1, Number(current.step ?? 1));
    const frac =
      current.orientation === "vertical"
        ? (ev.clientY - rect.top) / Math.max(1, rect.height)
        : (ev.clientX - rect.left) / Math.max(1, rect.width);
    const clamped = Math.min(1, Math.max(0, frac));
    const snapped = Math.min(max, Math.max(min, Math.round((min + clamped * (max - min)) / step) * step));
    emit("change", snapped);
  });
  update(props);
  return { update, destroy: () => track.remove() };
};

const createComboBox: ComponentCreate = (el, { props, emit }) => {
  const select = document.createElement("select");
  select.className = "vf-std-combobox";
  let lastOptions = "";
  select.addEventListener("change", () => emit("change", select.value));
  el.append(select);
  const update = (next: Record<string, Value>) => {
    const rawOptions = Array.isArray(next.options) ? next.options.map(String).join(",") : String(next.options ?? "");
    if (rawOptions !== lastOptions) {
      lastOptions = rawOptions;
      select.replaceChildren();
      for (const item of rawOptions.split(",").map((s) => s.trim()).filter(Boolean)) {
        const opt = document.createElement("option");
        opt.value = item;
        opt.textContent = item;
        select.append(opt);
      }
    }
    select.disabled = next.enabled === false;
    const val = String(next.value ?? "");
    if (val && select.value !== val) select.value = val;
  };
  update(props);
  return { update, destroy: () => select.remove() };
};

const createCheckBox: ComponentCreate = (el, { props, emit }) => {
  const label = document.createElement("label");
  label.className = "vf-std-checkbox";
  const box = document.createElement("input");
  box.type = "checkbox";
  const text = document.createElement("span");
  label.append(box, text);
  el.append(label);
  box.addEventListener("change", () => emit("change", box.checked));
  const update = (next: Record<string, Value>) => {
    box.checked = Boolean(next.value);
    box.disabled = next.enabled === false;
    text.textContent = String(next.text ?? "");
    label.dataset.disabled = String(next.enabled === false);
  };
  update(props);
  return { update, destroy: () => label.remove() };
};

const createTabBar: ComponentCreate = (el, { props, emit }) => {
  const bar = document.createElement("div");
  bar.className = "vf-std-tabbar";
  el.append(bar);
  const parseTabs = (raw: Value): string[] => {
    if (Array.isArray(raw)) {
      return raw.map((it, i) =>
        typeof it === "object" && it !== null && !Array.isArray(it)
          ? String(it.label ?? it.title ?? it.name ?? i)
          : String(it),
      );
    }
    return String(raw ?? "")
      .split(",")
      .map((s) => s.trim())
      .filter(Boolean);
  };
  const update = (next: Record<string, Value>) => {
    bar.replaceChildren();
    const tabs = parseTabs(next.tabs ?? "");
    const cur = Number(next.value ?? 0);
    const disabled = next.enabled === false;
    tabs.forEach((label, idx) => {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.dataset.vfTab = String(idx);
      btn.className = "vf-std-tabbar-tab" + (idx === cur ? " active" : "");
      btn.textContent = label;
      btn.disabled = disabled;
      btn.addEventListener("click", () => {
        if (disabled) return;
        if (idx !== cur) emit("change", idx);
      });
      bar.append(btn);
    });
  };
  update(props);
  return { update, destroy: () => bar.remove() };
};

const createList: ComponentCreate = (el, { props, emit }) => {
  const container = document.createElement("div");
  container.className = "vf-std-list";
  el.append(container);
  let dragFrom = -1;

  const itemLabel = (item: Value): string => {
    if (item && typeof item === "object" && !Array.isArray(item)) {
      return String(item.name ?? item.label ?? item.title ?? item.text ?? item.id ?? "");
    }
    return String(item ?? "");
  };

  const update = (next: Record<string, Value>) => {
    container.replaceChildren();
    const disabled = next.enabled === false;
    container.dataset.disabled = String(disabled);
    const items = Array.isArray(next.items) ? next.items : [];
    const selected = typeof next.value === "number" ? next.value : 0;
    const draggable = Boolean(next.draggable);
    const mutedFrom = typeof next.mutedFrom === "number" ? next.mutedFrom : -1;
    if (items.length === 0) {
      const empty = document.createElement("div");
      empty.className = "vf-std-list-empty";
      empty.textContent = String(next.emptyText ?? "暂无内容");
      container.append(empty);
      return;
    }
    items.forEach((item, index) => {
      const isObj = item !== null && typeof item === "object" && !Array.isArray(item);
      const muted = (isObj && Boolean(item.muted)) || (mutedFrom >= 0 && index >= mutedFrom);
      const row = document.createElement("div");
      row.dataset.vfRow = String(index);
      row.className = "vf-std-list-row" + (index === selected ? " selected" : "") + (muted ? " muted" : "");
      const text = document.createElement("span");
      text.className = "vf-std-list-text";
      text.textContent = itemLabel(item);
      row.append(text);
      if (isObj && item.badge !== undefined && item.badge !== "") {
        const badge = document.createElement("span");
        badge.className = "vf-std-list-badge";
        badge.textContent = String(item.badge);
        row.append(badge);
      }
      row.addEventListener("click", () => {
        if (disabled) return;
        emit("select", index);
        if (index !== selected) {
          emit("change", index);
        }
      });
      if (draggable) {
        row.draggable = true;
        row.addEventListener("dragstart", (ev) => {
          dragFrom = index;
          if (ev.dataTransfer) {
            ev.dataTransfer.effectAllowed = "move";
            ev.dataTransfer.setData("text/plain", String(index));
          }
        });
        row.addEventListener("dragover", (ev) => {
          ev.preventDefault();
          if (ev.dataTransfer) ev.dataTransfer.dropEffect = "move";
        });
        row.addEventListener("drop", (ev) => {
          ev.preventDefault();
          if (disabled) return;
          const from = dragFrom >= 0 ? dragFrom : Number(ev.dataTransfer?.getData("text/plain") ?? -1);
          dragFrom = -1;
          if (from >= 0 && from !== index) {
            emit("reorder", { from, to: index });
          }
        });
      }
      container.append(row);
    });
  };
  update(props);
  return { update, destroy: () => container.remove() };
};

interface FlatTreeRow {
  index: number;
  id: string;
  label: string;
  kind: string;
  isGroup: boolean;
  depth: number;
  collapsed: boolean;
  checked: boolean;
}

function normalizeTreeItems(rawItems: Value): FlatTreeRow[] {
  if (!Array.isArray(rawItems)) return [];
  const flat: FlatTreeRow[] = [];
  const walk = (list: Value[], depthOffset: number) => {
    for (let i = 0; i < list.length; i++) {
      const item = list[i];
      if (typeof item !== "object" || item === null || Array.isArray(item)) {
        flat.push({
          index: flat.length,
          id: String(flat.length),
          label: String(item ?? ""),
          kind: "",
          isGroup: false,
          depth: depthOffset,
          collapsed: false,
          checked: true,
        });
        continue;
      }
      const children = Array.isArray(item.children) ? item.children : [];
      const depth = typeof item.depth === "number" ? item.depth : depthOffset;
      const idx = flat.length;
      flat.push({
        index: idx,
        id: String(item.id ?? item.name ?? idx),
        label: String(item.label ?? item.name ?? item.text ?? idx),
        kind: String(item.kind ?? ""),
        isGroup: Boolean(item.isGroup || children.length > 0),
        depth,
        collapsed: Boolean(item.collapsed),
        checked: item.checked !== undefined ? Boolean(item.checked) : item.visible !== undefined ? Boolean(item.visible) : true,
      });
      if (children.length > 0) walk(children, depth + 1);
    }
  };
  walk(rawItems, 0);
  return flat;
}

const createTree: ComponentCreate = (el, { props, emit }) => {
  const container = document.createElement("div");
  container.className = "vf-std-tree";
  el.append(container);
  let dragFrom = -1;

  const update = (next: Record<string, Value>) => {
    container.replaceChildren();
    const disabled = next.enabled === false;
    container.dataset.disabled = String(disabled);
    const rows = normalizeTreeItems(next.items ?? []);
    const selected = typeof next.value === "number" ? next.value : Number(next.selectedIndex ?? 0);
    const checkable = next.checkable !== false;
    const draggable = next.draggable !== false;
    if (rows.length === 0) {
      const empty = document.createElement("div");
      empty.className = "vf-std-tree-empty";
      empty.textContent = String(next.emptyText ?? "暂无内容");
      container.append(empty);
      return;
    }
    let collapsedDepth: number | null = null;
    for (const r of rows) {
      if (collapsedDepth !== null) {
        if (r.depth > collapsedDepth) continue;
        collapsedDepth = null;
      }
      if (r.isGroup && r.collapsed) collapsedDepth = r.depth;
      const row = document.createElement("div");
      row.dataset.vfRow = String(r.index);
      row.dataset.vfItemId = r.id;
      row.className = "vf-std-tree-row" + (r.index === selected ? " selected" : "") + (!r.checked ? " unchecked" : "");
      row.style.paddingLeft = `${6 + r.depth * 14}px`;

      const caret = document.createElement("span");
      caret.className = "vf-std-tree-caret";
      if (r.isGroup) {
        caret.dataset.vfAction = "collapse";
        caret.textContent = r.collapsed ? "▸" : "▾";
        caret.addEventListener("click", (ev) => {
          ev.stopPropagation();
          if (disabled) return;
          emit("toggleCollapse", { index: r.index, id: r.id, collapsed: !r.collapsed });
        });
      }
      row.append(caret);

      if (checkable) {
        const chk = document.createElement("input");
        chk.type = "checkbox";
        chk.dataset.vfAction = "check";
        chk.className = "vf-std-tree-check";
        chk.checked = r.checked;
        chk.addEventListener("click", (ev) => ev.stopPropagation());
        chk.addEventListener("change", (ev) => {
          ev.stopPropagation();
          if (disabled) return;
          emit("toggleCheck", { index: r.index, id: r.id, checked: chk.checked });
        });
        row.append(chk);
      }

      const label = document.createElement("span");
      label.className = "vf-std-tree-text" + (r.isGroup ? " group" : "");
      label.textContent = r.label;
      row.append(label);

      if (r.kind) {
        const badge = document.createElement("span");
        badge.className = "vf-std-tree-badge";
        badge.textContent = r.kind;
        row.append(badge);
      }

      row.addEventListener("click", () => {
        if (disabled) return;
        emit("select", r.index);
        if (r.index !== selected) emit("change", r.index);
      });

      if (draggable) {
        row.draggable = true;
        row.addEventListener("dragstart", (ev) => {
          dragFrom = r.index;
          if (ev.dataTransfer) {
            ev.dataTransfer.effectAllowed = "move";
            ev.dataTransfer.setData("text/plain", String(r.index));
          }
        });
        row.addEventListener("dragover", (ev) => {
          ev.preventDefault();
          if (ev.dataTransfer) ev.dataTransfer.dropEffect = "move";
        });
        row.addEventListener("drop", (ev) => {
          ev.preventDefault();
          if (disabled) return;
          const from = dragFrom >= 0 ? dragFrom : Number(ev.dataTransfer?.getData("text/plain") ?? -1);
          dragFrom = -1;
          if (from >= 0 && from !== r.index) {
            emit("reorder", { from, to: r.index });
          }
        });
      }
      container.append(row);
    }
  };
  update(props);
  return { update, destroy: () => container.remove() };
};

const createDialogButtonBox: ComponentCreate = (el, { props, emit }) => {
  const bar = document.createElement("div");
  bar.className = "vf-std-buttonbox";
  el.append(bar);
  const parseButtons = (raw: Value): string[] => {
    if (Array.isArray(raw)) return raw.map(String);
    return String(raw ?? "OK,Cancel")
      .split(",")
      .map((s) => s.trim())
      .filter(Boolean);
  };
  const update = (next: Record<string, Value>) => {
    bar.replaceChildren();
    const list = parseButtons(next.buttons ?? "OK,Cancel");
    const disabled = next.enabled === false;
    for (const role of list) {
      const isAccept = /^(ok|accept|yes|apply|create|save|confirm)$/i.test(role);
      const isReject = /^(cancel|reject|no|close)$/i.test(role);
      const text =
        isAccept && next.okText && next.okText !== "OK"
          ? String(next.okText)
          : isReject && next.cancelText && next.cancelText !== "Cancel"
            ? String(next.cancelText)
            : role;
      const btn = document.createElement("button");
      btn.type = "button";
      btn.dataset.vfRole = isAccept ? "accept" : isReject ? "reject" : role.toLowerCase();
      btn.className = "vf-std-buttonbox-btn" + (isAccept ? " primary" : "");
      btn.textContent = text;
      btn.disabled = disabled;
      btn.addEventListener("click", () => {
        if (disabled) return;
        if (isAccept) emit("accept");
        else if (isReject) emit("reject");
      });
      bar.append(btn);
    }
  };
  update(props);
  return { update, destroy: () => bar.remove() };
};

const createSeparator: ComponentCreate = (el, { props }) => {
  const line = document.createElement("div");
  line.className = "vf-std-separator";
  el.append(line);
  const update = (next: Record<string, Value>) => {
    line.dataset.orientation = String(next.orientation ?? "horizontal");
  };
  update(props);
  return { update, destroy: () => line.remove() };
};

const createFloatBox: ComponentCreate = (el, { props, emit, mode }) => {
  const root = document.createElement("div");
  root.className = "vf-std-floatbox";
  const header = document.createElement("div");
  header.className = "vf-std-floatbox-header";
  const title = document.createElement("span");
  title.className = "vf-std-floatbox-title";
  const toggleBtn = document.createElement("button");
  toggleBtn.type = "button";
  toggleBtn.className = "vf-std-floatbox-toggle";
  header.append(title, toggleBtn);
  const body = document.createElement("div");
  body.className = "vf-std-floatbox-body";
  root.append(header, body);
  el.append(root);

  let current = props;

  toggleBtn.addEventListener("click", (e) => {
    e.stopPropagation();
    if (current.enabled === false) return;
    const next = !Boolean(current.floating);
    emit("toggle", next);
    emit("change", next);
  });

  header.addEventListener("pointerdown", (e) => {
    if (mode !== "preview" || current.enabled === false || !Boolean(current.floating)) return;
    if (e.button !== 0 || e.target === toggleBtn) return;
    e.stopPropagation();
    const doc = root.ownerDocument || document;
    const win = doc.defaultView || window;
    const frameEl = win.frameElement as HTMLElement | null;
    const outerWin = frameEl && frameEl.ownerDocument ? frameEl.ownerDocument.defaultView : null;
    const toLocalPoint = (pe: PointerEvent, fromOuter = false) => {
      const eventInOuter = Boolean(outerWin && frameEl && (fromOuter || pe.view === outerWin));
      if (eventInOuter && frameEl) {
        const fRect = frameEl.getBoundingClientRect();
        const scaleX = fRect.width > 0 && frameEl.offsetWidth > 0 ? fRect.width / frameEl.offsetWidth : 1;
        const scaleY = fRect.height > 0 && frameEl.offsetHeight > 0 ? fRect.height / frameEl.offsetHeight : 1;
        return { x: (pe.clientX - fRect.left) / scaleX, y: (pe.clientY - fRect.top) / scaleY };
      }
      const rect = root.getBoundingClientRect();
      const zoom = rect.width > 0 && root.offsetWidth > 0 ? rect.width / root.offsetWidth : 1;
      return { x: pe.clientX / zoom, y: pe.clientY / zoom };
    };
    const startPt = toLocalPoint(e, false);
    const baseX = Number(current.floatX ?? 60);
    const baseY = Number(current.floatY ?? 80);
    const onMove = (me: PointerEvent, fromOuter = false) => {
      const curPt = toLocalPoint(me, fromOuter);
      const dx = Math.round(curPt.x - startPt.x);
      const dy = Math.round(curPt.y - startPt.y);
      emit("move", { x: Math.max(0, baseX + dx), y: Math.max(0, baseY + dy) });
    };
    const onInnerMove = (me: PointerEvent) => onMove(me, false);
    const onOuterMove = (me: PointerEvent) => onMove(me, true);
    const onUp = () => {
      win.removeEventListener("pointermove", onInnerMove);
      win.removeEventListener("pointerup", onUp);
      if (outerWin && outerWin !== win) {
        outerWin.removeEventListener("pointermove", onOuterMove);
        outerWin.removeEventListener("pointerup", onUp);
      }
    };
    win.addEventListener("pointermove", onInnerMove);
    win.addEventListener("pointerup", onUp);
    if (outerWin && outerWin !== win) {
      outerWin.addEventListener("pointermove", onOuterMove);
      outerWin.addEventListener("pointerup", onUp);
    }
  });

  const update = (next: Record<string, Value>) => {
    current = next;
    const floating = Boolean(next.floating);
    const hdrH = Number(next.headerHeight ?? 24);
    header.style.height = `${hdrH}px`;
    root.dataset.floating = String(floating);
    root.dataset.disabled = String(next.enabled === false);
    title.textContent = String(next.title ?? "浮动嵌入框");
    toggleBtn.textContent = floating ? "嵌入" : "浮动";
    toggleBtn.title = floating ? "嵌入回原布局" : "脱离布局浮动";
    toggleBtn.disabled = next.enabled === false;
  };
  update(props);
  return { update, destroy: () => root.remove() };
};

const createSpacer: ComponentCreate = (el) => {
  const spacer = document.createElement("div");
  spacer.className = "vf-std-spacer";
  el.append(spacer);
  return { update() {}, destroy: () => spacer.remove() };
};

interface DataFormControlSpec {
  name?: string;
  type?: string;
  title?: string;
  hook?: string;
  value?: Value;
  min?: number;
  max?: number;
  options?: Value[];
  swatches?: Value[];
  row?: number;
  column?: number;
  start_column?: number;
  end_column?: number;
  state?: string | { color?: string };
  visible_when?:
    | string
    | {
        name?: string;
        control?: string;
        field?: string;
        value?: Value;
        values?: Value[];
        equals?: Value;
        not_equals?: Value;
        in?: Value[];
      };
}

function parseDataFormSchema(raw: Value): { row_spacing: number; column_spacing: number; controls: DataFormControlSpec[] } {
  let target = raw;
  if (typeof target === "string") {
    try {
      target = JSON.parse(target) as Value;
    } catch {
      return { row_spacing: 8, column_spacing: 6, controls: [] };
    }
  }
  if (Array.isArray(target)) {
    return { row_spacing: 8, column_spacing: 6, controls: target as DataFormControlSpec[] };
  }
  if (typeof target === "object" && target !== null) {
    const obj = target as Record<string, Value>;
    return {
      row_spacing: Number(obj.row_spacing ?? 8),
      column_spacing: Number(obj.column_spacing ?? 6),
      controls: Array.isArray(obj.controls) ? (obj.controls as DataFormControlSpec[]) : [],
    };
  }
  return { row_spacing: 8, column_spacing: 6, controls: [] };
}

function parseDataFormValues(raw: Value): Record<string, Value> {
  if (typeof raw === "string") {
    try {
      const parsed = JSON.parse(raw) as Value;
      return typeof parsed === "object" && parsed !== null && !Array.isArray(parsed) ? parsed : {};
    } catch {
      return {};
    }
  }
  if (typeof raw === "object" && raw !== null && !Array.isArray(raw)) return raw;
  return {};
}

function isCheckValueOn(val: Value): boolean {
  if (typeof val === "boolean") return val;
  if (typeof val === "number") return val !== 0;
  if (typeof val === "string") {
    const s = val.trim().toLowerCase();
    return s === "on" || s === "true" || s === "1" || s === "yes";
  }
  return false;
}

function evalStringVisibleWhen(expr: string, valMap: Record<string, Value>): boolean {
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

function isDataFormControlVisible(ctrl: DataFormControlSpec, valMap: Record<string, Value>): boolean {
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

function resolveColorHex(raw: Value): string {
  const s = String(raw ?? "").trim().toLowerCase();
  if (s.startsWith("#") && (s.length === 4 || s.length === 7)) return s;
  const map: Record<string, string> = {
    black: "#1e1e1e",
    blue: "#2563eb",
    green: "#16a34a",
    red: "#dc2626",
    white: "#f8fafc",
    yellow: "#eab308",
  };
  return map[s] ?? "#4a90d9";
}

const createDataForm: ComponentCreate = (el, { props, emit }) => {
  const root = document.createElement("div");
  root.className = "vf-std-dataform";
  el.append(root);
  let localOverrides: Record<string, Value> = {};
  let lastSchemaKey = "";
  let entries: Array<{
    spec: DataFormControlSpec;
    name: string;
    stateStr: string;
    cell: HTMLElement;
    sync: (valMap: Record<string, Value>, formEnabled: boolean) => void;
  }> = [];
  let currentProps = props;

  const computeValMap = (
    schema: { controls: DataFormControlSpec[] },
    next: Record<string, Value>,
  ): Record<string, Value> => {
    const extValues = parseDataFormValues(next.values ?? "{}");
    const valMap: Record<string, Value> = {};
    for (const c of schema.controls) {
      if (!c || typeof c !== "object") continue;
      const name = String(c.name ?? "");
      if (!name) continue;
      if (extValues[name] !== undefined) valMap[name] = extValues[name];
      else if (localOverrides[name] !== undefined) valMap[name] = localOverrides[name];
      else if (valMap[name] === undefined) valMap[name] = c.value ?? null;
    }
    for (const [k, v] of Object.entries(extValues)) {
      if (valMap[k] === undefined) valMap[k] = v;
    }
    return valMap;
  };

  const syncAll = (schema: { controls: DataFormControlSpec[] }, next: Record<string, Value>) => {
    const valMap = computeValMap(schema, next);
    const formEnabled = next.enabled !== false;
    for (const entry of entries) {
      entry.sync(valMap, formEnabled);
    }
  };

  const emitField = (c: DataFormControlSpec, val: Value) => {
    const name = String(c.name ?? "");
    localOverrides[name] = val;
    emit("change", {
      name,
      value: val,
      hook: String(c.hook ?? ""),
      type: String(c.type ?? ""),
    });
    const schema = parseDataFormSchema(currentProps.schema ?? []);
    syncAll(schema, currentProps);
  };

  const buildDom = (
    schema: { row_spacing: number; column_spacing: number; controls: DataFormControlSpec[] },
    next: Record<string, Value>,
  ) => {
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
    const rowCounts = new Map<number, number>();
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
    grid.style.rowGap = `${Math.min(2, Math.max(1, schema.row_spacing))}px`;
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

      let syncControl: (val: Value, disabled: boolean) => void = () => {};

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
          const optMap = isObj ? (opt as Record<string, Value>) : null;
          const optVal = optMap ? String(optMap.value ?? optMap.title ?? optMap.label ?? "") : String(opt);
          const optTitle = optMap ? String(optMap.title ?? optMap.label ?? optMap.value ?? "") : String(opt);
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
        const renderSwatches = (activeVal: Value, disabled: boolean) => {
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
        sub.append(head);
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
          const visible = stateStr !== "hidden" && isDataFormControlVisible(c, valMap);
          cell.style.display = visible ? "" : "none";
          cell.hidden = !visible;
          const disabled = !formEnabled || stateStr === "disabled";
          syncControl(valMap[name], disabled);
        },
      });
    }
    root.append(grid);
  };

  const update = (next: Record<string, Value>) => {
    currentProps = next;
    const schema = parseDataFormSchema(next.schema ?? []);
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
  };
  update(props);
  return { update, destroy: () => root.remove() };
};

export const STANDARD_COMPONENT_LIST: StandardComponent[] = [
  {
    decl: labelDecl as ComponentDecl,
    folder: "Label",
    create: createLabel,
    styleText: `.vf-std-label { display: flex; align-items: center; width: 100%; height: 100%; color: var(--color-text, #1e293b); font-size: 13px; line-height: 1.4; overflow: hidden; text-overflow: ellipsis; overflow-wrap: anywhere; }
.vf-std-label[data-align="center"] { justify-content: center; text-align: center; }
.vf-std-label[data-align="right"] { justify-content: flex-end; text-align: right; }
.vf-std-label[data-tone="title"] { font-size: 15px; font-weight: 600; color: var(--color-heading, #0f172a); }
.vf-std-label[data-tone="muted"] { color: var(--color-muted, #64748b); font-size: 12px; }
.vf-std-label[data-tone="error"] { color: var(--color-error, #dc2626); }
.vf-std-label[data-tone="accent"] { color: var(--color-primary, #2563eb); font-weight: 500; }`,
  },
  {
    decl: buttonDecl as ComponentDecl,
    folder: "Button",
    create: createButton,
    styleText: `.vf-std-button { width: 100%; height: 100%; padding: 0 6px; border: 1px solid var(--color-border, #cbd5e1); border-radius: 6px; background: var(--color-surface, #ffffff); color: var(--color-text, #1e293b); font: inherit; font-size: 12px; font-weight: 500; cursor: pointer; display: inline-flex; align-items: center; justify-content: center; gap: 4px; transition: background 0.12s, border-color 0.12s, box-shadow 0.12s, transform 0.06s; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.vf-std-button[data-has-icon="true"] { padding: 0 4px; gap: 3px; }
.vf-std-button[data-variant="primary"], .vf-std-button[data-checked="true"] { border-color: var(--color-primary, #2563eb); background: var(--color-primary, #2563eb); color: #ffffff; }
.vf-std-button[data-variant="danger"] { border-color: #ef4444; background: #fef2f2; color: #dc2626; }
.vf-std-button:hover:not(:disabled) { border-color: var(--color-primary, #2563eb); box-shadow: 0 0 0 2px rgba(37, 99, 235, 0.18); }
.vf-std-button:active:not(:disabled) { transform: scale(0.98); }
.vf-std-button:disabled { opacity: 0.45; cursor: not-allowed; }`,
  },
  {
    decl: textInputDecl as ComponentDecl,
    folder: "TextInput",
    create: createTextInput,
    styleText: `.vf-std-input { box-sizing: border-box; width: 100%; height: 100%; padding: 0 10px; border: 1px solid var(--color-border, #cbd5e1); border-radius: 6px; background: var(--color-surface, #ffffff); color: var(--color-text, #1e293b); font: inherit; font-size: 13px; outline: none; }
.vf-std-input:focus { border-color: var(--color-primary, #2563eb); box-shadow: 0 0 0 2px rgba(37, 99, 235, 0.18); }
.vf-std-input:disabled { opacity: 0.5; background: #f8fafc; }`,
  },
  {
    decl: textAreaDecl as ComponentDecl,
    folder: "TextArea",
    create: createTextArea,
    styleText: `.vf-std-textarea { box-sizing: border-box; width: 100%; height: 100%; padding: 6px 8px; border: 1px solid var(--color-border, #cbd5e1); border-radius: 6px; background: var(--color-surface, #ffffff); color: var(--color-text, #1e293b); font: inherit; font-size: 12px; line-height: 1.4; resize: none; outline: none; }
.vf-std-textarea:focus { border-color: var(--color-primary, #2563eb); box-shadow: 0 0 0 2px rgba(37, 99, 235, 0.18); }
.vf-std-textarea:disabled { opacity: 0.5; }`,
  },
  {
    decl: spinBoxDecl as ComponentDecl,
    folder: "SpinBox",
    create: createSpinBox,
    styleText: `.vf-std-spinbox { box-sizing: border-box; display: flex; align-items: center; width: 100%; height: 100%; padding: 0 8px; border: 1px solid var(--color-border, #cbd5e1); border-radius: 6px; background: var(--color-surface, #ffffff); color: var(--color-text, #1e293b); }
.vf-std-spinbox:focus-within { border-color: var(--color-primary, #2563eb); box-shadow: 0 0 0 2px rgba(37, 99, 235, 0.18); }
.vf-std-spinbox-input { flex: 1; min-width: 0; height: 100%; border: none; background: transparent; color: inherit; font: inherit; font-size: 13px; outline: none; }
.vf-std-spinbox-suffix { margin-left: 4px; color: var(--color-muted, #64748b); font-size: 12px; white-space: nowrap; }`,
  },
  {
    decl: sliderDecl as ComponentDecl,
    folder: "Slider",
    create: createSlider,
    styleText: `.vf-std-slider { box-sizing: border-box; display: flex; align-items: center; justify-content: center; width: 100%; height: 100%; }
.vf-std-slider-input { width: 100%; margin: 0; accent-color: var(--color-primary, #2563eb); cursor: pointer; }
.vf-std-slider[data-orientation="vertical"] .vf-std-slider-input { writing-mode: vertical-lr; width: auto; height: 100%; }`,
  },
  {
    decl: scrollBarDecl as ComponentDecl,
    folder: "ScrollBar",
    create: createScrollBar,
    styleText: `.vf-std-scrollbar { box-sizing: border-box; position: relative; width: 100%; height: 100%; border: 1px solid var(--color-border, #cbd5e1); border-radius: 4px; background: var(--color-surface, #f1f5f9); overflow: hidden; cursor: pointer; user-select: none; }
.vf-std-scrollbar-thumb { position: absolute; border-radius: 3px; background: var(--color-muted, #94a3b8); }
.vf-std-scrollbar[data-disabled="true"] { opacity: 0.5; pointer-events: none; }`,
  },
  {
    decl: comboBoxDecl as ComponentDecl,
    folder: "ComboBox",
    create: createComboBox,
    styleText: `.vf-std-combobox { box-sizing: border-box; width: 100%; height: 100%; padding: 0 8px; border: 1px solid var(--color-border, #cbd5e1); border-radius: 6px; background: var(--color-surface, #ffffff); color: var(--color-text, #1e293b); font: inherit; font-size: 13px; outline: none; cursor: pointer; }
.vf-std-combobox:focus { border-color: var(--color-primary, #2563eb); box-shadow: 0 0 0 2px rgba(37, 99, 235, 0.18); }
.vf-std-combobox:disabled { opacity: 0.5; cursor: not-allowed; }`,
  },
  {
    decl: checkBoxDecl as ComponentDecl,
    folder: "CheckBox",
    create: createCheckBox,
    styleText: `.vf-std-checkbox { display: flex; align-items: center; gap: 8px; width: 100%; height: 100%; color: var(--color-text, #1e293b); font-size: 13px; cursor: pointer; user-select: none; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.vf-std-checkbox input { width: 15px; height: 15px; margin: 0; accent-color: var(--color-primary, #2563eb); cursor: inherit; }
.vf-std-checkbox[data-disabled="true"] { opacity: 0.5; cursor: not-allowed; }`,
  },
  {
    decl: tabBarDecl as ComponentDecl,
    folder: "TabBar",
    create: createTabBar,
    styleText: `.vf-std-tabbar { box-sizing: border-box; display: flex; align-items: stretch; gap: 2px; width: 100%; height: 100%; padding: 2px 2px 0 2px; border-bottom: 1px solid var(--color-border, #cbd5e1); overflow: hidden; }
.vf-std-tabbar-tab { flex: 1 1 0; min-width: 0; height: 100%; padding: 0 4px; border: 1px solid var(--color-border, #cbd5e1); border-bottom: none; border-radius: 5px 5px 0 0; background: transparent; color: var(--color-muted, #64748b); font: inherit; font-size: 12px; cursor: pointer; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.vf-std-tabbar-tab.active { background: var(--color-primary, #2563eb); border-color: var(--color-primary, #60a5fa); color: #ffffff; font-weight: 600; }`,
  },
  {
    decl: listDecl as ComponentDecl,
    folder: "List",
    create: createList,
    styleText: `.vf-std-list { box-sizing: border-box; display: flex; flex-direction: column; gap: 1px; width: 100%; height: 100%; padding: 3px; border: 1px solid var(--color-border, #cbd5e1); border-radius: 6px; background: var(--color-surface, #ffffff); overflow-y: auto; }
.vf-std-list[data-disabled="true"] { opacity: 0.5; pointer-events: none; }
.vf-std-list-empty { display: flex; align-items: center; justify-content: center; height: 100%; color: var(--color-muted, #64748b); font-size: 12px; }
.vf-std-list-row { display: flex; align-items: center; gap: 6px; min-height: 20px; padding: 0 6px; border-radius: 4px; color: var(--color-text, #1e293b); font-size: 12px; cursor: pointer; user-select: none; }
.vf-std-list-row:hover { background: rgba(148, 163, 184, 0.15); }
.vf-std-list-row.selected { background: var(--color-primary, #2563eb); color: #ffffff; font-weight: 600; }
.vf-std-list-row.muted { opacity: 0.45; }
.vf-std-list-check { font-size: 12px; color: #94a3b8; cursor: pointer; }
.vf-std-list-check.on { color: var(--color-primary, #2563eb); }
.vf-std-list-text { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.vf-std-list-badge { padding: 1px 4px; border-radius: 3px; background: rgba(148, 163, 184, 0.2); color: inherit; font-size: 10px; }`,
  },
  {
    decl: treeDecl as ComponentDecl,
    folder: "Tree",
    create: createTree,
    styleText: `.vf-std-tree { box-sizing: border-box; display: flex; flex-direction: column; gap: 2px; width: 100%; height: 100%; padding: 4px; border: 1px solid var(--color-border, #cbd5e1); border-radius: 6px; background: var(--color-surface, #ffffff); overflow-y: auto; user-select: none; }
.vf-std-tree[data-disabled="true"] { opacity: 0.5; pointer-events: none; }
.vf-std-tree-empty { display: flex; align-items: center; justify-content: center; height: 100%; color: var(--color-muted, #64748b); font-size: 12px; }
.vf-std-tree-row { display: flex; align-items: center; gap: 5px; min-height: 24px; padding-right: 6px; border-radius: 4px; color: var(--color-text, #1e293b); font-size: 12px; cursor: pointer; }
.vf-std-tree-row.selected { background: var(--color-primary, #2563eb); color: #ffffff; }
.vf-std-tree-row.unchecked .vf-std-tree-text { opacity: 0.55; }
.vf-std-tree-caret { width: 14px; height: 14px; display: inline-flex; align-items: center; justify-content: center; font-size: 10px; flex-shrink: 0; }
.vf-std-tree-check { width: 13px; height: 13px; margin: 0; accent-color: var(--color-primary, #2563eb); cursor: pointer; flex-shrink: 0; }
.vf-std-tree-text { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.vf-std-tree-text.group { font-weight: 600; }
.vf-std-tree-badge { padding: 1px 4px; border-radius: 3px; background: rgba(148, 163, 184, 0.2); font-size: 10px; flex-shrink: 0; }`,
  },
  {
    decl: dialogButtonBoxDecl as ComponentDecl,
    folder: "DialogButtonBox",
    create: createDialogButtonBox,
    styleText: `.vf-std-buttonbox { box-sizing: border-box; display: flex; align-items: center; justify-content: flex-end; gap: 8px; width: 100%; height: 100%; }
.vf-std-buttonbox-btn { min-width: 72px; height: 28px; padding: 0 12px; border: 1px solid var(--color-border, #cbd5e1); border-radius: 5px; background: var(--color-surface, #ffffff); color: var(--color-text, #1e293b); font: inherit; font-size: 12px; cursor: pointer; }
.vf-std-buttonbox-btn.primary { border-color: var(--color-primary, #2563eb); background: var(--color-primary, #2563eb); color: #ffffff; font-weight: 500; }`,
  },
  {
    decl: separatorDecl as ComponentDecl,
    folder: "Separator",
    create: createSeparator,
    styleText: `.vf-std-separator { display: flex; align-items: center; justify-content: center; width: 100%; height: 100%; }
.vf-std-separator::after { content: ""; display: block; width: 100%; height: 1px; background: var(--color-border, #e2e8f0); }
.vf-std-separator[data-orientation="vertical"]::after { width: 1px; height: 100%; }`,
  },
  {
    decl: floatBoxDecl as ComponentDecl,
    folder: "FloatBox",
    create: createFloatBox,
    styleText: `.vf-std-floatbox { box-sizing: border-box; display: flex; flex-direction: column; width: 100%; height: 100%; border: 1px solid var(--color-border, #cbd5e1); border-radius: 6px; background: var(--color-surface, #ffffff); pointer-events: none; overflow: hidden; }
.vf-std-floatbox[data-floating="true"] { border-color: var(--color-primary, #2563eb); box-shadow: 0 12px 28px rgba(15, 23, 42, 0.35); }
.vf-std-floatbox-header { box-sizing: border-box; display: flex; align-items: center; justify-content: space-between; gap: 6px; padding: 0 8px; height: 24px; background: rgba(148, 163, 184, 0.14); border-bottom: 1px solid var(--color-border, #cbd5e1); color: var(--color-text, #1e293b); font-size: 12px; font-weight: 600; user-select: none; pointer-events: auto; cursor: grab; }
.vf-std-floatbox-title { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.vf-std-floatbox-toggle { padding: 1px 6px; border: 1px solid var(--color-border, #cbd5e1); border-radius: 4px; background: var(--color-surface, #ffffff); color: var(--color-text, #1e293b); font-size: 11px; line-height: 1.3; cursor: pointer; }
.vf-std-floatbox-toggle:hover:not(:disabled) { border-color: var(--color-primary, #2563eb); color: var(--color-primary, #2563eb); }
.vf-std-floatbox-body { flex: 1; min-height: 0; pointer-events: none; }`,
  },
  {
    decl: spacerDecl as ComponentDecl,
    folder: "Spacer",
    create: createSpacer,
    styleText: `.vf-std-spacer { width: 100%; height: 100%; pointer-events: none; }`,
  },
  {
    decl: dataFormDecl as ComponentDecl,
    folder: "DataForm",
    create: createDataForm,
    styleText: `.vf-std-dataform { box-sizing: border-box; width: 100%; height: 100%; padding: 2px 4px; border: 1px solid var(--color-border, #cbd5e1); border-radius: 6px; background: var(--color-surface, #ffffff); color: var(--color-text, #1e293b); font-size: 11px; overflow: auto; }
.vf-std-dataform-empty { display: flex; align-items: center; justify-content: center; height: 100%; color: var(--color-muted, #64748b); font-size: 12px; }
.vf-std-dataform-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); align-items: center; }
.vf-std-dataform-cell { display: flex; flex-direction: column; gap: 2px; min-width: 0; }
.vf-std-dataform-head { display: flex; justify-content: space-between; align-items: center; font-size: 11px; line-height: 1.1; }
.vf-std-dataform-val { color: var(--color-primary, #2563eb); font-variant-numeric: tabular-nums; font-weight: 500; }
.vf-std-dataform-slider { flex: 1; min-width: 0; height: 12px; margin: 0; accent-color: var(--color-primary, #2563eb); }
.vf-std-dataform-row { display: flex; align-items: center; justify-content: space-between; gap: 6px; }
.vf-std-dataform-select { flex: 1; min-width: 0; height: 22px; padding: 0 4px; border: 1px solid var(--color-border, #cbd5e1); border-radius: 4px; background: var(--color-surface, #ffffff); color: var(--color-text, #1e293b); font-size: 11px; }
.vf-std-dataform-check { display: flex; align-items: center; gap: 6px; height: 20px; cursor: pointer; }
.vf-std-dataform-btn { height: 19px; padding: 0 6px; border: 1px solid var(--color-border, #cbd5e1); border-radius: 4px; background: var(--color-surface, #ffffff); color: var(--color-text, #1e293b); font-size: 11px; cursor: pointer; display: inline-flex; align-items: center; justify-content: center; gap: 4px; }
.vf-std-dataform-btn[data-active="true"] { border-color: var(--color-primary, #2563eb); box-shadow: 0 0 0 1px var(--color-primary, #2563eb); font-weight: 600; }
.vf-std-dataform-swatch-dot { width: 10px; height: 10px; border-radius: 50%; border: 1px solid rgba(148, 163, 184, 0.6); flex-shrink: 0; }
.vf-std-dataform-palette { display: flex; flex-direction: column; gap: 3px; }
.vf-std-dataform-swatches { display: flex; flex-wrap: wrap; align-items: center; gap: 4px; }
.vf-std-dataform-swatch-btn { width: 18px; height: 18px; padding: 0; border-radius: 3px; border: 1px solid var(--color-border, #cbd5e1); cursor: pointer; }
.vf-std-dataform-swatch-btn[data-active="true"] { border: 2px solid var(--color-primary, #2563eb); }
.vf-std-dataform-subwindow { display: flex; flex-direction: column; gap: 1px; padding: 1px 6px; border: 1px dashed var(--color-primary, #2563eb); border-radius: 4px; background: rgba(37, 99, 235, 0.08); font-size: 11px; line-height: 1.15; cursor: pointer; }
.vf-std-dataform-subwindow-head { display: flex; justify-content: space-between; align-items: center; font-weight: 600; color: var(--color-primary, #2563eb); }`,
  },
];

export const STANDARD_COMPONENTS = new Map<string, StandardComponent>(
  STANDARD_COMPONENT_LIST.map((item) => [item.decl.type, item]),
);

// ---------- 状态图片（正常、悬停、按下、选中、不可用）渲染器 ----------

let svgScopeCounter = 0;

function parseSvgMetrics(svgText: string): { vx: number; vy: number; vw: number; vh: number; inner: string } {
  const vbMatch = svgText.match(/viewBox\s*=\s*["']\s*([-\d.]+)[,\s]+([-\d.]+)[,\s]+([-\d.]+)[,\s]+([-\d.]+)\s*["']/i);
  const wMatch = svgText.match(/\bwidth\s*=\s*["']\s*([\d.]+)/i);
  const hMatch = svgText.match(/\bheight\s*=\s*["']\s*([\d.]+)/i);
  const vx = vbMatch ? Number(vbMatch[1]) : 0;
  const vy = vbMatch ? Number(vbMatch[2]) : 0;
  const vw = vbMatch ? Number(vbMatch[3]) : wMatch ? Number(wMatch[1]) : 120;
  const vh = vbMatch ? Number(vbMatch[4]) : hMatch ? Number(hMatch[1]) : 32;
  const openEnd = svgText.indexOf(">", svgText.indexOf("<svg"));
  const closeStart = svgText.lastIndexOf("</svg>");
  const inner = openEnd >= 0 && closeStart > openEnd ? svgText.slice(openEnd + 1, closeStart) : "";
  return {
    vx: Number.isFinite(vx) ? vx : 0,
    vy: Number.isFinite(vy) ? vy : 0,
    vw: Number.isFinite(vw) && vw > 0 ? vw : 120,
    vh: Number.isFinite(vh) && vh > 0 ? vh : 32,
    inner,
  };
}

function scopeSvgIds(inner: string, prefix: string): string {
  return inner
    .replace(/\bid\s*=\s*["']([^"']+)["']/g, `id="${prefix}_$1"`)
    .replace(/url\(#([^)]+)\)/g, `url(#${prefix}_$1)`)
    .replace(/\b(xlink:href|href)\s*=\s*["']#([^"']+)["']/g, `$1="#${prefix}_$2"`);
}

function createSliceSvgElement(vx: number, vy: number, vw: number, vh: number, inner: string, prefix: string): SVGSVGElement {
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("viewBox", `${vx} ${vy} ${vw} ${vh}`);
  svg.setAttribute("preserveAspectRatio", "none");
  svg.style.width = "100%";
  svg.style.height = "100%";
  svg.style.display = "block";
  svg.style.overflow = "hidden";
  svg.innerHTML = scopeSvgIds(inner, prefix);
  return svg;
}

/**
 * 将一张 SVG 状态图按九宫格切线 [top, right, bottom, left] 渲染到容器中，保证拉伸时四角不变形。
 */
export function renderNinePatchSvg(
  container: HTMLElement,
  svgText: string,
  slice: [number, number, number, number],
): void {
  container.replaceChildren();
  if (!svgText.trim()) return;
  const { vx, vy, vw, vh, inner } = parseSvgMetrics(svgText);
  const [top, right, bottom, left] = slice;
  const uid = `vfs_${++svgScopeCounter}`;
  if ((top === 0 && right === 0 && bottom === 0 && left === 0) || left + right >= vw || top + bottom >= vh) {
    container.append(createSliceSvgElement(vx, vy, vw, vh, inner, uid));
    return;
  }
  const grid = document.createElement("div");
  grid.className = "vf-nine-patch";
  grid.style.cssText = `display:grid;width:100%;height:100%;grid-template-columns:${left}px minmax(0,1fr) ${right}px;grid-template-rows:${top}px minmax(0,1fr) ${bottom}px;`;
  const xs = [vx, vx + left, vx + vw - right];
  const ws = [left, Math.max(0.01, vw - left - right), right];
  const ys = [vy, vy + top, vy + vh - bottom];
  const hs = [top, Math.max(0.01, vh - top - bottom), bottom];
  for (let r = 0; r < 3; r++) {
    for (let c = 0; c < 3; c++) {
      const cell = document.createElement("div");
      cell.className = "vf-nine-patch-cell";
      cell.style.cssText = "width:100%;height:100%;overflow:hidden;position:relative;";
      if (ws[c] > 0 && hs[r] > 0) {
        cell.append(createSliceSvgElement(xs[c], ys[r], ws[c], hs[r], inner, `${uid}_${r}_${c}`));
      }
      grid.append(cell);
    }
  }
  container.append(grid);
}

/**
 * 挂载状态图片控件：
 * - 5 种固定状态（normal / hover / pressed / selected / disabled）渲染同一套 SVG 文件；
 * - 按 slice 做九宫格拉伸，四角不变形；
 * - 状态切换时带透明度过渡（transitionMs）；
 * - 文字由控件在顶层单独绘制，不写进 SVG。
 */
export function mountStateImageControl(
  el: HTMLElement,
  ctx: {
    props: Record<string, Value>;
    emit: (event: string, value?: Value) => void;
    mode?: "design" | "preview" | "edit";
    stateImages: StateImagesDecl;
    svgFiles: Record<string, string>;
  },
): ComponentInstance {
  const { stateImages, svgFiles, emit } = ctx;
  const isEditMode = (): boolean =>
    ctx.mode === "design" || ctx.mode === "edit" || Boolean(el.closest("#window.edit"));
  const slice = normalizeStateImageSlice(stateImages);
  const transitionMs = typeof stateImages.transitionMs === "number" && Number.isFinite(stateImages.transitionMs)
    ? Math.max(0, stateImages.transitionMs)
    : 120;

  const root = document.createElement("div");
  root.className = "vf-state-image-root";
  root.style.cssText =
    "position:relative;width:100%;height:100%;display:flex;align-items:center;justify-content:center;user-select:none;overflow:hidden;cursor:pointer;";

  const layersWrap = document.createElement("div");
  layersWrap.className = "vf-state-image-layers";
  layersWrap.style.cssText = "position:absolute;inset:0;pointer-events:none;";

  const layerEls = new Map<string, HTMLElement>();
  const fakeDecl: ComponentDecl = { type: "StateImage", stateImages };

  for (const key of STATE_IMAGE_KEYS) {
    const file = stateImages[key] || (key === "pressed" && stateImages.hover ? stateImages.hover : stateImages.normal);
    const layer = document.createElement("div");
    layer.className = "vf-state-image-layer";
    layer.dataset.stateKey = key;
    layer.dataset.svgFile = file;
    layer.style.cssText = `position:absolute;inset:0;opacity:0;transition:opacity ${transitionMs}ms ease;pointer-events:none;`;
    renderNinePatchSvg(layer, svgFiles[file] ?? svgFiles[stateImages.normal] ?? "", slice);
    layersWrap.append(layer);
    layerEls.set(key, layer);
  }

  const textSpan = document.createElement("span");
  textSpan.className = "vf-state-image-text";
  textSpan.style.cssText =
    "position:relative;z-index:1;padding:0 10px;font-size:13px;font-weight:500;line-height:1.2;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;pointer-events:none;transition:color 0.12s ease;";

  root.append(layersWrap, textSpan);
  el.append(root);

  let currentProps: Record<string, Value> = { ...ctx.props };
  let hovered = false;
  let pressed = false;

  const syncVisual = () => {
    if (root.parentElement !== el) el.append(root);
    const edit = isEditMode();
    if (edit) {
      hovered = false;
      pressed = false;
    }
    const enabled = currentProps.enabled !== false;
    if (!enabled) pressed = false;
    const selected = Boolean(
      currentProps.selected ?? currentProps.checked ?? (currentProps.variant === "primary"),
    );
    const resolved = resolveStateImage(fakeDecl, {
      enabled,
      pressed: (!edit && pressed) || currentProps.pressed === true,
      hovered: (!edit && hovered) || currentProps.hovered === true,
      selected,
    }) ?? { state: "normal" as const, file: stateImages.normal };

    for (const [k, layer] of layerEls) {
      layer.style.opacity = k === resolved.state ? "1" : "0";
    }
    root.dataset.state = resolved.state;
    root.dataset.stateImage = resolved.file;
    el.dataset.vfState = resolved.state;
    el.dataset.vfStateImage = resolved.file;
    root.style.cursor = edit ? "default" : enabled ? "pointer" : "not-allowed";

    textSpan.textContent = String(currentProps.text ?? "");
    if (typeof currentProps.textColor === "string" && currentProps.textColor) {
      textSpan.style.color = currentProps.textColor;
    } else if (!enabled) {
      textSpan.style.color = "#94a3b8";
    } else if (resolved.state === "selected" || resolved.state === "pressed") {
      textSpan.style.color = "#ffffff";
    } else {
      textSpan.style.color = "#1e293b";
    }
  };

  root.addEventListener("mouseenter", () => {
    if (isEditMode() || currentProps.enabled === false) return;
    hovered = true;
    syncVisual();
  });
  root.addEventListener("mouseleave", () => {
    hovered = false;
    pressed = false;
    syncVisual();
  });
  root.addEventListener("mousedown", (e) => {
    if (isEditMode() || e.button !== 0 || currentProps.enabled === false) return;
    pressed = true;
    syncVisual();
  });
  root.addEventListener("mouseup", (e) => {
    if (e.button !== 0) return;
    pressed = false;
    syncVisual();
  });
  root.addEventListener("click", () => {
    if (isEditMode() || currentProps.enabled === false) return;
    if (currentProps.checkable) emit("toggle", !currentProps.checked);
  });

  const update = (next: Record<string, Value>) => {
    currentProps = next;
    syncVisual();
  };
  update(ctx.props);

  return {
    update,
    destroy: () => {
      delete el.dataset.vfState;
      delete el.dataset.vfStateImage;
      root.remove();
    },
  };
}
