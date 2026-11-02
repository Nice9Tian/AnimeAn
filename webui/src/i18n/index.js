// The language of the interface. The English text is the key: menus, options,
// history labels and status lines reach the page from C++ and Python in English
// and are translated only where they are shown, so neither side has to know
// about languages and what the page sends back is always the original text.
//
// A key holding {0}, {1}... is a template: "Undo in {0}: {1}" also matches a
// finished line from the backend. What it captured is usually data such as a
// file name, so only an exact key replaces it; see LABEL_SLOTS for the exception.
import en from "./en.json" with { type: "json" };
import ja from "./ja.json" with { type: "json" };
import zhCN from "./zh-CN.json" with { type: "json" };

// Each language is listed in its own writing, whatever the interface is showing.
export const LANGUAGES = [
  { id: "en", name: "English" },
  { id: "zh-CN", name: "简体中文" },
  { id: "ja", name: "日本語" },
];

const CATALOGS = { en, "zh-CN": zhCN, ja };
const LANGUAGE_KEY = "animean.language";
const SOURCE_LANGUAGE = "en";
// Undo and redo lines quote a history label, which can be a template itself.
const LABEL_SLOTS = { "Undo in {0}: {1}": 1, "Redo in {0}: {1}": 1 };

function templatesOf(catalog) {
  const templates = [];
  for (const [source, text] of Object.entries(catalog)) {
    const parts = source.split(/\{(\d)\}/);
    if (parts.length === 1) continue;
    const literal = parts.filter((_, i) => i % 2 === 0);
    const pattern = literal.map((part) => part.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")).join("([\\s\\S]+?)");
    templates.push({
      match: new RegExp(`^${pattern}$`),
      slots: parts.filter((_, i) => i % 2 === 1).map(Number),
      labelSlot: LABEL_SLOTS[source],
      weight: literal.join("").length,
      text,
    });
  }
  // The most specific template wins: "AnimeAn - {0} [texture: {1}]" before "AnimeAn - {0}".
  return templates.sort((a, b) => b.weight - a.weight);
}

const compiled = new Map();
function compile(language) {
  if (!compiled.has(language)) compiled.set(language, { exact: CATALOGS[language], templates: templatesOf(CATALOGS[language]) });
  return compiled.get(language);
}

function savedLanguage() {
  try {
    const saved = localStorage.getItem(LANGUAGE_KEY);
    return LANGUAGES.some((l) => l.id === saved) ? saved : SOURCE_LANGUAGE;
  } catch {
    return SOURCE_LANGUAGE;
  }
}

let current = savedLanguage();
const listeners = new Set();
const fitters = new WeakMap();
// Text that reached t() with no translation, for the coverage checks.
export const missing = new Set();

export const language = () => current;
export const isSourceLanguage = () => current === SOURCE_LANGUAGE;

const fill = (text, values) => text.replace(/\{(\d)\}/g, (whole, slot) => values[Number(slot)] ?? whole);

const exactly = (text, catalog) => (Object.hasOwn(catalog.exact, text) ? catalog.exact[text] : null);

function translate(text, catalog) {
  const exact = exactly(text, catalog);
  if (exact !== null) return exact;
  for (const template of catalog.templates) {
    const found = template.match.exec(text);
    if (!found) continue;
    const values = [];
    template.slots.forEach((slot, i) => {
      const captured = found[i + 1];
      values[slot] = (slot === template.labelSlot ? translate(captured, catalog) : exactly(captured, catalog)) ?? captured;
    });
    return fill(template.text, values);
  }
  return null;
}

// t("Add Layer"), t("Undo in main: Add Layer"), or t("Frame {0}", name).
export function t(source, ...values) {
  const text = String(source ?? "");
  if (!text) return "";
  const translated = translate(text, compile(current));
  if (translated === null && !isSourceLanguage() && /\p{L}/u.test(text)) missing.add(text);
  return fill(translated ?? text, values);
}

function paint(el, source = el.dataset.i18n) {
  el.textContent = t(source);
  fitters.get(el)?.(el, source);
}

// Text that stays on screen: it follows the language without its panel being
// rebuilt. fit(el, source) runs after every change, for text sized to the Qt
// layout. The source is kept on the element, where a change of language finds it.
export function label(el, source, fit) {
  if (el.dataset) el.dataset.i18n = source;
  if (fit) fitters.set(el, fit);
  paint(el, source);
  return el;
}

export function tip(el, source) {
  if (el.dataset) el.dataset.i18nTip = source;
  el.title = t(source);
  return el;
}

// Repaints the text under root. The document is repainted on every change; a
// body parked off the page while nothing shows it (the texture panel, the
// sub-control frame) is out of its reach, so its owner calls this.
export function relabel(root) {
  if (typeof root?.querySelectorAll !== "function") return;
  for (const el of root.querySelectorAll("[data-i18n]")) paint(el);
  for (const el of root.querySelectorAll("[data-i18n-tip]")) el.title = t(el.dataset.i18nTip);
}

// Han characters are shared between Chinese and Japanese and drawn differently,
// so the browser needs the language to pick the right font.
function markDocument() {
  if (typeof document === "undefined") return;
  if (document.documentElement) document.documentElement.lang = current;
  relabel(document);
}

export function onLanguage(fn) {
  listeners.add(fn);
  return () => listeners.delete(fn);
}

// A per-machine choice like the board renderer, so it lives in the page's own
// storage rather than in the document or the backend.
export function setLanguage(next) {
  if (next === current || !LANGUAGES.some((l) => l.id === next)) return;
  current = next;
  try {
    localStorage.setItem(LANGUAGE_KEY, next);
  } catch (error) {
    console.error(error);
  }
  markDocument();
  // One panel failing to repaint must not leave the others in the old language.
  for (const fn of listeners) {
    try {
      fn(next);
    } catch (error) {
      console.error(error);
    }
  }
}

// A panel in a window of its own is a page of its own: it hears the choice made
// in the main window through the storage the two share.
if (typeof window !== "undefined" && typeof window.addEventListener === "function") {
  window.addEventListener("storage", (e) => {
    if (e.key === LANGUAGE_KEY && e.newValue) setLanguage(e.newValue);
  });
}

markDocument();
