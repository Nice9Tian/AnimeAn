import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { GlBoardRenderer } from "../src/board/glrenderer.ts";
import { BoardRenderer, findScopeStartContaining, type Canvas2DContextLike, type CanvasLike } from "../src/board/renderer.ts";
import { createMockGlCanvas } from "./glrenderer.test.ts";

function createMinimal2DCanvas(): CanvasLike {
  const ctx: Canvas2DContextLike = {
    fillStyle: "#000",
    strokeStyle: "#000",
    lineWidth: 1,
    lineCap: "butt",
    lineJoin: "miter",
    miterLimit: 2,
    lineDashOffset: 0,
    globalAlpha: 1,
    globalCompositeOperation: "source-over",
    imageSmoothingEnabled: false,
    save() {},
    restore() {},
    setTransform() {},
    clearRect() {},
    fillRect() {},
    strokeRect() {},
    beginPath() {},
    moveTo() {},
    lineTo() {},
    bezierCurveTo() {},
    rect() {},
    closePath() {},
    fill() {},
    stroke() {},
    clip() {},
    setLineDash() {},
    createLinearGradient: () => ({ addColorStop() {} }) as unknown as CanvasGradient,
    createRadialGradient: () => ({ addColorStop() {} }) as unknown as CanvasGradient,
    drawImage() {},
    putImageData() {},
  };
  return {
    width: 100,
    height: 100,
    style: {},
    getContext: (id: "2d") => (id === "2d" ? ctx : null),
  };
}

describe("Field Texture & F..f Scope Rendering", () => {
  it("reuses SHAPE_FS coloring logic in FIELD_FS without duplicating paint shader body", () => {
    const mock = createMockGlCanvas();
    new GlBoardRenderer(mock.canvas, { getDpr: () => 1 });
    const fragSources = mock.calls
      .filter((c) => c.fn === "shaderSource")
      .map((c) => String(c.args[1]))
      .filter((src) => src.includes("out vec4 outColor;"));

    const shapeFs = fragSources.find((s) => s.includes("u_gradTex") && !s.includes("u_fieldTex"));
    const fieldFs = fragSources.find((s) => s.includes("u_fieldTex"));
    assert.ok(shapeFs, "SHAPE_FS should be compiled");
    assert.ok(fieldFs, "FIELD_FS should be compiled");

    // Verify FIELD_FS contains the exact paint evaluation block from SHAPE_FS and applies u_tint without u_coverPass
    const paintBodyStart = shapeFs!.indexOf("if (u_paintKind == 0)");
    assert.ok(paintBodyStart > 0);
    const sharedTail = shapeFs!.slice(paintBodyStart);
    assert.ok(fieldFs!.includes(sharedTail), "FIELD_FS must share the exact paint body with SHAPE_FS");
    assert.ok(fieldFs!.includes("uniform vec4 u_tint;"), "FIELD_FS must declare u_tint");
    assert.ok(!fieldFs!.includes("u_coverPass"), "FIELD_FS must not contain u_coverPass");
  });

  it("executes F..f passes in reverse order, supports 7-element tint and 3-element legacy passes, restores entry state, and multiplies opacity", () => {
    const mock = createMockGlCanvas();
    const renderer = new GlBoardRenderer(mock.canvas, { getDpr: () => 1 });
    const field = new Float32Array(2 * 2 * 4);
    renderer.putField({ t: "field", key: "f_rev", w: 2, h: 2, version: 5, bytes: 64 }, field);

    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: 200,
      h: 200,
      keep: 0,
      ops: [
        ["T", 1, 0, 0, 1, 0, 0],
        ["O", 0.5],
        ["P", 0, 200, 0, 255, 2, 0, 0, 2, 0, null],
        [
          "F",
          "f_rev",
          5,
          [10, 20, 100, 80],
          4,
          [
            [1, 1, 1.0],
            [0, 0, 0.25, 255, 128, 0, 102],
          ],
        ],
        ["S", [0, 20, 30, 1, 60, 30], 0],
        // Mutate state inside scope: next pass and ops after 'f' must see restored entry state
        ["O", 0.8],
        ["P", 255, 0, 0, 255, 4, 0, 0, 2, 0, null],
        ["S", [0, 20, 50, 1, 60, 50], 0],
        ["f"],
        // After 'f', pen should be restored to green (0,200,0) and opacity to 0.5
        ["S", [0, 10, 10, 1, 30, 10], 0],
      ],
    });

    // Tag ranges must be bound in reverse pass order: [0, 0] first, then [1, 1]
    const tagUniforms = mock.calls.filter((c) => c.fn === "uniform2f" && c.args[0] === "u_tagRange");
    assert.ok(tagUniforms.length >= 2);
    const firstPassTag = tagUniforms[0];
    const lastPassTag = tagUniforms[tagUniforms.length - 1];
    assert.deepEqual(firstPassTag.args.slice(1), [0, 0], "pass[1] (tag 0..0) executes first");
    assert.deepEqual(lastPassTag.args.slice(1), [1, 1], "pass[0] (tag 1..1) executes second");

    // Check u_tint uniforms: pass[1] has tint [1, 128/255, 0, 102/255], pass[0] (3-element) defaults to [0, 0, 0, 0]
    const tintUniforms = mock.calls.filter((c) => c.fn === "uniform4f" && c.args[0] === "u_tint");
    assert.ok(tintUniforms.length >= 2);
    const firstTint = tintUniforms[0].args.slice(1).map(Number);
    const lastTint = tintUniforms[tintUniforms.length - 1].args.slice(1).map(Number);
    assert.ok(Math.abs(firstTint[0] - 1) < 1e-5);
    assert.ok(Math.abs(firstTint[1] - 128 / 255) < 1e-5);
    assert.ok(Math.abs(firstTint[2] - 0) < 1e-5);
    assert.ok(Math.abs(firstTint[3] - 102 / 255) < 1e-5);
    assert.deepEqual(lastTint, [0, 0, 0, 0], "3-element pass must default tint to [0,0,0,0]");

    // Check premultiplied alpha in u_color:
    // Pass 1 (tag 0..0, passOpacity=0.25):
    //   first stroke uses entry opacity 0.5 * 0.25 = 0.125 (green)
    //   second stroke after ["O", 0.8] uses 0.8 * 0.25 = 0.2 (red)
    // Pass 0 (tag 1..1, passOpacity=1.0):
    //   first stroke uses restored entry opacity 0.5 * 1.0 = 0.5 (green)
    //   second stroke after ["O", 0.8] uses 0.8 * 1.0 = 0.8 (red)
    // After 'f':
    //   stroke uses restored entry opacity 0.5 (green)
    const colorCalls = mock.calls.filter((c) => c.fn === "uniform4f" && c.args[0] === "u_color");
    assert.equal(colorCalls.length, 5);
    const alphas = colorCalls.map((c) => Number(c.args[4]));
    assert.ok(Math.abs(alphas[0] - 0.125) < 1e-5, `expected 0.125, got ${alphas[0]}`);
    assert.ok(Math.abs(alphas[1] - 0.2) < 1e-5, `expected 0.2, got ${alphas[1]}`);
    assert.ok(Math.abs(alphas[2] - 0.5) < 1e-5, `expected 0.5, got ${alphas[2]}`);
    assert.ok(Math.abs(alphas[3] - 0.8) < 1e-5, `expected 0.8, got ${alphas[3]}`);
    assert.ok(Math.abs(alphas[4] - 0.5) < 1e-5, `expected 0.5 after f, got ${alphas[4]}`);
    // Verify green channel is non-zero on restored strokes (index 0, 2, 4)
    assert.ok(Number(colorCalls[2].args[2]) > 0, "pass[0] starts with restored green pen");
    assert.ok(Number(colorCalls[4].args[2]) > 0, "stroke after 'f' uses restored green pen");
  });

  it("uses maxEdge in geometry cache key without colliding with normal geometry, and reuses geometry when field version updates", () => {
    const mock = createMockGlCanvas();
    const renderer = new GlBoardRenderer(mock.canvas, { getDpr: () => 1 });
    const internals = renderer as unknown as { geoCache: Map<string, { vertexCount: number }> };

    const fieldV1 = new Float32Array(2 * 2 * 4);
    renderer.putField({ t: "field", key: "f_geo", w: 2, h: 2, version: 1, bytes: 64 }, fieldV1);

    const sharedLine = [0, 0, 0, 1, 100, 0];
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: 200,
      h: 200,
      keep: 0,
      ops: [
        ["P", 0, 0, 0, 255, 2, 0, 0, 2, 0, null],
        ["S", sharedLine, 0],
        ["F", "f_geo", 1, [0, 0, 200, 200], 4, [{ tagRange: [0, 1], opacity: 1 }]],
        ["S", sharedLine, 0],
        ["f"],
      ],
    });

    const keys = Array.from(internals.geoCache.keys()).filter((k) => k.startsWith("S_"));
    assert.equal(keys.length, 2, "normal stroke and maxEdge-subdivided stroke must have distinct cache keys");
    const normalKey = keys.find((k) => !k.includes("_E4"))!;
    const fieldKey = keys.find((k) => k.includes("_E4"))!;
    assert.ok(normalKey);
    assert.ok(fieldKey);
    assert.equal(internals.geoCache.get(normalKey)!.vertexCount, 6);
    assert.ok(internals.geoCache.get(fieldKey)!.vertexCount >= 150);

    // Update field to version 2 and render again: geometry must NOT be rebuilt or re-uploaded
    const fieldV2 = new Float32Array(2 * 2 * 4);
    fieldV2[0] = 25;
    renderer.putField({ t: "field", key: "f_geo", w: 2, h: 2, version: 2, bytes: 64 }, fieldV2);

    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 2,
      w: 200,
      h: 200,
      keep: 2,
      ops: [
        ["F", "f_geo", 2, [0, 0, 200, 200], 4, [{ tagRange: [0, 1], opacity: 1 }]],
        ["S", sharedLine, 0],
        ["f"],
      ],
    });
    const subUploads = mock.calls.filter((c) => c.fn === "bufferSubData");
    assert.equal(subUploads.length, 0, "field update must not rebuild or re-upload geometry");
  });

  it("draws band-subdivided fill triangles directly inside F..f without stencil, and skips scope on missing key or version mismatch", () => {
    const mock = createMockGlCanvas();
    const renderer = new GlBoardRenderer(mock.canvas, { getDpr: () => 1 });
    const internals = renderer as unknown as { geoCache: Map<string, { vertexCount: number }> };
    const field = new Float32Array(2 * 2 * 4);
    renderer.putField({ t: "field", key: "f_fill", w: 2, h: 2, version: 3, bytes: 64 }, field);

    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: 300,
      h: 200,
      keep: 0,
      ops: [
        ["B", 255, 0, 0, 255],
        ["P", 0, 0, 0, 0, 1, 0, 0, 2, 0, null],
        ["F", "f_fill", 3, [0, 0, 300, 200], 10, [[0, 1, 1, 0, 0, 0, 0]]],
        ["S", [0, 20, 20, 1, 60, 20, 1, 60, 60, 1, 20, 60, 3], 0],
        ["f"],
      ],
    });

    const fillKeys = Array.from(internals.geoCache.keys()).filter((k) => k.startsWith("F_") && k.includes("_E10_"));
    assert.equal(fillKeys.length, 1, "fill inside F..f must cache band-subdivided geometry with maxEdge in key");
    assert.ok(internals.geoCache.get(fillKeys[0])!.vertexCount >= 16 * 6, "40x40 square with maxEdge=10 has >=16 quads");
    assert.equal(
      mock.calls.filter((c) => c.fn === "stencilOp" || c.fn === "stencilOpSeparate").length,
      0,
      "fill inside F..f must draw triangles directly without stencil passes",
    );

    // Version mismatch (asking for version 99 when version 3 is stored) and missing key must skip drawing without error
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 2,
      w: 300,
      h: 200,
      keep: 0,
      ops: [
        ["F", "f_fill", 99, [0, 0, 300, 200], 10, [[0, 1, 1]]],
        ["B", 255, 0, 0, 255],
        ["S", [0, 20, 20, 1, 60, 20, 1, 60, 60, 1, 20, 60, 3], 0],
        ["f"],
        ["F", "missing_key", 1, [0, 0, 300, 200], 10, [[0, 1, 1]]],
        ["B", 255, 0, 0, 255],
        ["S", [0, 20, 20, 1, 60, 20, 1, 60, 60, 1, 20, 60, 3], 0],
        ["f"],
      ],
    });
    assert.equal(renderer.getLastDrawCalls(), 0, "mismatched version or missing key must skip entire F..f scope");
  });

  it("enforces incremental keep constraints: rolls back keep inside F..f, preserves prefix before F on version update, and tolerates incomplete ranges", () => {
    // 1. Unit check on findScopeStartContaining
    const sampleOps: unknown[][] = [
      ["B", 1, 2, 3, 255],
      ["R", 0, 0, 10, 10],
      ["F", "k", 1, [0, 0, 100, 100], 4, [{ tagRange: [1, 1], opacity: 1 }]],
      ["S", [0, 0, 0, 1, 10, 10], 0],
      ["S", [0, 10, 10, 1, 20, 20], 0],
      ["f"],
      ["R", 20, 20, 10, 10],
    ];
    assert.equal(findScopeStartContaining(sampleOps, 2), 2, "keep at F stays at F");
    assert.equal(findScopeStartContaining(sampleOps, 3), 2, "keep inside F..f rolls back to F");
    assert.equal(findScopeStartContaining(sampleOps, 5), 2, "keep right at f (before f is consumed) rolls back to F");
    assert.equal(findScopeStartContaining(sampleOps, 6), 6, "keep after f stays after f");

    // 2. When field version changes and keep stops at F (index 2), prefix 0..2 is NOT redrawn
    const mock = createMockGlCanvas();
    const renderer = new GlBoardRenderer(mock.canvas, { getDpr: () => 1 });
    const internals = renderer as unknown as {
      cachedCount: number;
      executeOps: (ops: unknown[][], start: number, end: number, state: unknown, dpr: number, pw: number, ph: number) => void;
    };

    const fData = new Float32Array(2 * 2 * 4);
    renderer.putField({ t: "field", key: "k", w: 2, h: 2, version: 1, bytes: 64 }, fData);

    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: 100,
      h: 100,
      keep: 0,
      ops: sampleOps.slice(0, 6),
    });
    assert.equal(internals.cachedCount, 2, "prefix before F is cached at F boundary");

    // Update field version to 2; backend sends keep=2 (at F) with updated F op
    renderer.putField({ t: "field", key: "k", w: 2, h: 2, version: 2, bytes: 64 }, fData);
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 2,
      w: 100,
      h: 100,
      keep: 2,
      ops: [
        ["F", "k", 2, [0, 0, 100, 100], 4, [{ tagRange: [1, 1], opacity: 1 }]],
        ["S", [0, 0, 0, 1, 10, 10], 0],
        ["S", [0, 10, 10, 1, 20, 20], 0],
        ["f"],
      ],
    });
    // Prefix 0..2 has ["R", 0, 0, 10, 10] with solid color (1,2,3,255); verify prefixFbo was not cleared or redrawn
    const prefixClears = mock.calls.filter((c) => c.fn === "clear");
    assert.equal(prefixClears.length, 1, "only workFbo is cleared; prefixFbo is not redrawn");
    assert.ok(
      mock.calls.some((c) => c.fn === "uniform1i" && c.args[0] === "u_passthrough" && c.args[1] === 1),
      "prefix texture is copied to workFbo",
    );

    // 3. When keep lands inside F..f (e.g. keep=4), cachedCount rolls back to 2 (before F) and full F..f scope executes
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 3,
      w: 100,
      h: 100,
      keep: 4,
      ops: [
        ["S", [0, 5, 5, 1, 15, 15], 0],
        ["f"],
      ],
    });
    assert.equal(internals.cachedCount, 2, "keep inside F..f must keep cache boundary at F (index 2)");
    assert.equal(renderer.getOps().length, 6);

    // 4. Incomplete start/end range in executeOps does not crash
    const dummyState = (renderer as unknown as { cachedState: unknown }).cachedState;
    assert.doesNotThrow(() => {
      internals.executeOps(sampleOps, 2, 4, dummyState, 1, 100, 100);
      internals.executeOps(sampleOps, 4, 6, dummyState, 1, 100, 100);
      internals.executeOps(sampleOps, -5, 999, dummyState, 1, 100, 100);
    });
  });

  it("preserves F..f ops and rawFields when adopting in both directions between BoardRenderer and GlBoardRenderer", () => {
    const c2d = new BoardRenderer(createMinimal2DCanvas(), {
      getDpr: () => 1,
      createCacheCanvas: () => createMinimal2DCanvas(),
    });
    const fData = new Float32Array([4, -4, 1, 0, 4, -4, 1, 0, 4, -4, 1, 0, 4, -4, 1, 0]);
    c2d.putField({ key: "fld_adopt", w: 2, h: 2, version: 7 }, fData);
    const ops: unknown[][] = [
      ["P", 0, 0, 0, 255, 2, 0, 0, 2, 0, null],
      ["F", "fld_adopt", 7, [0, 0, 100, 100], 4, [{ tagRange: [1, 1], opacity: 1 }]],
      ["S", [0, 10, 10, 1, 50, 10], 0],
      ["f"],
    ];
    c2d.renderFrame({ t: "board", view: "main", frame: 1, w: 100, h: 100, keep: 0, ops });

    // Hand off from Canvas 2D -> WebGL2
    const mockGl = createMockGlCanvas();
    const glRenderer = new GlBoardRenderer(mockGl.canvas, { getDpr: () => 1 });
    mockGl.calls.length = 0;
    glRenderer.adopt(c2d);

    assert.equal(glRenderer.getOps().length, 4);
    assert.equal(glRenderer.getField("fld_adopt")?.version, 7);
    assert.ok(glRenderer.getLastDrawCalls() > 0, "GlBoardRenderer immediately renders adopted F..f scope");

    // Hand off from WebGL2 -> Canvas 2D
    const c2dNext = new BoardRenderer(createMinimal2DCanvas(), {
      getDpr: () => 1,
      createCacheCanvas: () => createMinimal2DCanvas(),
    });
    c2dNext.adopt(glRenderer as never);
    assert.equal(c2dNext.getOps().length, 4);
    assert.equal(c2dNext.getOps()[1][0], "F");
    assert.equal(c2dNext.getField("fld_adopt")?.version, 7);
  });

  it("reports board.capabilities on mount and renderer switch, and swallows backend rejection", async () => {
    const prevDoc = globalThis.document;
    const prevWin = globalThis.window;
    const prevStorage = globalThis.localStorage;
    const prevRO = globalThis.ResizeObserver;

    const mockGl = createMockGlCanvas();
    const calls: Array<{ method: string; params: unknown }> = [];
    let rejectCall = false;

    const fakeElement = (): Record<string, unknown> => {
      const children: unknown[] = [];
      const el: Record<string, unknown> = {
        className: "",
        style: {},
        dataset: {},
        children,
        classList: { add() {}, remove() {}, toggle() {} },
        append(...nodes: unknown[]) {
          children.push(...nodes);
        },
        replaceWith() {},
        replaceChildren() {},
        addEventListener() {},
        removeEventListener() {},
        setAttribute() {},
        focus() {},
        querySelector: () => fakeElement(),
        getBoundingClientRect: () => ({ left: 0, top: 0, width: 200, height: 150 }),
        getContext(kind: string) {
          if (kind === "webgl2") return mockGl.gl;
          if (kind === "2d") return createMinimal2DCanvas().getContext("2d");
          return null;
        },
      };
      return el;
    };

    try {
      const storageMap = new Map<string, string>([["animean.boardRenderer", "webgl"]]);
      globalThis.localStorage = {
        getItem: (k: string) => storageMap.get(k) ?? null,
        setItem: (k: string, v: string) => {
          storageMap.set(k, v);
        },
      } as unknown as Storage;
      globalThis.ResizeObserver = class {
        observe() {}
        disconnect() {}
      } as unknown as typeof ResizeObserver;
      globalThis.window = {
        devicePixelRatio: 1,
        addEventListener() {},
      } as unknown as Window & typeof globalThis;
      globalThis.document = {
        activeElement: null,
        createElement: () => fakeElement(),
        documentElement: { dataset: {} },
        body: fakeElement(),
      } as unknown as Document;

      const client = {
        onState: () => () => {},
        onImage: () => () => {},
        onField: () => () => {},
        onBoard: () => () => {},
        sendAck() {},
        call(method: string, params: unknown) {
          calls.push({ method, params });
          if (rejectCall) {
            return Promise.reject(new Error("unknown method: board.capabilities"));
          }
          return Promise.resolve({});
        },
      };

      const { createStore, panelFactories } = await import("../src/ui/app.js");
      const store = createStore(client as never) as Record<string, unknown>;
      const factories = panelFactories(store as never, client as never);
      const centralEl = fakeElement();
      factories.QtCentral(centralEl as never);

      const capCallsInit = calls.filter((c) => c.method === "board.capabilities");
      assert.equal(capCallsInit.length, 2);
      assert.deepEqual(capCallsInit[0].params, { view: "main", fieldDisplay: true });
      assert.deepEqual(capCallsInit[1].params, { view: "child", fieldDisplay: true });

      // Switch to canvas2d while backend rejects board.capabilities -> must report false and swallow error
      calls.length = 0;
      rejectCall = true;
      const boards = store.boards as { setRenderer: (k: string) => void };
      boards.setRenderer("canvas2d");
      await new Promise((r) => setTimeout(r, 10));

      const capCallsSwitch = calls.filter((c) => c.method === "board.capabilities");
      assert.equal(capCallsSwitch.length, 2);
      assert.deepEqual(capCallsSwitch[0].params, { view: "main", fieldDisplay: false });
      assert.deepEqual(capCallsSwitch[1].params, { view: "child", fieldDisplay: false });
    } finally {
      globalThis.document = prevDoc;
      globalThis.window = prevWin;
      globalThis.localStorage = prevStorage;
      globalThis.ResizeObserver = prevRO;
    }
  });
});
