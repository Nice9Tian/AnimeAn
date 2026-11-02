// `npm test` runs `node --test tests/`, which loads only this file, so every
// *.test.ts in this folder has to be listed here; the last test fails when one is missing.
import assert from "node:assert/strict";
import { readdirSync } from "node:fs";
import { test } from "node:test";

const files = [
  // First: it switches the language, which repaints every panel the later tests leave behind.
  "i18n.test.ts",
  "net_and_bridge.test.ts",
  "renderer.test.ts",
  "tess.test.ts",
  "glrenderer.test.ts",
  "field_view.test.ts",
  "input.test.ts",
  "cursor.test.ts",
  "incremental_refresh.test.ts",
  "tool_groups.test.ts",
  "layers_assets.test.ts",
  "timeline_arrange.test.ts",
  "dialogs_and_options.test.ts",
  "pen_probe.test.ts",
  "texture_panel.test.ts",
  "relay.test.ts",
  "board_effects.test.ts",
  "dock_glide.test.ts",
];

for (const file of files) await import(`./${file}`);

test("tests/index.js lists every *.test.ts in tests/", () => {
  const onDisk = readdirSync(new URL(".", import.meta.url)).filter((name) => name.endsWith(".test.ts"));
  assert.deepEqual([...files].sort(), onDisk.sort());
});
