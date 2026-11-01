import assert from "node:assert/strict";
import { test } from "node:test";
import { cursorCss } from "../src/board/cursor.ts";

test("位图光标按屏幕比例声明，热点保持 CSS 像素", () => {
  const value = { key: "cur_a", css: "default", hx: 22, hy: 22, scale: 1.25 };
  assert.equal(cursorCss(value, "data:x"), "image-set(url(data:x) 1.25x) 22 22, default");
});

test("没有位图时用形状名", () => {
  assert.equal(cursorCss({ css: "crosshair" }, null), "crosshair");
  assert.equal(cursorCss(undefined, undefined), "default");
});

test("位图还没到时不写坏的 image-set", () => {
  assert.equal(cursorCss({ key: "cur_a", css: "default", hx: 5, hy: 5, scale: 2 }, undefined), "default");
});
