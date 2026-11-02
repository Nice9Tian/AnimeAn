import assert from "node:assert/strict";
import { test } from "node:test";
import { asksForCanvasOnStartup } from "../src/ui/startup.js";

test("a fresh backend gets the New Project dialog", () => {
  assert.equal(asksForCanvasOnStartup({ canvasWidth: 1280, canvasHeight: 720, currentFilePath: "" }), true);
});

test("a backend already holding a project is not asked", () => {
  assert.equal(
    asksForCanvasOnStartup({ canvasWidth: 1280, canvasHeight: 720, currentFilePath: "C:/work/girl.anproj" }),
    false,
  );
});

test("no canvas yet, no dialog", () => {
  assert.equal(asksForCanvasOnStartup({ currentFilePath: "" }), false);
  assert.equal(asksForCanvasOnStartup(undefined), false);
});
