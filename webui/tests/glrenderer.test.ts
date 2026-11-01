import assert from "node:assert/strict";
import { describe, it } from "node:test";
import {
  GlBoardRenderer,
  type GlCanvasLike,
  type WebGL2ContextLike,
} from "../src/board/glrenderer.ts";
import { BoardRenderer, type Canvas2DContextLike, type CanvasLike } from "../src/board/renderer.ts";

interface GlCall {
  fn: string;
  args: unknown[];
}

function createMockGlCanvas() {
  const calls: GlCall[] = [];
  const listeners = new Map<string, Array<(ev: Event) => void>>();
  let nextId = 1;
  let throwOnNextDraw = false;
  let currentRenderbuffer: WebGLRenderbuffer | null = null;
  let currentDrawFbo: WebGLFramebuffer | null = null;
  const multisampledRbos = new Set<WebGLRenderbuffer>();
  const multisampledFbos = new Set<WebGLFramebuffer>();

  const gl: WebGL2ContextLike = {
    ARRAY_BUFFER: 0x8892,
    STATIC_DRAW: 0x88e4,
    DYNAMIC_DRAW: 0x88e8,
    FLOAT: 0x1406,
    TRIANGLES: 0x0004,
    VERTEX_SHADER: 0x8b31,
    FRAGMENT_SHADER: 0x8b30,
    COMPILE_STATUS: 0x8b81,
    LINK_STATUS: 0x8b82,
    COLOR_BUFFER_BIT: 0x4000,
    DEPTH_BUFFER_BIT: 0x0100,
    STENCIL_BUFFER_BIT: 0x0400,
    FRAMEBUFFER: 0x8d40,
    READ_FRAMEBUFFER: 0x8ca8,
    DRAW_FRAMEBUFFER: 0x8ca9,
    RENDERBUFFER: 0x8d41,
    COLOR_ATTACHMENT0: 0x8ce0,
    DEPTH_STENCIL_ATTACHMENT: 0x821a,
    RGBA8: 0x8058,
    DEPTH24_STENCIL8: 0x88f0,
    MAX_SAMPLES: 0x8d57,
    FRAMEBUFFER_COMPLETE: 0x8cd5,
    TEXTURE_2D: 0x0de1,
    TEXTURE0: 0x84c0,
    TEXTURE_MIN_FILTER: 0x2801,
    TEXTURE_MAG_FILTER: 0x2800,
    TEXTURE_WRAP_S: 0x2802,
    TEXTURE_WRAP_T: 0x2803,
    NEAREST: 0x2600,
    LINEAR: 0x2601,
    CLAMP_TO_EDGE: 0x812f,
    RGBA: 0x1908,
    UNSIGNED_BYTE: 0x1401,
    UNPACK_ALIGNMENT: 0x0cf5,
    BLEND: 0x0be2,
    STENCIL_TEST: 0x0b90,
    DEPTH_TEST: 0x0b71,
    CULL_FACE: 0x0b44,
    ZERO: 0,
    ONE: 1,
    SRC_ALPHA: 0x0302,
    ONE_MINUS_SRC_ALPHA: 0x0303,
    DST_ALPHA: 0x0304,
    ONE_MINUS_DST_ALPHA: 0x0305,
    KEEP: 0x1e00,
    REPLACE: 0x1e01,
    INCR_WRAP: 0x8507,
    DECR_WRAP: 0x8508,
    INVERT: 0x150a,
    ALWAYS: 0x0207,
    EQUAL: 0x0202,
    NOTEQUAL: 0x0205,
    FRONT: 0x0404,
    BACK: 0x0405,

    getParameter(pname) {
      calls.push({ fn: "getParameter", args: [pname] });
      if (pname === this.MAX_SAMPLES) return 8;
      return 1;
    },
    viewport(x, y, w, h) {
      calls.push({ fn: "viewport", args: [x, y, w, h] });
    },
    clearColor(r, g, b, a) {
      calls.push({ fn: "clearColor", args: [r, g, b, a] });
    },
    clearStencil(s) {
      calls.push({ fn: "clearStencil", args: [s] });
    },
    clear(mask) {
      calls.push({ fn: "clear", args: [mask] });
    },
    enable(cap) {
      calls.push({ fn: "enable", args: [cap] });
    },
    disable(cap) {
      calls.push({ fn: "disable", args: [cap] });
    },
    colorMask(r, g, b, a) {
      calls.push({ fn: "colorMask", args: [r, g, b, a] });
    },
    blendFunc(s, d) {
      calls.push({ fn: "blendFunc", args: [s, d] });
    },
    stencilMask(mask) {
      calls.push({ fn: "stencilMask", args: [mask] });
    },
    stencilFunc(func, ref, mask) {
      calls.push({ fn: "stencilFunc", args: [func, ref, mask] });
    },
    stencilOp(fail, zfail, zpass) {
      calls.push({ fn: "stencilOp", args: [fail, zfail, zpass] });
    },
    stencilOpSeparate(face, fail, zfail, zpass) {
      calls.push({ fn: "stencilOpSeparate", args: [face, fail, zfail, zpass] });
    },
    createShader(type) {
      const obj = { kind: "shader", id: nextId++, type } as unknown as WebGLShader;
      calls.push({ fn: "createShader", args: [type] });
      return obj;
    },
    shaderSource(shader, src) {
      calls.push({ fn: "shaderSource", args: [shader, src] });
    },
    compileShader(shader) {
      calls.push({ fn: "compileShader", args: [shader] });
    },
    getShaderParameter(_shader, _pname) {
      return true;
    },
    getShaderInfoLog() {
      return "";
    },
    deleteShader(shader) {
      calls.push({ fn: "deleteShader", args: [shader] });
    },
    createProgram() {
      const obj = { kind: "program", id: nextId++ } as unknown as WebGLProgram;
      calls.push({ fn: "createProgram", args: [] });
      return obj;
    },
    attachShader(p, s) {
      calls.push({ fn: "attachShader", args: [p, s] });
    },
    linkProgram(p) {
      calls.push({ fn: "linkProgram", args: [p] });
    },
    getProgramParameter() {
      return true;
    },
    getProgramInfoLog() {
      return "";
    },
    useProgram(p) {
      calls.push({ fn: "useProgram", args: [p] });
    },
    deleteProgram(p) {
      calls.push({ fn: "deleteProgram", args: [p] });
    },
    getUniformLocation(_p, name) {
      return { name } as unknown as WebGLUniformLocation;
    },
    uniform1i(loc, x) {
      calls.push({ fn: "uniform1i", args: [(loc as { name?: string })?.name, x] });
    },
    uniform1f(loc, x) {
      calls.push({ fn: "uniform1f", args: [(loc as { name?: string })?.name, x] });
    },
    uniform2f(loc, x, y) {
      calls.push({ fn: "uniform2f", args: [(loc as { name?: string })?.name, x, y] });
    },
    uniform3f(loc, x, y, z) {
      calls.push({ fn: "uniform3f", args: [(loc as { name?: string })?.name, x, y, z] });
    },
    uniform4f(loc, x, y, z, w) {
      calls.push({ fn: "uniform4f", args: [(loc as { name?: string })?.name, x, y, z, w] });
    },
    createVertexArray() {
      const obj = { kind: "vao", id: nextId++ } as unknown as WebGLVertexArrayObject;
      calls.push({ fn: "createVertexArray", args: [] });
      return obj;
    },
    bindVertexArray(vao) {
      calls.push({ fn: "bindVertexArray", args: [vao] });
    },
    deleteVertexArray(vao) {
      calls.push({ fn: "deleteVertexArray", args: [vao] });
    },
    createBuffer() {
      const obj = { kind: "buffer", id: nextId++ } as unknown as WebGLBuffer;
      calls.push({ fn: "createBuffer", args: [obj] });
      return obj;
    },
    bindBuffer(target, buf) {
      calls.push({ fn: "bindBuffer", args: [target, buf] });
    },
    bufferSubData(target, offset, data) {
      calls.push({ fn: "bufferSubData", args: [target, offset, new Float32Array(data as Float32Array)] });
    },
    bufferData(target, data, usage) {
      const copy = data instanceof Float32Array ? new Float32Array(data) : data;
      calls.push({ fn: "bufferData", args: [target, copy, usage] });
    },
    deleteBuffer(buf) {
      calls.push({ fn: "deleteBuffer", args: [buf] });
    },
    enableVertexAttribArray(idx) {
      calls.push({ fn: "enableVertexAttribArray", args: [idx] });
    },
    vertexAttribPointer(idx, size, type, norm, stride, offset) {
      calls.push({ fn: "vertexAttribPointer", args: [idx, size, type, norm, stride, offset] });
    },
    drawArrays(mode, first, count) {
      if (throwOnNextDraw) {
        throwOnNextDraw = false;
        throw new Error("Simulated GL draw failure");
      }
      calls.push({ fn: "drawArrays", args: [mode, first, count] });
    },
    createFramebuffer() {
      const obj = { kind: "fbo", id: nextId++ } as unknown as WebGLFramebuffer;
      calls.push({ fn: "createFramebuffer", args: [obj] });
      return obj;
    },
    bindFramebuffer(target, fbo) {
      if (target === this.FRAMEBUFFER || target === this.DRAW_FRAMEBUFFER) {
        currentDrawFbo = fbo;
      }
      calls.push({ fn: "bindFramebuffer", args: [target, fbo] });
    },
    framebufferRenderbuffer(target, attachment, rbtarget, rb) {
      if (
        (target === this.FRAMEBUFFER || target === this.DRAW_FRAMEBUFFER) &&
        currentDrawFbo &&
        rb &&
        multisampledRbos.has(rb)
      ) {
        multisampledFbos.add(currentDrawFbo);
      }
      calls.push({ fn: "framebufferRenderbuffer", args: [target, attachment, rbtarget, rb] });
    },
    framebufferTexture2D(target, attachment, textarget, texture, level) {
      calls.push({
        fn: "framebufferTexture2D",
        args: [target, attachment, textarget, texture, level],
      });
    },
    blitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter) {
      if (currentDrawFbo && multisampledFbos.has(currentDrawFbo)) {
        throw new Error("INVALID_OPERATION: blitFramebuffer DRAW_FRAMEBUFFER is multisampled");
      }
      calls.push({
        fn: "blitFramebuffer",
        args: [sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter],
      });
    },
    deleteFramebuffer(fbo) {
      if (fbo) multisampledFbos.delete(fbo);
      calls.push({ fn: "deleteFramebuffer", args: [fbo] });
    },
    createRenderbuffer() {
      const obj = { kind: "rbo", id: nextId++ } as unknown as WebGLRenderbuffer;
      calls.push({ fn: "createRenderbuffer", args: [obj] });
      return obj;
    },
    bindRenderbuffer(target, rb) {
      currentRenderbuffer = rb;
      calls.push({ fn: "bindRenderbuffer", args: [target, rb] });
    },
    renderbufferStorageMultisample(target, samples, fmt, w, h) {
      if (samples > 0 && currentRenderbuffer) {
        multisampledRbos.add(currentRenderbuffer);
      }
      calls.push({ fn: "renderbufferStorageMultisample", args: [target, samples, fmt, w, h] });
    },
    deleteRenderbuffer(rb) {
      if (rb) multisampledRbos.delete(rb);
      calls.push({ fn: "deleteRenderbuffer", args: [rb] });
    },
    createTexture() {
      const obj = { kind: "tex", id: nextId++ } as unknown as WebGLTexture;
      calls.push({ fn: "createTexture", args: [obj] });
      return obj;
    },
    bindTexture(target, tex) {
      calls.push({ fn: "bindTexture", args: [target, tex] });
    },
    activeTexture(unit) {
      calls.push({ fn: "activeTexture", args: [unit] });
    },
    pixelStorei(pname, param) {
      calls.push({ fn: "pixelStorei", args: [pname, param] });
    },
    texParameteri(target, pname, param) {
      calls.push({ fn: "texParameteri", args: [target, pname, param] });
    },
    texImage2D(target, level, internalformat, w, h, border, format, type, pixels) {
      calls.push({
        fn: "texImage2D",
        args: [target, level, internalformat, w, h, border, format, type, pixels],
      });
    },
    deleteTexture(tex) {
      calls.push({ fn: "deleteTexture", args: [tex] });
    },
  };

  const canvas: GlCanvasLike = {
    width: 0,
    height: 0,
    getContext(_id, opts) {
      calls.push({ fn: "getContext", args: [opts] });
      return gl;
    },
    addEventListener(type, fn) {
      const arr = listeners.get(type) ?? [];
      arr.push(fn);
      listeners.set(type, arr);
    },
    removeEventListener(type, fn) {
      const arr = listeners.get(type);
      if (!arr) return;
      const idx = arr.indexOf(fn);
      if (idx >= 0) arr.splice(idx, 1);
    },
  };

  return {
    canvas,
    gl,
    calls,
    setThrowOnNextDraw(v: boolean) {
      throwOnNextDraw = v;
    },
    dispatchEvent(type: string, ev: Event) {
      for (const fn of listeners.get(type) ?? []) {
        fn(ev);
      }
    },
  };
}

describe("GlBoardRenderer", () => {
  it("handles all three keep branches (keep=0, cachedCount<keep, cachedCount>keep) with MSAA prefix/work FBOs", () => {
    const mock = createMockGlCanvas();
    const acks: Array<{ view: string; frame: number }> = [];
    const renderer = new GlBoardRenderer(mock.canvas, {
      view: "main",
      getDpr: () => 2,
      onAck: (view, frame) => acks.push({ view, frame }),
    });

    // Branch 1: keep = 0 (initial full draw into prefixFbo and resolve to screen)
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: 400,
      h: 300,
      keep: 0,
      ops: [
        ["T", 1, 0, 0, 1, 0, 0],
        ["P", 0, 0, 0, 0, 1, 0, 0, 2, 0, null],
        ["B", 255, 255, 255, 255],
        ["R", 0, 0, 400, 300],
      ],
    });

    assert.equal(mock.canvas.width, 800);
    assert.equal(mock.canvas.height, 600);
    assert.equal(renderer.getOps().length, 4);
    assert.deepEqual(acks, [{ view: "main", frame: 1 }]);

    const fbos = mock.calls.filter((c) => c.fn === "createFramebuffer").map((c) => c.args[0]);
    assert.equal(fbos.length, 3, "Should create prefixFbo, prefixResolveFbo, and workFbo");
    const [prefixFbo, prefixResolveFbo, workFbo] = fbos;

    // Frame 2: keep = 4 (cachedCount === keep), plus 2 suffix ops
    // First time suffix is used after keep=0: resolves prefixFbo -> prefixResolveFbo once,
    // then clears workFbo and draws prefixTexture fullscreen quad with u_passthrough=1 and BLEND disabled.
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 2,
      w: 400,
      h: 300,
      keep: 4,
      ops: [
        ["P", 20, 40, 60, 255, 4, 2, 2, 2, 0, null],
        ["S", [0, 10, 10, 1, 100, 10], 0],
      ],
    });
    assert.equal(renderer.getOps().length, 6);
    assert.equal(
      mock.calls.filter((c) => c.fn === "clear").length,
      1,
      "When cachedCount === keep, only workFbo is cleared (prefixFbo is not cleared)",
    );
    assert.ok(
      mock.calls.some(
        (c) => c.fn === "bindFramebuffer" && c.args[0] === mock.gl.READ_FRAMEBUFFER && c.args[1] === prefixFbo,
      ),
    );
    assert.ok(
      mock.calls.some(
        (c) =>
          c.fn === "bindFramebuffer" &&
          c.args[0] === mock.gl.DRAW_FRAMEBUFFER &&
          c.args[1] === prefixResolveFbo,
      ),
    );
    assert.ok(
      mock.calls.some((c) => c.fn === "uniform1i" && c.args[0] === "u_passthrough" && c.args[1] === 1),
      "Should draw prefixTexture quad onto workFbo with u_passthrough = 1",
    );

    // Frame 2b: keep = 4 again (prefix did not change and is already resolved): must NOT re-resolve prefixFbo
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 2,
      w: 400,
      h: 300,
      keep: 4,
      ops: [
        ["P", 20, 40, 60, 255, 4, 2, 2, 2, 0, null],
        ["S", [0, 10, 10, 1, 120, 30], 0],
      ],
    });
    assert.equal(
      mock.calls.some(
        (c) =>
          c.fn === "bindFramebuffer" &&
          c.args[0] === mock.gl.DRAW_FRAMEBUFFER &&
          c.args[1] === prefixResolveFbo,
      ),
      false,
      "Unchanged prefix must not be resolved to prefixResolveFbo again",
    );

    // Branch 2: cachedCount (4) < keep (6), appending ops 4..6 into prefixFbo WITHOUT clearing prefixFbo
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 3,
      w: 400,
      h: 300,
      keep: 6,
      ops: [["S", [0, 20, 20, 1, 120, 20], 0]],
    });
    assert.equal(renderer.getOps().length, 7);
    assert.equal(
      mock.calls.filter((c) => c.fn === "clear").length,
      1,
      "When cachedCount < keep (and cachedCount > 0), prefixFbo must be appended without clearing (only workFbo is cleared)",
    );
    const bindPrefixIdx = mock.calls.findIndex(
      (c) => c.fn === "bindFramebuffer" && c.args[0] === mock.gl.FRAMEBUFFER && c.args[1] === prefixFbo,
    );
    const blitIdx = mock.calls.findIndex((c) => c.fn === "blitFramebuffer");
    assert.ok(
      bindPrefixIdx >= 0 && bindPrefixIdx < blitIdx,
      "Should append to prefixFbo before resolving to prefixResolveFbo",
    );

    // Branch 3: cachedCount (6) > keep (3), must clear prefixFbo and redraw 0..3 (plus clear workFbo)
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 4,
      w: 400,
      h: 300,
      keep: 3,
      ops: [["R", 10, 10, 50, 50]],
    });
    assert.equal(renderer.getOps().length, 4);
    assert.equal(
      mock.calls.filter((c) => c.fn === "clear").length,
      2,
      "When cachedCount > keep, both prefixFbo and workFbo must be cleared",
    );

    // Guard rule: blitting directly into a multisampled DRAW_FRAMEBUFFER must throw
    mock.gl.bindFramebuffer(mock.gl.READ_FRAMEBUFFER, prefixFbo as WebGLFramebuffer);
    mock.gl.bindFramebuffer(mock.gl.DRAW_FRAMEBUFFER, workFbo as WebGLFramebuffer);
    assert.throws(() => {
      mock.gl.blitFramebuffer(0, 0, 800, 600, 0, 0, 800, 600, mock.gl.COLOR_BUFFER_BIT, mock.gl.NEAREST);
    }, /INVALID_OPERATION: blitFramebuffer DRAW_FRAMEBUFFER is multisampled/);

    // Frame 5: active clip in prefix is re-written into workFbo stencil bit 0x80 at suffix start
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 5,
      w: 400,
      h: 300,
      keep: 0,
      ops: [
        ["K", [0, 10, 10, 1, 200, 10, 1, 200, 200, 1, 10, 200, 3], 1],
        ["B", 200, 100, 50, 255],
      ],
    });
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 6,
      w: 400,
      h: 300,
      keep: 2,
      ops: [["R", 20, 20, 80, 80]],
    });
    const passthroughIdx = mock.calls.findIndex(
      (c) => c.fn === "uniform1i" && c.args[0] === "u_passthrough" && c.args[1] === 1,
    );
    const clipRestoreIdx = mock.calls.findIndex(
      (c, idx) =>
        idx > passthroughIdx &&
        c.fn === "stencilFunc" &&
        c.args[0] === mock.gl.NOTEQUAL &&
        c.args[1] === 0x80 &&
        c.args[2] === 0x7f,
    );
    assert.ok(
      passthroughIdx >= 0 && clipRestoreIdx > passthroughIdx,
      "Must re-write active clip into workFbo stencil bit 0x80 after copying prefixTexture",
    );
  });

  it("sends ack in finally even when drawFrame throws an exception", () => {
    const mock = createMockGlCanvas();
    const clientAcks: Array<{ view: string; frame: number }> = [];
    const callbackAcks: Array<{ view: string; frame: number }> = [];

    const renderer = new GlBoardRenderer(mock.canvas, {
      view: "main",
      client: {
        sendAck(view: string, frame: number) {
          clientAcks.push({ view, frame });
        },
      } as unknown as import("../src/net.ts").BackendClient,
      onAck(view, frame) {
        callbackAcks.push({ view, frame });
      },
    });

    mock.setThrowOnNextDraw(true);
    assert.throws(() => {
      renderer.renderFrame({
        t: "board",
        view: "main",
        frame: 99,
        w: 200,
        h: 200,
        keep: 0,
        ops: [
          ["B", 255, 0, 0, 255],
          ["R", 0, 0, 100, 100],
        ],
      });
    }, /Simulated GL draw failure/);

    assert.deepEqual(clientAcks, [{ view: "main", frame: 99 }]);
    assert.deepEqual(callbackAcks, [{ view: "main", frame: 99 }]);
  });

  it("does not re-upload geometry on cache hit and only re-uploads when scale changes across bucket boundary", () => {
    const mock = createMockGlCanvas();
    const renderer = new GlBoardRenderer(mock.canvas, { getDpr: () => 1 });
    const curvePath = [0, 10, 10, 2, 30, 50, 70, 50, 90, 10];

    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: 300,
      h: 300,
      keep: 0,
      ops: [
        ["T", 1, 0, 0, 1, 0, 0],
        ["P", 10, 20, 30, 255, 4, 2, 2, 2, 0, null],
        ["S", curvePath, 0],
      ],
    });
    const uploadsFrame1 = mock.calls.filter((c) => c.fn === "bufferSubData").length;
    assert.equal(uploadsFrame1, 1, "First frame should upload stroke geometry once");

    // Frame 2: pan + slight zoom (1.15x stays within 1.75x builtScale) => cache hit, zero bufferData calls!
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 2,
      w: 300,
      h: 300,
      keep: 0,
      ops: [
        ["T", 1.15, 0, 0, 1.15, 50, 30],
        ["P", 10, 20, 30, 255, 4, 2, 2, 2, 0, null],
        ["S", curvePath, 0],
      ],
    });
    const uploadsFrame2 = mock.calls.filter((c) => c.fn === "bufferSubData").length;
    assert.equal(uploadsFrame2, 0, "Cache hit within scale bucket must not call bufferData");
    assert.ok(
      mock.calls.some(
        (c) => c.fn === "uniform3f" && c.args[0] === "u_transform0" && c.args[1] === 1.15 && c.args[3] === 50,
      ),
      "Should update transform uniform on cache hit",
    );

    // Frame 3: zoom out (0.25x < builtScale) => always reuses existing geometry without re-uploading!
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 3,
      w: 300,
      h: 300,
      keep: 0,
      ops: [
        ["T", 0.25, 0, 0, 0.25, 0, 0],
        ["P", 10, 20, 30, 255, 4, 2, 2, 2, 0, null],
        ["S", curvePath, 0],
      ],
    });
    const uploadsFrame3 = mock.calls.filter((c) => c.fn === "bufferSubData").length;
    assert.equal(uploadsFrame3, 0, "Zooming out must always reuse existing cached geometry");

    // Frame 4: zoom in 4x crosses scale bucket boundary => re-tessellates and uploads once
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 4,
      w: 300,
      h: 300,
      keep: 0,
      ops: [
        ["T", 4, 0, 0, 4, 0, 0],
        ["P", 10, 20, 30, 255, 4, 2, 2, 2, 0, null],
        ["S", curvePath, 0],
      ],
    });
    const uploadsFrame4 = mock.calls.filter((c) => c.fn === "bufferSubData").length;
    assert.equal(uploadsFrame4, 1, "Crossing scale bucket when zooming in should re-tessellate and upload once");
  });

  it("handles cosmetic pen by transforming path coordinates while keeping screen-space stroke width", () => {
    const mock = createMockGlCanvas();
    const renderer = new GlBoardRenderer(mock.canvas, { getDpr: () => 2 });

    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: 300,
      h: 300,
      keep: 0,
      ops: [
        ["T", 3, 0, 0, 3, 15, 25],
        ["P", 0, 200, 100, 255, 4, 0, 0, 2, 1, null], // cosmetic = 1, width = 4, butt cap
        ["S", [0, 0, 0, 1, 10, 0], 0],
      ],
    });

    const uploadCall = mock.calls.find((c) => c.fn === "bufferSubData");
    assert.ok(uploadCall, "Should upload cosmetic stroke geometry");
    const strokeVerts = uploadCall.args[2] as Float32Array;
    assert.equal(strokeVerts.length, 12, "Opaque direct stroke uploads only its 6 vertices (12 floats)");
    let minX = Infinity;
    let maxX = -Infinity;
    let minY = Infinity;
    let maxY = -Infinity;
    for (let i = 0; i < strokeVerts.length; i += 2) {
      minX = Math.min(minX, strokeVerts[i]);
      maxX = Math.max(maxX, strokeVerts[i]);
      minY = Math.min(minY, strokeVerts[i + 1]);
      maxY = Math.max(maxY, strokeVerts[i + 1]);
    }
    // Length [0..10] scaled by T's m11=3 becomes [0..30], while width stays 4 ([-2..+2]), not 4*3=12!
    assert.ok(Math.abs(maxX - minX - 30) < 1e-4, `Expected transformed length 30, got ${maxX - minX}`);
    assert.ok(Math.abs(maxY - minY - 4) < 1e-4, `Expected cosmetic width 4, got ${maxY - minY}`);

    // Shader transform uniform should use identity linear part [1, 0, dx=15] and [0, 1, dy=25].
    assert.ok(
      mock.calls.some(
        (c) => c.fn === "uniform3f" && c.args[0] === "u_transform0" && c.args[1] === 1 && c.args[3] === 15,
      ),
    );
  });

  it("adopts ops and images from a previous renderer and redraws immediately", () => {
    const noopCtx = {
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
      createLinearGradient: () => ({ addColorStop() {} }),
      createRadialGradient: () => ({ addColorStop() {} }),
      drawImage() {},
      fillStyle: "",
      strokeStyle: "",
      lineWidth: 1,
      lineCap: "butt",
      lineJoin: "miter",
      miterLimit: 2,
      lineDashOffset: 0,
      globalAlpha: 1,
      globalCompositeOperation: "source-over",
    } as unknown as Canvas2DContextLike;
    const prevCanvas: CanvasLike = { width: 200, height: 150, getContext: () => noopCtx };
    const prev = new BoardRenderer(prevCanvas, { getDpr: () => 1 });
    prev.putImage({ t: "image", key: "k1", w: 4, h: 4, bytes: 64 }, new Uint8Array(64));
    prev.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: 200,
      h: 150,
      keep: 0,
      ops: [
        ["B", 100, 120, 140, 255],
        ["R", 0, 0, 200, 150],
        ["I", "k1", 0, 0, 4, 4, 10, 10, 20, 20],
      ],
    });

    const mock = createMockGlCanvas();
    const glRenderer = new GlBoardRenderer(mock.canvas, { getDpr: () => 1 });
    mock.calls.length = 0;
    glRenderer.adopt(prev);

    assert.equal(glRenderer.getOps().length, 3);
    assert.ok(glRenderer.getImage("k1"));
    assert.ok(mock.calls.some((c) => c.fn === "texImage2D"), "Should upload adopted image texture and draw");
    assert.ok(mock.calls.some((c) => c.fn === "blitFramebuffer"), "Should resolve adopted frame to screen");
  });

  it("handles K clip stencil bit 0x80, G gradients, M composite modes, batching, and context loss/restore", () => {
    const mock = createMockGlCanvas();
    const renderer = new GlBoardRenderer(mock.canvas, { getDpr: () => 1 });

    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: 300,
      h: 300,
      keep: 0,
      ops: [
        ["K", [0, 10, 10, 1, 200, 10, 1, 200, 200, 1, 10, 200, 3], 1],
        ["G", "radial", [100, 100, 10, 100, 100, 80], [[0, 255, 0, 0, 255], [1, 0, 0, 255, 128]]],
        ["O", 0.5],
        ["M", "destination-out"],
        ["S", [0, 20, 20, 1, 180, 20, 1, 180, 180, 3], 1],
        ["K", null, 0],
        ["O", 1],
        ["M", "source-over"],
        ["B", 0, 0, 0, 0],
        ["P", 10, 20, 30, 255, 3, 2, 2, 2, 0, null],
        ["S", [0, 0, 0, 1, 50, 50], 0],
        ["S", [0, 50, 50, 1, 100, 20], 0],
      ],
    });

    // Clip promotion pass uses stencilFunc(NOTEQUAL, 0x80, 0x7f)
    assert.ok(
      mock.calls.some(
        (c) => c.fn === "stencilFunc" && c.args[0] === mock.gl.NOTEQUAL && c.args[1] === 0x80 && c.args[2] === 0x7f,
      ),
    );
    // destination-out sets blendFunc(ZERO, ONE_MINUS_SRC_ALPHA)
    assert.ok(
      mock.calls.some(
        (c) => c.fn === "blendFunc" && c.args[0] === mock.gl.ZERO && c.args[1] === mock.gl.ONE_MINUS_SRC_ALPHA,
      ),
    );

    // Simulate webglcontextlost and webglcontextrestored
    let prevented = false;
    mock.dispatchEvent("webglcontextlost", {
      preventDefault() {
        prevented = true;
      },
    } as unknown as Event);
    assert.equal(prevented, true);

    mock.calls.length = 0;
    mock.dispatchEvent("webglcontextrestored", {} as unknown as Event);
    assert.ok(mock.calls.some((c) => c.fn === "createProgram"), "Should recreate shaders on context restore");
    assert.ok(mock.calls.some((c) => c.fn === "blitFramebuffer"), "Should redraw saved ops on context restore");
  });

  it("batches adjacent same-state opaque source-over strokes into a single drawArrays call while preserving painter order across colors", () => {
    const mock = createMockGlCanvas();
    const renderer = new GlBoardRenderer(mock.canvas, { getDpr: () => 1 });

    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: 400,
      h: 400,
      keep: 0,
      ops: [
        ["T", 1, 0, 0, 1, 0, 0],
        ["B", 0, 0, 0, 0],
        ["P", 20, 30, 40, 255, 3, 2, 2, 2, 0, null],
        ["S", [0, 10, 10, 1, 50, 10], 0],
        ["S", [0, 10, 20, 1, 50, 20], 0],
        ["S", [0, 10, 30, 1, 50, 30], 0],
        ["P", 200, 40, 50, 255, 3, 2, 2, 2, 0, null],
        ["S", [0, 10, 40, 1, 50, 40], 0],
        ["S", [0, 10, 50, 1, 50, 50], 0],
      ],
    });

    assert.equal(renderer.getLastDrawCalls(), 2, "3 dark strokes + 2 red strokes should merge into exactly 2 drawArrays calls");
    const drawCalls = mock.calls.filter((c) => c.fn === "drawArrays");
    assert.equal(drawCalls.length, 2);
    assert.deepEqual(drawCalls[0].args, [mock.gl.TRIANGLES, 0, 162], "First batch draws 3 strokes contiguously");
    assert.deepEqual(drawCalls[1].args, [mock.gl.TRIANGLES, 162, 108], "Second batch draws 2 strokes contiguously");

    // Warm redraw of the same ops: 0 bufferData uploads, still 2 drawArrays calls
    mock.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 2,
      w: 400,
      h: 400,
      keep: 0,
      ops: renderer.getOps() as unknown[][],
    });
    assert.equal(mock.calls.filter((c) => c.fn === "bufferSubData").length, 0, "Warm frame must do 0 bufferData uploads");
    assert.equal(renderer.getLastDrawCalls(), 2);
  });

  it("drops cached geometry only between frames, so a frame past the limit still draws every stroke", () => {
    const mock = createMockGlCanvas();
    const renderer = new GlBoardRenderer(mock.canvas, { getDpr: () => 1 });
    const internals = renderer as unknown as { maxGeoCacheEntries: number; geoCache: Map<string, unknown> };
    internals.maxGeoCacheEntries = 2;
    const frame = (n: number, firstY: number) => ({
      t: "board" as const,
      view: "main",
      frame: n,
      w: 400,
      h: 400,
      keep: 0,
      ops: [
        ["T", 1, 0, 0, 1, 0, 0],
        ["B", 0, 0, 0, 0],
        ["P", 20, 30, 40, 255, 3, 2, 2, 2, 0, null],
        ...[0, 1, 2, 3, 4].map((i) => ["S", [0, 10, firstY + i * 10, 1, 50, firstY + i * 10], 0]),
      ] as unknown[][],
    });

    mock.calls.length = 0;
    renderer.renderFrame(frame(1, 10));
    assert.equal(internals.geoCache.size, 5, "the frame keeps every entry it handed out");
    const draws = mock.calls.filter((c) => c.fn === "drawArrays");
    assert.equal(draws.length, 1);
    assert.deepEqual(draws[0].args, [mock.gl.TRIANGLES, 0, 270], "all five strokes are drawn from one range");

    renderer.renderFrame(frame(2, 110));
    assert.equal(internals.geoCache.size, 5, "the next frame starts from a trimmed cache");
  });

  it("sends only the new geometry when a stroke is added, not everything so far", () => {
    const mock = createMockGlCanvas();
    const renderer = new GlBoardRenderer(mock.canvas, { getDpr: () => 1 });
    const start = [
      ["T", 1, 0, 0, 1, 0, 0],
      ["B", 0, 0, 0, 0],
      ["P", 20, 30, 40, 255, 3, 0, 0, 2, 0, null],
    ];
    const frame = (n: number, ops: unknown[][], keep: number) => ({ t: "board" as const, view: "main", frame: n, w: 400, h: 400, keep, ops });
    renderer.renderFrame(frame(1, [...start, ["S", [0, 10, 10, 1, 50, 10], 0]], 0));

    mock.calls.length = 0;
    renderer.renderFrame(frame(2, [["S", [0, 10, 20, 1, 50, 20], 0]], 4));

    const arenaAllocations = mock.calls.filter((c) => c.fn === "bufferData" && typeof c.args[1] === "number");
    assert.equal(arenaAllocations.length, 0, "the buffer keeps its reserved capacity");
    const uploads = mock.calls.filter((c) => c.fn === "bufferSubData");
    assert.equal(uploads.length, 1);
    assert.equal(uploads[0].args[1], 12 * 4, "the upload starts after the first stroke's 6 vertices");
    assert.equal((uploads[0].args[2] as Float32Array).length, 12, "and carries only the new stroke");
  });

  it("takes the exact bytes from a renderer that kept them instead of reading a canvas back", () => {
    const mock = createMockGlCanvas();
    const renderer = new GlBoardRenderer(mock.canvas, { getDpr: () => 1 });
    const data = new Uint8Array([9, 8, 7, 1]);
    let readBack = 0;
    const holder = {
      width: 1,
      height: 1,
      getContext: () => {
        readBack += 1;
        return null;
      },
    };
    renderer.adopt({
      getOps: () => [],
      getImage: () => holder,
      images: new Map([["img", holder]]),
      rawImages: new Map([["img", { key: "img", width: 1, height: 1, data, source: holder }]]),
    } as never);

    assert.equal(renderer.getImage("img"), holder);
    assert.equal(readBack, 0);
  });
});
