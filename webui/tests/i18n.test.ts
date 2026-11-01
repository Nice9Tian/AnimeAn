import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { beforeEach, test } from "node:test";

const { LANGUAGES, language, missing, onLanguage, setLanguage, t } = await import("../src/i18n/index.js");

const ROOT = join(import.meta.dirname, "..", "..");
const read = (path: string) => readFileSync(join(ROOT, path), "utf8");
const catalog = (name: string): Record<string, string> => JSON.parse(read(`webui/src/i18n/${name}.json`));
const CATALOGS = { "zh-CN": catalog("zh-CN"), ja: catalog("ja") };
const slots = (text: string) => [...text.matchAll(/\{(\d)\}/g)].map((m) => m[1]).sort();

const scan = (text: string, pattern: RegExp) => [...text.matchAll(pattern)].map((m) => m[1]);
const fromQt = (text: string) => text.replace(/%(\d)/g, (_, n) => `{${Number(n) - 1}}`);

// Names that the scan below picks up but nobody reads: glyph ids and a parameter.
const NOT_TEXT = new Set(["CollapseDown", "ToHorizontal", "title"]);

// What can be read off the sources without running them. The menus and options
// Python builds at run time are checked against the real backend by
// backend/tests/i18n_coverage.mjs.
function sourceStrings() {
  const out = new Set<string>();
  for (const path of ["backend/business/animeanbusiness.cpp", "backend/session/backendsession.cpp", "openglwidget.cpp"]) {
    const text = read(path);
    for (const pattern of [
      /(?:commitHistory|setStatusMessage|resetHistory)\(\s*QStringLiteral\("([^"]+)"\)/g,
      /\? QStringLiteral\("([A-Z][^"]+)"\)\s*\n?\s*: QStringLiteral\("[^"]+"\)/g,
      /: QStringLiteral\("([A-Z][a-z]+ [^"]+)"\)\.arg/g,
    ]) {
      for (const found of scan(text, pattern)) out.add(fromQt(found));
    }
    for (const found of scan(text, /makeMenuItem\(QStringLiteral\("[^"]+"\),\s*QStringLiteral\("([^"]+)"\)/g)) out.add(fromQt(found));
  }
  for (const name of readdirSync(join(ROOT, "pyfile"))) {
    if (!name.endsWith(".py") || name === "three_vendor.py" || name === "ui_language.py") continue;
    const text = read(`pyfile/${name}`);
    for (const pattern of [/history_commit\(\s*"([^"]+)"/g, /"title":\s*"([^"]+)"/g, /_slider\("\w+",\s*"([^"]+)"/g]) {
      for (const found of scan(text, pattern)) out.add(found);
    }
  }
  for (const name of readdirSync(join(ROOT, "childrenpanel/tool_controls"))) {
    for (const found of scan(read(`childrenpanel/tool_controls/${name}`), /"title":\s*"([^"]+)"/g)) out.add(found);
  }
  for (const found of scan(read("webui/design/ui/scene.json"), /"(?:title|text|label)":\s*"([^"]+)"/g)) out.add(found);
  for (const name of readdirSync(join(ROOT, "webui/src/ui"))) {
    if (!name.endsWith(".js")) continue;
    const text = read(`webui/src/ui/${name}`);
    for (const pattern of [
      /\b(?:labelled|label)\([^,()]+(?:\([^()]*\))?,\s*"([^"]+)"/g,
      /\bt\("([^"]+)"\)/g,
      /\btrackedLabel\("[^"]+",\s*"([^"]+)"\)/g,
      /\bpushButton\("([^"]+)"/g,
      /\bsingleTab\(root,\s*"([^"]+)"\)/g,
      /\btabBar\(\[([^\]]+)\]/g,
      /\blabelDiv\("[^"]+",\s*"([^"]+)"/g,
      /\btitle: "(1s with \d)"/g,
    ]) {
      for (const found of scan(text, pattern)) {
        for (const part of found.split(/",\s*"/)) out.add(part.replace(/^"|"$/g, ""));
      }
    }
    for (const found of scan(text, /return (?:onionClass\()?"([A-Z][^"]+)"/g)) out.add(found);
    for (const found of scan(text, /state\.\w+ \? "([A-Z][^"]+)"/g)) out.add(found);
    for (const found of scan(text, /: "([A-Z][a-z]+ [^"]+)";/g)) out.add(found);
  }
  return [...out].filter((text) => /\p{L}/u.test(text) && !NOT_TEXT.has(text));
}

beforeEach(() => {
  setLanguage("en");
  missing.clear();
});

test("英文是原文：没有译文时显示原文，也不算漏译", () => {
  assert.equal(language(), "en");
  assert.equal(t("Add Layer"), "Add Layer");
  assert.equal(missing.size, 0);
});

test("逐字词条和模板词条都翻得出来", () => {
  setLanguage("zh-CN");
  assert.equal(t("Add Layer"), CATALOGS["zh-CN"]["Add Layer"]);
  assert.equal(t("Canvas: 1920 x 1080 px"), "画布：1920 x 1080 px");
  assert.equal(t("Frame {0}", "012"), "第 012 帧");
});

test("模板抓到的部分再翻一层", () => {
  setLanguage("zh-CN");
  assert.equal(t("Undo in main: Add Layer"), "在主画板上撤销：添加图层");
  assert.equal(t("Undo in main: Delete Group (3 layers)"), "在主画板上撤销：删除组（3 个图层）");
  setLanguage("ja");
  assert.equal(t("Undo in child: Add Frame"), "テクスチャボードで取り消し：フレームを追加");
});

test("文件名这类抓到的数据原样留下，长得像模板也一样", () => {
  setLanguage("ja");
  assert.equal(t("Opened texture view: scene_01.textureview"), "テクスチャビューを開きました：scene_01.textureview");
  assert.equal(t("AnimeAn - cut 12.anproj"), "AnimeAn - cut 12.anproj");
  assert.equal(t("AnimeAn - Frame 12.anproj"), "AnimeAn - Frame 12.anproj");
  assert.equal(t("Saved project (main + texture): Remove bg.anproj"), "プロジェクトを保存しました（メイン + テクスチャ）：Remove bg.anproj");
  assert.equal(t("AnimeAn - Untitled"), "AnimeAn - 無題");
});

test("撤销一句里的历史标签可以是模板", () => {
  setLanguage("zh-CN");
  assert.equal(t("Redo in child: Remove H center line"), "在纹理板上重做：删除H 轴线");
});

test("更具体的模板优先", () => {
  setLanguage("zh-CN");
  assert.equal(t("AnimeAn - a.anproj [texture: b.textureview]"), "AnimeAn - a.anproj [纹理：b.textureview]");
});

test("没有译文的文字记下来，纯数字不记", () => {
  setLanguage("ja");
  assert.equal(t("Nobody wrote this"), "Nobody wrote this");
  assert.equal(t("1280"), "1280");
  assert.deepEqual([...missing], ["Nobody wrote this"]);
});

test("换语言写进本机并通知，选同一种或不认识的语言什么也不做", () => {
  const saved = new Map<string, string>();
  const original = globalThis.localStorage;
  Object.assign(globalThis, { localStorage: { setItem: (key: string, value: string) => void saved.set(key, value) } });
  const heard: string[] = [];
  const stop = onLanguage((next: string) => heard.push(next));
  setLanguage("ja");
  setLanguage("ja");
  setLanguage("tlh");
  stop();
  setLanguage("zh-CN");
  Object.assign(globalThis, { localStorage: original });
  assert.deepEqual(heard, ["ja"]);
  assert.equal(saved.get("animean.language"), "zh-CN");
});

test("三种语言都在，菜单里各用自己的文字", () => {
  assert.deepEqual(LANGUAGES.map((l: { id: string; name: string }) => l.name), ["English", "简体中文", "日本語"]);
});

test("中文和日文的词条一一对应，占位符也对得上", () => {
  assert.deepEqual(Object.keys(CATALOGS.ja).sort(), Object.keys(CATALOGS["zh-CN"]).sort());
  for (const [name, entries] of Object.entries(CATALOGS)) {
    for (const [source, text] of Object.entries(entries)) {
      assert.ok(text.trim(), `${name}: ${source} 的译文是空的`);
      assert.deepEqual(slots(text), slots(source), `${name}: ${source} 的占位符不一致`);
    }
  }
});

test("源码里看得见的界面文字都有译文", () => {
  const lost = sourceStrings().filter((text) => !(text in CATALOGS.ja));
  assert.deepEqual(lost, []);
});
