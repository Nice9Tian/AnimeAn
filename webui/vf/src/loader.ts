// 把设计项目的文件读成一个 Project。不碰网页：文件怎么读由调用方提供，
// 工具界面里走 fs.ts，命令行里走 Node 的文件接口。
import { parse as parseYaml } from "yaml";
import { check } from "./check.ts";
import { md5 } from "./md5.ts";
import {
  LOGIC_PATH,
  parseDocRef,
  SCENE_PATH,
  type ComponentDecl,
  type EffectDecl,
  type LoadedComponent,
  type LoadedEffect,
  type Logic,
  type Problem,
  type Project,
  type Scene,
} from "./model.ts";
import type { ShapePreset } from "./shapes.ts";
import { canonicalJson, normalizeLf } from "./structhash.ts";

export const PRESETS_PATH = "ui/presets.json";

export interface ProjectIO {
  /** 设计项目文件夹的绝对路径 */
  root(): Promise<string>;
  /** 路径相对于项目文件夹；文件不存在时返回 null */
  readText(path: string): Promise<string | null>;
  listDir(path: string): Promise<Array<{ name: string; isDir: boolean }>>;
}

/**
 * 效果的哈希：对名字、种类和文件夹里全部文件的内容计算 MD5，取前 8 位。
 * label 和 description 是给人看的，不参与计算；换行符统一成 LF、嵌套对象按键名递归排序后再算。
 */
export function effectHash(decl: EffectDecl, files: Record<string, string>): string {
  const { label: _label, description: _description, ...rest } = decl;
  const body = Object.keys(files)
    .filter((name) => name !== "effect.json")
    .sort()
    .map((name) => [name, normalizeLf(files[name])]);
  const canonical = JSON.stringify([canonicalJson(rest), body]);
  return md5(canonical).slice(0, 8);
}

async function loadComponent(io: ProjectIO, folder: string): Promise<LoadedComponent | null> {
  const declText = await io.readText(`components/${folder}/component.json`);
  if (declText === null) return null;
  let decl: ComponentDecl;
  try {
    decl = JSON.parse(declText) as ComponentDecl;
  } catch (e) {
    return { decl: { type: folder }, folder, error: `component.json 不是有效的 JSON：${(e as Error).message}` };
  }
  if (!decl.type) decl.type = folder;
  const svgFiles: Record<string, string> = {};
  for (const entry of await io.listDir(`components/${folder}`)) {
    if (!entry.isDir && entry.name.toLowerCase().endsWith(".svg")) {
      const svgText = await io.readText(`components/${folder}/${entry.name}`);
      if (svgText !== null) svgFiles[entry.name] = svgText;
    }
  }
  const entry = decl.entry ?? "index.js";
  const source = await io.readText(`components/${folder}/${entry}`);
  if (source === null) return { decl, folder, svgFiles, error: `找不到实现文件 ${entry}` };
  return { decl, folder, source, svgFiles };
}

async function loadEffect(io: ProjectIO, folder: string): Promise<LoadedEffect | null> {
  const declText = await io.readText(`effects/${folder}/effect.json`);
  if (declText === null) return null;
  const files: Record<string, string> = {};
  for (const entry of await io.listDir(`effects/${folder}`)) {
    if (entry.isDir) continue;
    const text = await io.readText(`effects/${folder}/${entry.name}`);
    if (text !== null) files[entry.name] = text;
  }
  let decl: EffectDecl;
  try {
    decl = JSON.parse(declText) as EffectDecl;
  } catch (e) {
    return { decl: { name: folder, kind: "" }, folder, files, hash: "", error: `effect.json 不是有效的 JSON：${(e as Error).message}` };
  }
  if (!decl.name) decl.name = folder;
  const effect: LoadedEffect = { decl, folder, files, hash: effectHash(decl, files) };
  if (files[decl.entry ?? "index.js"] === undefined) effect.error = `找不到实现文件 ${decl.entry ?? "index.js"}`;
  return effect;
}

/** previous 用于在 scene.json 或 YAML 写坏时沿用上一份有效的内容。 */
export async function loadProjectFiles(io: ProjectIO, previous: Project | null): Promise<Project> {
  const problems: Problem[] = [];
  const root = await io.root();

  let name = "未命名项目";
  let pageDoc = "docs/page.md";
  let qtExportDir: string | null = null;
  const extraWindowDocs: string[] = [];
  const configText = (await io.readText("project.json")) ?? "{}";
  try {
    const config = JSON.parse(configText) as {
      name?: string;
      windows?: Array<{ id?: string; doc?: string }>;
      export?: { qt?: string };
    };
    name = config.name ?? name;
    pageDoc = config.windows?.[0]?.doc ?? pageDoc;
    for (const win of config.windows ?? []) {
      if (win?.doc) extraWindowDocs.push(win.doc);
    }
    qtExportDir = config.export?.qt ?? null;
  } catch (e) {
    problems.push({ level: "error", message: `project.json 不是有效的 JSON：${(e as Error).message}` });
  }

  let scene: Scene = previous?.scene ?? { version: 1, root: "window", nodes: { window: { type: "Window", width: 480, height: 320, children: [] } } };
  const sceneText = await io.readText(SCENE_PATH);
  if (sceneText === null) {
    problems.push({ level: "error", message: `找不到 ${SCENE_PATH}` });
  } else {
    try {
      scene = JSON.parse(sceneText) as Scene;
    } catch (e) {
      problems.push({ level: "error", message: `${SCENE_PATH} 不是有效的 JSON，画布沿用上一份有效内容：${(e as Error).message}` });
    }
  }

  const logicText = (await io.readText(LOGIC_PATH)) ?? "";
  let logic: Logic = previous?.logic ?? {};
  try {
    logic = (parseYaml(logicText) as Logic | null) ?? {};
  } catch (e) {
    problems.push({ level: "error", message: `${LOGIC_PATH} 有语法错误，预览沿用上一份有效逻辑：${(e as Error).message}` });
  }

  const components = new Map<string, LoadedComponent>();
  const fingerprints: string[] = [];
  for (const entry of await io.listDir("components")) {
    if (!entry.isDir) continue;
    const component = await loadComponent(io, entry.name);
    if (!component) continue;
    components.set(component.decl.type, component);
    fingerprints.push(JSON.stringify(component.decl), component.source ?? "");
  }

  const effects = new Map<string, LoadedEffect>();
  for (const entry of await io.listDir("effects")) {
    if (!entry.isDir) continue;
    const effect = await loadEffect(io, entry.name);
    if (!effect) continue;
    effects.set(effect.decl.name, effect);
    fingerprints.push(JSON.stringify(effect.files));
  }

  // 纯计算 JS 函数（functions/*.js）
  const functions: Record<string, string> = {};
  for (const entry of await io.listDir("functions")) {
    if (entry.isDir || !entry.name.endsWith(".js")) continue;
    const fnName = entry.name.slice(0, -3);
    const text = await io.readText(`functions/${entry.name}`);
    if (text !== null) {
      functions[fnName] = text;
      fingerprints.push(`fn:${fnName}:${text}`);
    }
  }
  for (const [fnName, def] of Object.entries(logic.functions ?? {})) {
    if (functions[fnName] !== undefined) continue;
    const handlerPath = def?.handler ?? `functions/${fnName}.js`;
    const text = await io.readText(handlerPath);
    if (text !== null) {
      functions[fnName] = text;
      fingerprints.push(`fn:${fnName}:${text}`);
    }
  }

  // 动作实现文件（actions/*.js）
  const actionSources: Record<string, string> = {};
  for (const entry of await io.listDir("actions")) {
    if (entry.isDir || !entry.name.endsWith(".js")) continue;
    const actName = entry.name.slice(0, -3);
    const text = await io.readText(`actions/${entry.name}`);
    if (text !== null) {
      actionSources[actName] = text;
      fingerprints.push(`act:${actName}:${text}`);
    }
  }
  for (const [actName, def] of Object.entries(logic.actions ?? {})) {
    if (actionSources[actName] !== undefined) continue;
    const handlerPath = def?.handler ?? `actions/${actName}.js`;
    const text = await io.readText(handlerPath);
    if (text !== null) {
      actionSources[actName] = text;
      fingerprints.push(`act:${actName}:${text}`);
    }
  }

  // 用户存下来的图形预设
  let presets: ShapePreset[] = [];
  const presetsText = await io.readText(PRESETS_PATH);
  if (presetsText !== null) {
    try {
      const parsed = JSON.parse(presetsText) as { presets?: ShapePreset[] };
      presets = (parsed.presets ?? []).filter((preset) => preset && (preset.type === "Shape" || preset.type === "Line"));
    } catch (e) {
      problems.push({ level: "error", message: `${PRESETS_PATH} 不是有效的 JSON：${(e as Error).message}` });
    }
  }

  const docs = new Map<string, string>();
  const docPaths = new Set<string>([pageDoc, ...extraWindowDocs]);
  for (const object of Object.values(logic.objects ?? {})) {
    if (object?.doc) docPaths.add(parseDocRef(object.doc).path);
  }
  for (const path of docPaths) {
    const text = await io.readText(path);
    if (text !== null) docs.set(path, text);
  }

  const signature = JSON.stringify([sceneText, logicText, [...docs], fingerprints, configText, presetsText]);
  const project: Project = {
    root,
    name,
    pageDoc,
    qtExportDir,
    presets,
    scene,
    sceneText: sceneText ?? "",
    logic,
    logicText,
    docs,
    components,
    effects,
    functions,
    actionSources,
    problems,
    signature,
  };
  problems.push(...check(project));
  return project;
}
