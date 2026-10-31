// 检查设计项目：画布、逻辑、说明、函数、动作和效果之间能不能对上。结果显示在工具的问题栏里，导出前也会检查一遍。
import { dockIdsOfLayout } from "./dock.ts";
import { parseAssignment, parseExpr, pathsIn } from "./expr.ts";
import {
  allStateLeaves,
  BUILTIN_TYPES,
  businessEvent,
  childrenOfNode,
  declFor,
  EFFECT_KINDS,
  ensureComponentSvgFiles,
  findCondition,
  findMenuItem,
  findStateLeaf,
  getSection,
  isBusinessState,
  isPaintEffect,
  isTransitionEffect,
  MOUSE_EVENTS,
  parseDocRef,
  requestEvent,
  STANDARD_COMPONENTS,
  STATE_IMAGE_KEYS,
  walkMenuItems,
  type ConditionDef,
  type Problem,
  type Project,
  type Step,
} from "./model.ts";

/** 步骤出现在哪里：普通事件里，还是业务事件的模拟步骤里。两处的规则不同。 */
type StepPlace = "normal" | "simulate";

/** 系统保留名称（内置只读状态、事件临时变量、表达式关键字、内置节点类型）：不能用作控件 ID、状态根名、全局条件名或菜单项 ID。 */
export const RESERVED_IDENTIFIER_NAMES = new Set<string>([
  "clock",
  "value",
  "param",
  "result",
  "error",
  "self",
  "true",
  "false",
  "null",
  "undefined",
  "and",
  "or",
  "not",
  ...BUILTIN_TYPES,
]);

const FORBIDDEN_DOM_IDENTIFIERS = [
  "document",
  "window",
  "globalThis",
  "self",
  "top",
  "parent",
  "frames",
  "Function",
  "eval",
  "HTMLElement",
  "SVGElement",
  "Element",
  "Node",
  "navigator",
  "location",
  "localStorage",
  "sessionStorage",
  "indexedDB",
  "XMLHttpRequest",
  "fetch",
  "WebSocket",
  "Worker",
  "MutationObserver",
  "ResizeObserver",
  "IntersectionObserver",
  "CustomEvent",
  "EventTarget",
  "DOMParser",
  "Image",
  "Audio",
  "alert",
  "confirm",
  "prompt",
  "setInterval",
  "requestAnimationFrame",
  "cancelAnimationFrame",
  "querySelector",
  "querySelectorAll",
  "getElementById",
  "getElementsByClassName",
  "getElementsByTagName",
  "createElement",
  "createElementNS",
  "innerHTML",
  "outerHTML",
  "addEventListener",
  "removeEventListener",
];

const REGEX_PRECEDING_KEYWORDS = new Set([
  "return",
  "case",
  "throw",
  "delete",
  "void",
  "typeof",
  "yield",
  "await",
  "in",
  "of",
  "instanceof",
  "new",
  "else",
  "do",
]);

const COND_PAREN_KEYWORDS = new Set(["if", "while", "for", "with"]);

function canStartRegexLiteral(prevToken: string): boolean {
  if (!prevToken) return true;
  if (prevToken === "cond)") return true;
  if (REGEX_PRECEDING_KEYWORDS.has(prevToken)) return true;
  if (prevToken === ")" || prevToken === "]" || prevToken === "++" || prevToken === "--" || prevToken === "literal") {
    return false;
  }
  if (/^[A-Za-z_$]/.test(prevToken) || /^[0-9]/.test(prevToken)) {
    return false;
  }
  return true;
}

function stripCommentsAndStringText(source: string): string {
  let out = "";
  let i = 0;
  let braceDepth = 0;
  let prevToken = "";
  const templateBraceStack: number[] = [];
  const parenCondStack: boolean[] = [];

  const scanTemplateText = () => {
    while (i < source.length) {
      if (source[i] === "\\") {
        i += 2;
        continue;
      }
      if (source[i] === "`") {
        i++;
        out += "''";
        prevToken = "literal";
        return;
      }
      if (source[i] === "$" && source[i + 1] === "{") {
        i += 2;
        templateBraceStack.push(braceDepth);
        out += " ( ";
        prevToken = "(";
        return;
      }
      i++;
    }
    out += "''";
    prevToken = "literal";
  };

  while (i < source.length) {
    const ch = source[i];
    const next = source[i + 1];
    if (/\s/.test(ch)) {
      out += ch;
      i++;
      continue;
    }
    if (ch === "/" && next === "/") {
      i += 2;
      while (i < source.length && source[i] !== "\n") i++;
      out += " ";
      continue;
    }
    if (ch === "/" && next === "*") {
      i += 2;
      while (i < source.length && !(source[i] === "*" && source[i + 1] === "/")) i++;
      if (i < source.length) i += 2;
      out += " ";
      continue;
    }
    if (ch === "/" && canStartRegexLiteral(prevToken)) {
      // 扫描正则表达式字面量 /pattern/flags（含 [...] 字符类与 \ 转义）
      let j = i + 1;
      let inCharClass = false;
      let closed = false;
      while (j < source.length) {
        const c = source[j];
        if (c === "\\") {
          if (j + 1 < source.length && source[j + 1] !== "\n" && source[j + 1] !== "\r") {
            j += 2;
            continue;
          }
          break;
        }
        if (c === "\n" || c === "\r") break;
        if (c === "[") {
          inCharClass = true;
          j++;
          continue;
        }
        if (c === "]") {
          inCharClass = false;
          j++;
          continue;
        }
        if (c === "/" && !inCharClass) {
          j++;
          while (j < source.length && /[A-Za-z]/.test(source[j])) j++;
          closed = true;
          break;
        }
        j++;
      }
      if (closed) {
        out += " null ";
        prevToken = "literal";
        i = j;
        continue;
      }
    }
    if (ch === "'" || ch === '"') {
      const quote = ch;
      i++;
      while (i < source.length) {
        if (source[i] === "\\") {
          i += 2;
          continue;
        }
        if (source[i] === quote) {
          i++;
          break;
        }
        i++;
      }
      out += "''";
      prevToken = "literal";
      continue;
    }
    if (ch === "`") {
      i++;
      out += "''";
      scanTemplateText();
      continue;
    }
    if (ch === "{") {
      braceDepth++;
      out += "{";
      prevToken = "{";
      i++;
      continue;
    }
    if (ch === "}") {
      if (templateBraceStack.length > 0 && braceDepth === templateBraceStack[templateBraceStack.length - 1]) {
        templateBraceStack.pop();
        i++;
        out += " ) ";
        scanTemplateText();
        continue;
      }
      if (braceDepth > 0) braceDepth--;
      out += "}";
      prevToken = "}";
      i++;
      continue;
    }
    if (ch === "(") {
      parenCondStack.push(COND_PAREN_KEYWORDS.has(prevToken));
      out += "(";
      prevToken = "(";
      i++;
      continue;
    }
    if (ch === ")") {
      const isCond = parenCondStack.pop() ?? false;
      out += ")";
      prevToken = isCond ? "cond)" : ")";
      i++;
      continue;
    }
    if ((ch === "+" && next === "+") || (ch === "-" && next === "-")) {
      out += ch + next;
      prevToken = ch + next;
      i += 2;
      continue;
    }
    if (/[A-Za-z_$]/.test(ch) || (ch === "\\" && next === "u")) {
      let start = i;
      while (
        i < source.length &&
        (/[A-Za-z0-9_$]/.test(source[i]) ||
          (source[i] === "\\" && source[i + 1] === "u") ||
          (source[i - 1] === "u" && source[i] === "{") ||
          (source[i] === "}" && /\\u\{[0-9a-fA-F]+$/.test(source.slice(start, i))))
      ) {
        i++;
      }
      const word = source.slice(start, i);
      out += word;
      prevToken = word;
      continue;
    }
    if (/[0-9]/.test(ch)) {
      let start = i;
      while (i < source.length && /[0-9a-fA-FxXoObB._]/.test(source[i])) i++;
      out += source.slice(start, i);
      prevToken = "literal";
      continue;
    }
    out += ch;
    prevToken = ch;
    i++;
  }
  return out;
}

export function findForbiddenDomUsage(source: string): string | null {
  // 单遍词法扫描：保留模板字符串 ${...} 内的表达式，去掉普通字符串与注释，再解码 Unicode 转义后检查禁用标识符
  const stripped = stripCommentsAndStringText(source)
    .replace(/\\u\{([0-9a-fA-F]{1,6})\}/g, (_, hex) => {
      const cp = parseInt(hex, 16);
      return Number.isFinite(cp) && cp <= 0x10ffff ? String.fromCodePoint(cp) : "";
    })
    .replace(/\\u([0-9a-fA-F]{4})/g, (_, hex) => String.fromCharCode(parseInt(hex, 16)));
  for (const word of FORBIDDEN_DOM_IDENTIFIERS) {
    const re = new RegExp(`\\b${word}\\b`);
    if (re.test(stripped)) return word;
  }
  return null;
}

/**
 * 检查 SVG 源码中是否使用了在浏览器能画但 Qt QSvgRenderer 会忽略或表现不一致的写法：
 * - <feDropShadow>
 * - <feComponentTransfer>
 * - clip-path / <clipPath>
 */
export function checkSvgCompatibility(svgText: string, fileLabel: string, nodeId?: string): Problem[] {
  const issues: Problem[] = [];
  if (/<\s*feDropShadow\b|\bfeDropShadow\b/i.test(svgText)) {
    issues.push({
      level: "hint",
      message: `${fileLabel}：SVG 使用了 Qt QSvgRenderer 不支持的 <feDropShadow>；替代做法：改用 <feGaussianBlur> + <feOffset> + <feMerge>，或者在亮色图形下面垫一层带高斯模糊的深色图形`,
      ...(nodeId ? { nodeId } : {}),
    });
  }
  if (/<\s*feComponentTransfer\b|\bfeComponentTransfer\b/i.test(svgText)) {
    issues.push({
      level: "hint",
      message: `${fileLabel}：SVG 使用了 Qt QSvgRenderer 不支持的 <feComponentTransfer>；替代做法：直接在图形或阴影层上写 opacity / fill-opacity`,
      ...(nodeId ? { nodeId } : {}),
    });
  }
  if (/<\s*clipPath\b|\bclip-path\s*[:=]/i.test(svgText)) {
    issues.push({
      level: "hint",
      message: `${fileLabel}：SVG 使用了在 Qt QSvgRenderer 中受限的 clip-path；替代做法：直接把图形画成目标轮廓，或用 <mask>`,
      ...(nodeId ? { nodeId } : {}),
    });
  }
  return issues;
}

export function check(project: Project): Problem[] {
  ensureComponentSvgFiles(project);
  const problems: Problem[] = [];
  const { scene, logic } = project;
  const error = (message: string, nodeId?: string) => problems.push({ level: "error", message, nodeId });
  const hint = (message: string, nodeId?: string) => problems.push({ level: "hint", message, nodeId });

  const isKnownPath = (path: string, extraNames: Set<string>): boolean => {
    if (path === "clock" || path === "clock.ms" || path === "clock.sec") return true;
    if (extraNames.has(path)) return true;
    for (const extra of extraNames) {
      if (path.startsWith(extra + ".")) return true;
    }
    if (findStateLeaf(logic, path) || findCondition(logic, path)) return true;
    const parts = path.split(".");
    for (let i = parts.length - 1; i >= 1; i--) {
      const prefix = parts.slice(0, i).join(".");
      const leaf = findStateLeaf(logic, prefix);
      if (leaf && leaf.type === "object") return true;
    }
    if (path.endsWith(".length")) {
      const base = path.slice(0, -".length".length);
      const leaf = findStateLeaf(logic, base);
      if (leaf && (leaf.type === "array" || leaf.type === "string")) return true;
    }
    return false;
  };

  // 表达式里的名字必须是状态路径、条件代号或当前上下文允许的临时名字
  const checkExpr = (source: unknown, where: string, nodeId?: string, extraNames = new Set<string>()) => {
    try {
      for (const path of pathsIn(parseExpr(String(source)))) {
        if (!isKnownPath(path, extraNames)) {
          error(`${where}：未知的状态路径或条件代号“${path}”`, nodeId);
        }
      }
    } catch (e) {
      error(`${where}：${(e as Error).message}`, nodeId);
    }
  };

  const checkStructuredExpr = (raw: unknown, where: string, nodeId?: string, extraNames = new Set<string>()) => {
    if (raw === null || raw === undefined || typeof raw === "boolean" || typeof raw === "number") return;
    if (typeof raw === "string") {
      checkExpr(raw, where, nodeId, extraNames);
      return;
    }
    if (Array.isArray(raw)) {
      for (const item of raw) checkStructuredExpr(item, where, nodeId, extraNames);
      return;
    }
    if (typeof raw === "object") {
      for (const v of Object.values(raw as Record<string, unknown>)) {
        checkStructuredExpr(v, where, nodeId, extraNames);
      }
    }
  };

  const checkTargetOwnership = (target: string, where: string, place: StepPlace, nodeId?: string, requireArray = false) => {
    const leaf = findStateLeaf(logic, target);
    if (!leaf) {
      error(`${where}：目标“${target}”不是状态路径`, nodeId);
      return null;
    }
    if (requireArray && leaf.type !== "array") {
      error(`${where}：目标“${target}”不是数组状态（type: array）`, nodeId);
    }
    if (place === "normal" && isBusinessState(leaf)) {
      error(`${where}：状态“${target}”归业务管，不能在这里直接修改。请改成 emit 一个业务事件，把修改写在它的 simulate 里`, nodeId);
    } else if (place === "simulate" && !isBusinessState(leaf)) {
      error(`${where}：模拟步骤只能修改业务管的状态，“${target}”是界面自己管的。业务接上以后模拟步骤不再执行，写在这里的修改会丢`, nodeId);
    }
    return leaf;
  };

  const checkSteps = (steps: unknown, where: string, place: StepPlace, nodeId?: string, extraNames = new Set<string>()) => {
    if (!Array.isArray(steps)) return error(`${where}：这里应该是步骤列表`, nodeId);
    for (const step of steps as Step[]) {
      if ("set" in step) {
        try {
          const parsed = parseAssignment(step.set);
          checkTargetOwnership(parsed.target, `${where} 的 set`, place, nodeId, parsed.index !== undefined);
          if (parsed.index !== undefined) {
            for (const p of pathsIn(parsed.index)) {
              if (!isKnownPath(p, extraNames)) error(`${where}：未知的状态路径或条件代号“${p}”`, nodeId);
            }
          }
          for (const p of pathsIn(parsed.value)) {
            if (!isKnownPath(p, extraNames)) error(`${where}：未知的状态路径或条件代号“${p}”`, nodeId);
          }
        } catch (e) {
          error(`${where}：${(e as Error).message}`, nodeId);
        }
      } else if ("push" in step) {
        const spec: { to?: string; item?: unknown; index?: string | number } =
          typeof step.push === "string"
            ? { to: step.push, item: (step as { value?: unknown }).value, index: (step as { index?: string | number }).index }
            : step.push;
        if (!spec?.to) error(`${where}：push 缺少目标数组状态 to`, nodeId);
        else checkTargetOwnership(spec.to, `${where} 的 push`, place, nodeId, true);
        if (spec?.index !== undefined) checkExpr(spec.index, `${where} 的 push.index`, nodeId, extraNames);
        checkStructuredExpr(spec?.item, `${where} 的 push.item`, nodeId, extraNames);
      } else if ("remove" in step) {
        const spec = typeof step.remove === "string" ? { from: step.remove, index: (step as { index?: unknown }).index } : step.remove;
        if (!spec?.from) error(`${where}：remove 缺少目标数组状态 from`, nodeId);
        else checkTargetOwnership(spec.from, `${where} 的 remove`, place, nodeId, true);
        if (spec?.index === undefined) error(`${where}：remove 缺少下标 index`, nodeId);
        else checkExpr(spec.index, `${where} 的 remove.index`, nodeId, extraNames);
      } else if ("move" in step) {
        const spec =
          typeof step.move === "string"
            ? { in: step.move, from: (step as { from?: unknown }).from, to: (step as { to?: unknown }).to }
            : step.move;
        if (!spec?.in) error(`${where}：move 缺少目标数组状态 in`, nodeId);
        else checkTargetOwnership(spec.in, `${where} 的 move`, place, nodeId, true);
        if (spec?.from === undefined || spec?.to === undefined) error(`${where}：move 需要同时提供 from 和 to`, nodeId);
        else {
          checkExpr(spec.from, `${where} 的 move.from`, nodeId, extraNames);
          checkExpr(spec.to, `${where} 的 move.to`, nodeId, extraNames);
        }
      } else if ("fn" in step) {
        const spec = typeof step.fn === "string" ? { name: step.fn, args: (step as { args?: Record<string, unknown> }).args } : step.fn;
        if (!spec?.name) {
          error(`${where}：fn 缺少函数名`, nodeId);
        } else if (!project.functions?.[spec.name]) {
          error(`${where}：找不到 JS 函数“${spec.name}”（请放在 functions/${spec.name}.js）`, nodeId);
        }
        if (spec?.args) checkStructuredExpr(spec.args, `${where} 的 fn.args`, nodeId, extraNames);
      } else if ("openWindow" in step) {
        const winId = typeof step.openWindow === "string" ? step.openWindow : step.openWindow?.id;
        const winNode = winId ? scene.nodes[winId] : undefined;
        if (!winNode || (winNode.type !== "Dialog" && winNode.type !== "Window")) {
          error(`${where}：openWindow 的目标“${winId ?? ""}”不是画布中的窗口或对话框节点`, nodeId);
        }
      } else if ("closeWindow" in step) {
        const winId = step.closeWindow;
        const winNode = scene.nodes[winId];
        if (!winNode || (winNode.type !== "Dialog" && winNode.type !== "Window")) {
          error(`${where}：closeWindow 的目标“${winId}”不是画布中的窗口或对话框节点`, nodeId);
        }
      } else if ("call" in step) {
        if (!logic.actions?.[step.call]) {
          error(`${where}：call 的动作“${step.call}”没有在顶层 actions 中定义`, nodeId);
        }
      } else if ("emit" in step) {
        const spec = typeof step.emit === "string" ? { name: step.emit, args: (step as { args?: Record<string, unknown> }).args } : step.emit;
        const isBusiness = businessEvent(logic, spec.name) !== null;
        if (logic.events?.[spec.name] === undefined) {
          error(`${where}：emit 的全局事件“${spec.name}”没有在顶层 events 中定义`, nodeId);
        } else if (place === "simulate" && !isBusiness) {
          error(`${where}：模拟步骤里只能 emit 业务事件，“${spec.name}”是普通全局事件`, nodeId);
        }
        if (spec.args) checkStructuredExpr(spec.args, `${where} 的 emit.args`, nodeId, extraNames);
      } else if ("if" in step) {
        checkExpr(step.if, where, nodeId, extraNames);
        if (step.then) checkSteps(step.then, where, place, nodeId, extraNames);
        if (step.else) checkSteps(step.else, where, place, nodeId, extraNames);
      } else {
        error(`${where}：无法识别的步骤 ${JSON.stringify(step)}`, nodeId);
      }
    }
  };

  const checkConditions = (conditions: Record<string, ConditionDef> | undefined, prefix: string, nodeId?: string) => {
    for (const [name, def] of Object.entries(conditions ?? {})) {
      const id = prefix + name;
      if (def?.type === "symbol") {
        if (def.expr === undefined) error(`条件 ${id}：符号条件缺少 expr`, nodeId);
        else checkExpr(def.expr, `条件 ${id}`, nodeId);
      } else if (def?.type === "text") {
        hint(`文字条件 ${id}（“${def.text ?? ""}”）尚未改写为符号条件，预览时用开关切换`, nodeId);
      } else {
        error(`条件 ${id}：type 应为 text 或 symbol`, nodeId);
      }
    }
  };

  // 画布
  for (const [id, node] of Object.entries(scene.nodes)) {
    const childIds = childrenOfNode(node);
    for (const cid of childIds) {
      if (!scene.nodes[cid]) {
        error(`节点 ${id}：引用的子节点“${cid}”在画布中不存在`, id);
      }
    }
    if (node.layout) {
      const validLayouts = ["vbox", "hbox", "grid", "form", "stack", "flow"];
      if (!validLayouts.includes(node.layout.kind)) {
        error(`节点 ${id}：layout.kind 应为 ${validLayouts.join("、")} 之一，现在是“${node.layout.kind}”`, id);
      }
    }
    if (node.dockArea && !["left", "right", "top", "bottom"].includes(node.dockArea)) {
      error(`停靠面板 ${id}：dockArea 应为 left、right、top、bottom 之一，现在是“${node.dockArea}”`, id);
    }
    if (node.dockLayout) {
      for (const did of dockIdsOfLayout(node.dockLayout)) {
        const dn = scene.nodes[did];
        if (dn && dn.type !== "Dock") {
          error(`窗口 ${id}：dockLayout 引用的“${did}”不是 Dock 节点`, id);
        }
      }
      for (const [areaKey, areaSpec] of Object.entries(node.dockLayout.areas ?? {})) {
        if (!["left", "right", "top", "bottom"].includes(areaKey)) {
          error(`窗口 ${id}：dockLayout.areas 的键应为 left、right、top、bottom 之一，现在是“${areaKey}”`, id);
        }
        if (areaSpec?.splitOrientation && !["horizontal", "vertical"].includes(areaSpec.splitOrientation)) {
          error(`窗口 ${id}：dockLayout.areas.${areaKey}.splitOrientation 应为 horizontal 或 vertical`, id);
        }
      }
    }
    if (node.rotation && (node.type !== "Shape" || childIds.length > 0)) {
      hint(`控件 ${id} 设置了旋转（${node.rotation}°），导出成 Qt 时暂不处理旋转`, id);
    }
    if (node.type === "External") {
      if (childIds.length) error(`外部容器 ${id} 不能包含子节点，里面的控件由业务提供`, id);
      continue;
    }
    if (node.type === "Shape" || node.type === "Line") {
      const decl = declFor(project, node.type)!;
      for (const key of Object.keys(node.props ?? {})) {
        if (!decl.props?.[key]) error(`${node.type === "Shape" ? "形状" : "线条"} ${id}：scene.json 中的属性“${key}”不在声明里`, id);
      }
      if (childIds.length) error(`${id}：形状和线条不能包含子节点`, id);
      for (const end of ["startNode", "endNode"]) {
        const target = node.props?.[end];
        if (typeof target === "string" && target !== "" && !scene.nodes[target]) {
          hint(`线条 ${id}：${end === "startNode" ? "起点" : "终点"}挂着的节点“${target}”不存在，这一头停在原来的位置`, id);
        }
      }
      continue;
    }
    if (BUILTIN_TYPES.includes(node.type)) {
      const decl = declFor(project, node.type);
      for (const key of Object.keys(node.props ?? {})) {
        if (key === "visible" || key === "enabled" || key === "open" || key === "modal") continue;
        if (decl?.props && !decl.props[key]) {
          error(`节点 ${id}：scene.json 中的属性“${key}”不在 ${node.type} 的声明里`, id);
        }
      }
      continue;
    }
    const component = project.components.get(node.type) ?? STANDARD_COMPONENTS.get(node.type);
    if (!component) {
      error(`控件 ${id} 的类型“${node.type}”在 components/ 或标准控件中找不到`, id);
      continue;
    }
    for (const key of Object.keys(node.props ?? {})) {
      if (key === "visible") continue;
      if (!component.decl.props?.[key]) error(`控件 ${id}：scene.json 中的属性“${key}”不在 ${node.type} 的声明里`, id);
    }
  }

  // 逻辑 JS 函数与动作脚本：禁止使用网页元素或浏览器对象
  for (const [fnName, source] of Object.entries(project.functions ?? {})) {
    const forbidden = findForbiddenDomUsage(source);
    if (forbidden) {
      error(`函数 ${fnName}：逻辑 JS 函数不能使用网页元素或浏览器对象“${forbidden}”`);
    }
  }
  for (const [actName, source] of Object.entries(project.actionSources ?? {})) {
    const forbidden = findForbiddenDomUsage(source);
    if (forbidden) {
      error(`动作 ${actName}：动作 JS 脚本不能使用网页元素或浏览器对象“${forbidden}”`);
    }
  }

  // 状态图片与 SVG 写法检查（components/ 与 effects/）
  ensureComponentSvgFiles(project);
  for (const [type, comp] of project.components) {
    if (comp.error) {
      error(`控件 ${type}：${comp.error}`);
    }
    const folder = comp.folder ?? type;
    if (comp.decl.stateImages) {
      if (!comp.decl.stateImages.normal) {
        error(`控件 ${type}：stateImages 必须至少提供 normal 状态对应的 SVG 文件名`);
      }
      if (comp.svgFiles !== undefined) {
        for (const key of STATE_IMAGE_KEYS) {
          const fname = comp.decl.stateImages[key];
          if (fname && !(fname in comp.svgFiles)) {
            error(`控件 ${type}：stateImages.${key} 引用的 SVG 文件“${fname}”在 components/${folder}/ 中找不到`);
          }
        }
      }
    }
    for (const [fileName, svgText] of Object.entries(comp.svgFiles ?? {})) {
      for (const issue of checkSvgCompatibility(svgText, `components/${folder}/${fileName}`)) {
        problems.push(issue);
      }
    }
    if (comp.decl.effects) {
      for (const [slot, effectName] of Object.entries(comp.decl.effects)) {
        if (!effectName) continue;
        if (slot !== "visible" && slot !== "paint") {
          error(`控件 ${type}：effects 只支持 visible（过渡效果）或 paint（绘制效果），不支持“${slot}”`);
          continue;
        }
        const eff = project.effects.get(String(effectName));
        if (!eff) {
          error(`控件 ${type}：声明引用的效果“${effectName}”在 effects/ 中找不到`);
        } else if (slot === "visible" && !isTransitionEffect(eff.decl.kind)) {
          error(`控件 ${type}：效果 ${effectName} 的种类是 ${eff.decl.kind}，不能用在 visible 过渡效果上`);
        } else if (slot === "paint" && !isPaintEffect(eff.decl.kind)) {
          error(`控件 ${type}：效果 ${effectName} 的种类是 ${eff.decl.kind}，不能用在 paint 绘制效果上`);
        }
      }
    }
  }

  // 效果
  for (const [name, effect] of project.effects) {
    for (const [fileName, content] of Object.entries(effect.files)) {
      if (fileName.toLowerCase().endsWith(".svg")) {
        for (const issue of checkSvgCompatibility(content, `effects/${effect.folder}/${fileName}`)) {
          problems.push(issue);
        }
      }
    }
    if (effect.error) {
      error(`效果 ${name}：${effect.error}`);
      continue;
    }
    if (!EFFECT_KINDS.includes(effect.decl.kind)) {
      error(`效果 ${name}：kind 应为 ${EFFECT_KINDS.join("、")} 之一，现在是“${effect.decl.kind}”`);
    }
    if (isPaintEffect(effect.decl.kind)) {
      hint(`效果 ${name}（绘制效果）需要翻译：导出成 Qt 后，要在界面效果层为它提供实现（当前哈希 ${effect.hash}）；没有提供时显示占位框`);
    } else {
      hint(`效果 ${name}（过渡效果）需要翻译：导出成 Qt 后，要在界面效果层为它提供实现（当前哈希 ${effect.hash}）；没有提供时直接切到结束状态`);
    }
  }

  // 状态、条件、控件与菜单项的保留名与重名检查
  const stateRoots = Object.keys(logic.state ?? {});
  for (const id of Object.keys(scene.nodes)) {
    if (RESERVED_IDENTIFIER_NAMES.has(id)) {
      error(`控件 ID“${id}”是系统保留名称，请改名`, id);
    }
  }
  for (const { path, leaf } of allStateLeaves(logic)) {
    if (leaf.owner !== undefined && leaf.owner !== "ui" && leaf.owner !== "business") {
      error(`状态 ${path}：owner 应为 ui（界面自己管）或 business（业务管），现在是“${leaf.owner}”`);
    }
  }
  for (const id of Object.keys(logic.conditions ?? {})) {
    if (RESERVED_IDENTIFIER_NAMES.has(id)) error(`全局条件“${id}”是系统保留名称，请改名`);
    if (stateRoots.includes(id) || scene.nodes[id]) error(`全局条件“${id}”与状态根名或控件 ID 重名`);
  }
  for (const root of stateRoots) {
    if (RESERVED_IDENTIFIER_NAMES.has(root)) error(`状态根名“${root}”是系统保留名称，请改名`);
    if (scene.nodes[root]) error(`状态根名“${root}”与控件 ID 重名`);
  }
  const seenMenuItemIds = new Set<string>();
  const checkMenuItemList = (items: import("./model.ts").MenuItemSpec[] | undefined, ownerNodeId?: string) => {
    walkMenuItems(items, (item) => {
      if (item.kind === "separator" || !item.id) return;
      if (RESERVED_IDENTIFIER_NAMES.has(item.id)) {
        error(`菜单项 ID“${item.id}”是系统保留名称，请改名`, ownerNodeId);
      }
      if (seenMenuItemIds.has(item.id)) {
        error(`菜单项 ID“${item.id}”重复定义`, ownerNodeId);
      }
      seenMenuItemIds.add(item.id);
      if (scene.nodes[item.id] || stateRoots.includes(item.id) || logic.conditions?.[item.id]) {
        error(`菜单项 ID“${item.id}”与控件 ID、状态根名或全局条件重名`, ownerNodeId);
      }
    });
  };
  for (const [nid, n] of Object.entries(scene.nodes)) {
    for (const menu of n.menuBar?.menus ?? []) {
      checkMenuItemList(menu.items, nid);
    }
    checkMenuItemList(n.contextMenu, nid);
    checkMenuItemList(n.popupMenu, nid);
  }
  checkConditions(logic.conditions, "");

  // 全局事件、业务事件与请求事件
  for (const [name, def] of Object.entries(logic.events ?? {})) {
    if (Array.isArray(def)) {
      const extra = name === "clock.tick" ? new Set(["value", "param", "ms", "prevMs", "deltaMs"]) : new Set<string>();
      checkSteps(def, `全局事件 ${name}`, "normal", undefined, extra);
    } else if (def && typeof def === "object" && def.owner === "business") {
      const bDef = businessEvent(logic, name)!;
      const paramNames = new Set<string>(["param"]);
      for (const k of Object.keys(bDef.params ?? {})) {
        paramNames.add(k);
        paramNames.add(`param.${k}`);
      }
      if (bDef.simulate === undefined) hint(`业务事件 ${name} 没有写 simulate，业务没有接上时它什么都不做`);
      else checkSteps(bDef.simulate, `业务事件 ${name} 的模拟步骤`, "simulate", undefined, paramNames);
    } else if (def && typeof def === "object" && def.owner === "ui") {
      const rDef = requestEvent(logic, name)!;
      const paramNames = new Set<string>(["param"]);
      for (const k of Object.keys(rDef.params ?? {})) {
        paramNames.add(k);
        paramNames.add(`param.${k}`);
      }
      if (!Array.isArray(rDef.steps)) error(`请求事件 ${name}：缺少 steps 步骤列表`);
      else checkSteps(rDef.steps, `请求事件 ${name} 的步骤`, "normal", undefined, paramNames);
    } else {
      error(`全局事件 ${name}：应该是步骤列表；如果是交给业务的事件，写 owner: business；如果是业务发给界面的请求事件，写 owner: ui`);
    }
  }

  // 动作 actions
  for (const [name, def] of Object.entries(logic.actions ?? {})) {
    if (!def || typeof def !== "object") {
      error(`动作 ${name}：定义应为对象`);
      continue;
    }
    for (const [inputKey, expr] of Object.entries(def.input ?? {})) {
      checkExpr(expr, `动作 ${name} 的 input.${inputKey}`);
    }
    if (def.onSuccess) {
      checkSteps(def.onSuccess, `动作 ${name} 的 onSuccess`, "normal", undefined, new Set(["result"]));
    }
    if (def.onError) {
      checkSteps(def.onError, `动作 ${name} 的 onError`, "normal", undefined, new Set(["error", "error.message"]));
    }
    if (!project.actionSources?.[name]) {
      hint(`动作 ${name} 没有实现文件（${def.handler ?? `actions/${name}.js`}），预览时默认按模拟成功处理`);
    }
  }

  // 控件与菜单项
  for (const [id, object] of Object.entries(logic.objects ?? {})) {
    if (!object) continue;
    const node = scene.nodes[id];
    const menuItem = !node ? findMenuItem(scene, id) : null;
    if (!node && !menuItem) {
      error(`逻辑中的对象“${id}”在画布中不存在`);
      continue;
    }
    if (menuItem && !node) {
      checkConditions(object.conditions, `${id}.`, id);
      for (const [key, source] of Object.entries(object.props ?? {})) {
        if (!["visible", "enabled", "text", "checked"].includes(key)) {
          error(`菜单项 ${id}：不支持由逻辑控制属性“${key}”`, id);
        }
        checkExpr(source, `菜单项 ${id} 的 props.${key}`, id);
      }
      for (const [event, steps] of Object.entries(object.events ?? {})) {
        if (event !== "click") error(`菜单项 ${id}：只支持 click 事件，不支持“${event}”`, id);
        checkSteps(steps, `菜单项 ${id} 的 ${event} 事件`, "normal", id, new Set(["value", "param"]));
      }
      continue;
    }

    const decl = declFor(project, node.type);
    const isExternal = node.type === "External";
    checkConditions(object.conditions, `${id}.`, id);
    for (const [key, source] of Object.entries(object.props ?? {})) {
      if (key !== "visible" && key !== "enabled" && !decl?.props?.[key]?.logic) {
        error(`控件 ${id}：当前类型 ${node.type} 不支持由逻辑控制属性“${key}”`, id);
      }
      checkExpr(source, `控件 ${id} 的 props.${key}`, id);
    }
    if (object.bind !== undefined) {
      if (!decl?.bind) error(`控件 ${id}：当前类型 ${node.type} 不支持 bind`, id);
      const leaf = findStateLeaf(logic, object.bind);
      if (!leaf) error(`控件 ${id}：bind 的目标“${object.bind}”不是状态路径`, id);
      else if (isBusinessState(leaf)) error(`控件 ${id}：bind 的目标“${object.bind}”归业务管，界面不能直接写入`, id);
    }
    for (const [event, steps] of Object.entries(object.events ?? {})) {
      if (isExternal) {
        error(`外部容器 ${id} 不支持事件：里面的控件由业务提供，界面收不到它上面的鼠标事件`, id);
      } else if (!MOUSE_EVENTS.includes(event) && !decl?.events?.includes(event)) {
        error(`控件 ${id}：当前类型 ${node.type} 不支持事件“${event}”`, id);
      }
      checkSteps(
        steps,
        `控件 ${id} 的 ${event} 事件`,
        "normal",
        id,
        new Set(["value", "param", "from", "to", "index", "id", "checked", "collapsed", "name", "hook", "type"]),
      );
    }
    for (const [prop, effectName] of Object.entries(object.effects ?? {})) {
      if (prop !== "visible" && prop !== "paint") {
        error(`控件 ${id}：effects 目前只支持 visible（显示和隐藏时的过渡）或 paint（自绘绘制效果），不支持“${prop}”`, id);
        continue;
      }
      const effect = project.effects.get(String(effectName));
      if (!effect) {
        error(`控件 ${id}：效果“${effectName}”在 effects/ 中找不到`, id);
      } else if (prop === "visible" && !isTransitionEffect(effect.decl.kind)) {
        error(`控件 ${id}：效果 ${effectName} 的种类是 ${effect.decl.kind}，不能用在 visible 上`, id);
      } else if (prop === "paint" && !isPaintEffect(effect.decl.kind)) {
        error(`控件 ${id}：效果 ${effectName} 的种类是 ${effect.decl.kind}，不能用在 paint 上`, id);
      }
      if (prop === "visible" && object.props?.visible === undefined) {
        hint(`控件 ${id}：给 visible 指定了效果，但 visible 没有由逻辑控制，这个效果不会被播放`, id);
      }
    }
    if (object.doc) {
      const { path, section } = parseDocRef(object.doc);
      const text = project.docs.get(path);
      if (text === undefined) hint(`控件 ${id}：找不到说明文档 ${path}`, id);
      else if (section && getSection(text, section) === null) hint(`控件 ${id}：${path} 中找不到小节 ${section}`, id);
    }
  }
  return problems;
}
