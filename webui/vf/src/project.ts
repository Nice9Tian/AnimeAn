// 读取设计项目：在 loader.ts 读出的内容之上，把控件和效果的实现文件加载成可以运行的模块，
// 并把控件的样式加作用域注入。数据结构和查找函数在 model.ts 里，这里原样转出。
import { mountStateImageControl, STANDARD_COMPONENT_LIST } from "../standard/index.ts";
import { check } from "./check.ts";
import { loadProjectFiles, type ProjectIO } from "./loader.ts";
import {
  isPaintEffect,
  STATE_IMAGE_KEYS,
  type ComponentCreate,
  type EffectPaint,
  type EffectPlay,
  type Project,
} from "./model.ts";

export * from "./model.ts";
export type { ProjectIO };

const moduleCache = new Map<string, { source: string; module?: { default?: unknown }; error?: string }>();
let stageStyleTarget: Document | null = null;
let lastComponentCss = "";

/** 把一段源码当作 ES 模块加载。同样的源码只加载一次。 */
export async function importSource(cacheKey: string, source: string): Promise<{ module?: { default?: unknown }; error?: string }> {
  let cached = moduleCache.get(cacheKey);
  if (!cached || cached.source !== source) {
    cached = { source };
    const useBlob = typeof URL !== "undefined" && typeof URL.createObjectURL === "function" && typeof document !== "undefined";
    const url = useBlob
      ? URL.createObjectURL(new Blob([source], { type: "text/javascript" }))
      : `data:text/javascript;charset=utf-8,${encodeURIComponent(source)}`;
    try {
      cached.module = (await import(/* @vite-ignore */ url)) as { default?: unknown };
    } catch (e) {
      cached.error = `加载失败：${(e as Error).message}`;
    } finally {
      if (useBlob) URL.revokeObjectURL(url);
    }
    moduleCache.set(cacheKey, cached);
  }
  return cached;
}

function splitSelectorList(selectorText: string): string[] {
  const parts: string[] = [];
  let current = "";
  let parenDepth = 0;
  let bracketDepth = 0;
  let quote: string | null = null;
  for (let i = 0; i < selectorText.length; i++) {
    const ch = selectorText[i];
    if (quote) {
      current += ch;
      if (ch === "\\" && i + 1 < selectorText.length) {
        current += selectorText[++i];
      } else if (ch === quote) {
        quote = null;
      }
      continue;
    }
    if (ch === '"' || ch === "'") {
      quote = ch;
      current += ch;
      continue;
    }
    if (ch === "(") parenDepth++;
    else if (ch === ")") parenDepth = Math.max(0, parenDepth - 1);
    else if (ch === "[") bracketDepth++;
    else if (ch === "]") bracketDepth = Math.max(0, bracketDepth - 1);
    else if (ch === "," && parenDepth === 0 && bracketDepth === 0) {
      parts.push(current);
      current = "";
      continue;
    }
    current += ch;
  }
  if (current) parts.push(current);
  return parts;
}

function prefixSingleSelector(prefix: string, rawSelector: string): string {
  const sel = rawSelector.trim();
  if (!sel) return "";
  if (sel === ":scope" || sel === ":root" || sel === "html" || sel === "body") {
    return prefix;
  }
  if (sel.startsWith(":scope")) {
    return `${prefix}${sel.slice(":scope".length)}`;
  }
  if (sel.startsWith(prefix)) {
    return sel;
  }
  return `${prefix} ${sel}`;
}

/** 把控件 CSS 的选择器加上 `[data-vf-type="..."]` 前缀，不依赖 @scope。 */
export function scopeComponentCss(type: string, cssText: string): string {
  const prefix = `[data-vf-type="${type}"]`;
  const stripped = cssText.replace(/\/\*[\s\S]*?\*\//g, "");
  let out = "";
  let i = 0;
  const n = stripped.length;

  while (i < n) {
    while (i < n && /\s/.test(stripped[i])) {
      out += stripped[i++];
    }
    if (i >= n) break;

    const headerStart = i;
    let quote: string | null = null;
    while (i < n && stripped[i] !== "{" && stripped[i] !== ";") {
      const ch = stripped[i];
      if (quote) {
        if (ch === "\\" && i + 1 < n) i++;
        else if (ch === quote) quote = null;
      } else if (ch === '"' || ch === "'") {
        quote = ch;
      }
      i++;
    }
    if (i >= n) {
      out += stripped.slice(headerStart);
      break;
    }
    if (stripped[i] === ";") {
      out += stripped.slice(headerStart, i + 1);
      i++;
      continue;
    }

    const header = stripped.slice(headerStart, i).trim();
    i++; // skip '{'
    const bodyStart = i;
    let depth = 1;
    quote = null;
    while (i < n && depth > 0) {
      const ch = stripped[i];
      if (quote) {
        if (ch === "\\" && i + 1 < n) i++;
        else if (ch === quote) quote = null;
      } else if (ch === '"' || ch === "'") {
        quote = ch;
      } else if (ch === "{") {
        depth++;
      } else if (ch === "}") {
        depth--;
      }
      i++;
    }
    const body = stripped.slice(bodyStart, Math.max(bodyStart, i - 1));

    if (header.startsWith("@")) {
      const lower = header.toLowerCase();
      if (
        lower.startsWith("@media") ||
        lower.startsWith("@supports") ||
        lower.startsWith("@container") ||
        lower.startsWith("@layer")
      ) {
        out += `${header} {\n${scopeComponentCss(type, body)}\n}\n`;
      } else {
        out += `${header} {${body}}\n`;
      }
    } else {
      const prefixed = splitSelectorList(header)
        .map((s) => prefixSingleSelector(prefix, s))
        .filter(Boolean)
        .join(", ");
      if (prefixed) {
        out += `${prefixed} {${body}}\n`;
      }
    }
  }
  return out;
}

export function applyComponentStylesToDoc(doc: Document, css: string): void {
  let style = doc.getElementById("vf-component-styles") as HTMLStyleElement | null;
  if (!style) {
    style = doc.createElement("style");
    style.id = "vf-component-styles";
    doc.head.append(style);
  }
  if (style.textContent !== css) style.textContent = css;
}

/** 指定样式注入目标 Document。 */
export function setStageStyleTarget(doc: Document | null): void {
  stageStyleTarget = doc;
  if (typeof document !== "undefined" && doc && doc !== document) {
    document.getElementById("vf-component-styles")?.remove();
  }
  if (doc) {
    applyComponentStylesToDoc(doc, lastComponentCss);
  }
}

/** 把所有控件（含标准控件与项目控件）的 style.css 加上类型前缀，合并注入目标 Document。 */
export async function injectComponentStyles(project: Project, io?: Pick<ProjectIO, "readText">): Promise<string> {
  let css = "";
  for (const std of STANDARD_COMPONENT_LIST) {
    if (project.components.has(std.decl.type)) continue;
    if (std.styleText) css += `${scopeComponentCss(std.decl.type, std.styleText)}\n`;
  }
  for (const [type, component] of project.components) {
    const inlineStyle = (component as { styleText?: string }).styleText;
    const text =
      inlineStyle !== undefined
        ? inlineStyle
        : component.folder && io
          ? await io.readText(`components/${component.folder}/${component.decl.style ?? "style.css"}`)
          : null;
    if (text) css += `${scopeComponentCss(type, text)}\n`;
  }
  lastComponentCss = css;
  if (stageStyleTarget) {
    applyComponentStylesToDoc(stageStyleTarget, css);
  }
  return css;
}

/** previous 用于在 YAML 写坏时沿用上一份有效的逻辑。 */
export async function loadProject(io: ProjectIO, previous: Project | null = null): Promise<Project> {
  const project = await loadProjectFiles(io, previous);
  const preCheckSet = new Set(check(project).map((p) => `${p.level}\0${p.message}`));
  const loaderProblems = project.problems.filter((p) => !preCheckSet.has(`${p.level}\0${p.message}`));
  let svgSignature = "";

  for (const [, component] of project.components) {
    if (!component.folder) continue;
    const svgFiles: Record<string, string> = { ...(component.svgFiles ?? {}) };
    const entries = await io.listDir(`components/${component.folder}`);
    const svgNames = new Set<string>(
      entries.filter((e) => !e.isDir && e.name.toLowerCase().endsWith(".svg")).map((e) => e.name),
    );
    if (component.decl.stateImages) {
      for (const key of STATE_IMAGE_KEYS) {
        const fname = component.decl.stateImages[key];
        if (typeof fname === "string" && fname) svgNames.add(fname);
      }
    }
    for (const name of [...svgNames].sort()) {
      const text = await io.readText(`components/${component.folder}/${name}`);
      if (text !== null) {
        svgFiles[name] = text;
        svgSignature += `\ncomponents/${component.folder}/${name}\n${text}`;
      }
    }
    component.svgFiles = svgFiles;
    const styleText = await io.readText(`components/${component.folder}/${component.decl.style ?? "style.css"}`);
    if (styleText !== null) {
      (component as { styleText?: string }).styleText = styleText;
    }
  }

  for (const [, component] of project.components) {
    if (component.source === undefined) continue;
    const entry = component.decl.entry ?? "index.js";
    const loaded = await importSource(`components/${component.folder}/${entry}`, component.source);
    if (loaded.error) component.error = `${entry} ${loaded.error}`;
    else if (typeof loaded.module?.default !== "function") component.error = `${entry} 没有默认导出一个创建函数`;
    else {
      const rawCreate = loaded.module.default as ComponentCreate;
      const stateImages = component.decl.stateImages;
      const svgFiles = component.svgFiles ?? {};
      if (stateImages) {
        component.create = (el, ctx) => {
          const userInst = rawCreate(el, { ...ctx, stateImages, svgFiles });
          const stateInst = mountStateImageControl(el, {
            props: ctx.props,
            emit: ctx.emit,
            mode: ctx.mode,
            stateImages,
            svgFiles,
          });
          return {
            update(next) {
              userInst?.update?.(next);
              stateInst.update(next);
            },
            destroy() {
              userInst?.destroy?.();
              stateInst.destroy();
            },
          };
        };
      } else {
        component.create = rawCreate;
      }
    }
  }

  for (const [, effect] of project.effects) {
    const entry = effect.decl.entry ?? "index.js";
    const source = effect.files[entry];
    if (effect.error || source === undefined) continue;
    const loaded = await importSource(`effects/${effect.folder}/${entry}`, source);
    if (loaded.error) effect.error = `${entry} ${loaded.error}`;
    else if (typeof loaded.module?.default !== "function") {
      effect.error = `${entry} 没有默认导出一个${isPaintEffect(effect.decl.kind) ? "绘制" : "播放"}函数`;
    } else if (isPaintEffect(effect.decl.kind)) {
      effect.paint = loaded.module.default as EffectPaint;
    } else {
      effect.play = loaded.module.default as EffectPlay;
    }
  }

  project.problems = [...loaderProblems, ...check(project)];

  const css = await injectComponentStyles(project, io);
  project.signature += svgSignature + css;
  return project;
}
