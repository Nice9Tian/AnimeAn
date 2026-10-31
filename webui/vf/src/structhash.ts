// 控件的结构哈希（设计文档第 3.2 节）：
// 对控件文件夹内容计算 MD5，覆盖 component.json 中除 type、label、description 以外的所有字段，
// 以及 index.js 和 style.css（和文件夹内其他文件）的全部内容。
// 计算前统一换行为 LF，component.json 按键名排序并去掉空白；名字和说明不参与计算。
import { md5 } from "./md5.ts";
import type { ComponentDecl } from "./model.ts";

export interface ComponentHashInput {
  /** component.json 的解析结果或原文 */
  decl: ComponentDecl | Record<string, unknown> | string;
  /** 实现文件（默认 index.js）的原文 */
  source: string;
  /** 样式文件（默认 style.css）的原文，没有时传空字符串或省略 */
  style?: string;
  /** 控件文件夹里的其他文件（文件名 → 原文），可选 */
  extraFiles?: Record<string, string>;
}

/** 把换行符统一为 LF。 */
export function normalizeLf(text: string): string {
  return text.replace(/\r\n/g, "\n").replace(/\r/g, "\n");
}

/** 按键名递归排序、去掉空白后序列化为 JSON，消除缩进与键顺序差异。 */
export function canonicalJson(value: unknown): string {
  if (value === null || typeof value !== "object") return JSON.stringify(value);
  if (Array.isArray(value)) return `[${value.map(canonicalJson).join(",")}]`;
  const record = value as Record<string, unknown>;
  const keys = Object.keys(record)
    .filter((key) => record[key] !== undefined)
    .sort();
  return `{${keys.map((key) => `${JSON.stringify(key)}:${canonicalJson(record[key])}`).join(",")}}`;
}

function parseDeclObject(decl: ComponentDecl | Record<string, unknown> | string): Record<string, unknown> {
  if (typeof decl === "string") {
    try {
      const parsed = JSON.parse(normalizeLf(decl)) as unknown;
      if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) {
        return parsed as Record<string, unknown>;
      }
    } catch {
      return { __rawDecl: normalizeLf(decl) };
    }
    return {};
  }
  return { ...(decl as Record<string, unknown>) };
}

function normalizeDeclEffectsForHash(effects: unknown): unknown {
  if (!effects || typeof effects !== "object" || Array.isArray(effects)) return effects;
  const out: Record<string, unknown> = {};
  for (const [k, v] of Object.entries(effects as Record<string, unknown>)) {
    out[k] = typeof v === "string" ? v.replace(/_\d+$/, "") : v;
  }
  return out;
}

/**
 * 计算控件的结构哈希（32 位十六进制 MD5）。
 * type、label、description 不参与计算；换行符与 JSON 缩进/键顺序不影响结果。
 */
export function structHash(input: ComponentHashInput): string {
  const declObj = parseDeclObject(input.decl);
  const { type: _type, label: _label, description: _description, ...rest } = declObj;
  if (rest.effects !== undefined) {
    rest.effects = normalizeDeclEffectsForHash(rest.effects);
  }
  const source = normalizeLf(input.source ?? "");
  const style = normalizeLf(input.style ?? "");
  const extraEntries = Object.keys(input.extraFiles ?? {})
    .sort()
    .map((name) => [name, normalizeLf(input.extraFiles![name])]);
  const payload = extraEntries.length > 0
    ? [canonicalJson(rest), source, style, extraEntries]
    : [canonicalJson(rest), source, style];
  return md5(JSON.stringify(payload));
}

/**
 * 直接从控件文件夹的文件表（文件名 → 原文）计算结构哈希。
 */
export function structHashFromFiles(files: Record<string, string>): string {
  const declText = files["component.json"] ?? "{}";
  const declObj = parseDeclObject(declText);
  const entryName = typeof declObj.entry === "string" && declObj.entry ? declObj.entry : "index.js";
  const styleName = typeof declObj.style === "string" && declObj.style ? declObj.style : "style.css";
  const extraFiles: Record<string, string> = {};
  for (const [name, content] of Object.entries(files)) {
    if (name === "component.json" || name === entryName || name === styleName) continue;
    extraFiles[name] = content;
  }
  return structHash({
    decl: declObj,
    source: files[entryName] ?? "",
    style: files[styleName] ?? "",
    extraFiles,
  });
}
