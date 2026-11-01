import type { BackendClient, BoardMessage, FieldMeta, ImageMeta } from "../net.ts";
import {
  type BoardRendererOptions,
  type CanvasLike,
  clampByte,
  cloneDrawState,
  createDefaultCacheCanvas,
  defaultDrawState,
  type DrawState,
  formatRgbaColor,
  mapCompositeMode,
  mapLineCap,
  mapLineJoin,
  parseGradientStops,
  type PenState,
  findScopeStartContaining,
  type RawFieldData,
} from "./renderer.ts";
import {
  computeBounds,
  computeScaleFactor,
  hashNumbers,
  normalizeFillRule,
  tessellateFillView,
  tessellateStrokeView,
} from "./tess.ts";

export interface WebGL2ContextLike {
  readonly ARRAY_BUFFER: number;
  readonly STATIC_DRAW: number;
  readonly DYNAMIC_DRAW: number;
  readonly FLOAT: number;
  readonly TRIANGLES: number;
  readonly VERTEX_SHADER: number;
  readonly FRAGMENT_SHADER: number;
  readonly COMPILE_STATUS: number;
  readonly LINK_STATUS: number;
  readonly COLOR_BUFFER_BIT: number;
  readonly DEPTH_BUFFER_BIT: number;
  readonly STENCIL_BUFFER_BIT: number;
  readonly FRAMEBUFFER: number;
  readonly READ_FRAMEBUFFER: number;
  readonly DRAW_FRAMEBUFFER: number;
  readonly RENDERBUFFER: number;
  readonly COLOR_ATTACHMENT0: number;
  readonly DEPTH_STENCIL_ATTACHMENT: number;
  readonly RGBA8: number;
  readonly RGBA32F: number;
  readonly DEPTH24_STENCIL8: number;
  readonly MAX_SAMPLES: number;
  readonly FRAMEBUFFER_COMPLETE: number;
  readonly TEXTURE_2D: number;
  readonly TEXTURE0: number;
  readonly TEXTURE1: number;
  readonly TEXTURE_MIN_FILTER: number;
  readonly TEXTURE_MAG_FILTER: number;
  readonly TEXTURE_WRAP_S: number;
  readonly TEXTURE_WRAP_T: number;
  readonly NEAREST: number;
  readonly LINEAR: number;
  readonly CLAMP_TO_EDGE: number;
  readonly RGBA: number;
  readonly UNSIGNED_BYTE: number;
  readonly UNPACK_ALIGNMENT: number;
  readonly BLEND: number;
  readonly STENCIL_TEST: number;
  readonly DEPTH_TEST: number;
  readonly CULL_FACE: number;
  readonly ZERO: number;
  readonly ONE: number;
  readonly SRC_ALPHA: number;
  readonly ONE_MINUS_SRC_ALPHA: number;
  readonly DST_ALPHA: number;
  readonly ONE_MINUS_DST_ALPHA: number;
  readonly KEEP: number;
  readonly REPLACE: number;
  readonly INCR_WRAP: number;
  readonly DECR_WRAP: number;
  readonly INVERT: number;
  readonly ALWAYS: number;
  readonly EQUAL: number;
  readonly NOTEQUAL: number;
  readonly FRONT: number;
  readonly BACK: number;

  getParameter(pname: number): unknown;
  viewport(x: number, y: number, width: number, height: number): void;
  clearColor(r: number, g: number, b: number, a: number): void;
  clearStencil(s: number): void;
  clear(mask: number): void;
  enable(cap: number): void;
  disable(cap: number): void;
  colorMask(r: boolean, g: boolean, b: boolean, a: boolean): void;
  blendFunc(sfactor: number, dfactor: number): void;
  stencilMask(mask: number): void;
  stencilFunc(func: number, ref: number, mask: number): void;
  stencilOp(fail: number, zfail: number, zpass: number): void;
  stencilOpSeparate(face: number, fail: number, zfail: number, zpass: number): void;

  createShader(type: number): WebGLShader | null;
  shaderSource(shader: WebGLShader, source: string): void;
  compileShader(shader: WebGLShader): void;
  getShaderParameter(shader: WebGLShader, pname: number): unknown;
  getShaderInfoLog(shader: WebGLShader): string | null;
  deleteShader(shader: WebGLShader | null): void;

  createProgram(): WebGLProgram | null;
  attachShader(program: WebGLProgram, shader: WebGLShader): void;
  linkProgram(program: WebGLProgram): void;
  getProgramParameter(program: WebGLProgram, pname: number): unknown;
  getProgramInfoLog(program: WebGLProgram): string | null;
  useProgram(program: WebGLProgram | null): void;
  deleteProgram(program: WebGLProgram | null): void;
  getUniformLocation(program: WebGLProgram, name: string): WebGLUniformLocation | null;

  uniform1i(location: WebGLUniformLocation | null, x: number): void;
  uniform2i(location: WebGLUniformLocation | null, x: number, y: number): void;
  uniform1f(location: WebGLUniformLocation | null, x: number): void;
  uniform2f(location: WebGLUniformLocation | null, x: number, y: number): void;
  uniform3f(location: WebGLUniformLocation | null, x: number, y: number, z: number): void;
  uniform4f(location: WebGLUniformLocation | null, x: number, y: number, z: number, w: number): void;

  createVertexArray(): WebGLVertexArrayObject | null;
  bindVertexArray(vao: WebGLVertexArrayObject | null): void;
  deleteVertexArray(vao: WebGLVertexArrayObject | null): void;

  createBuffer(): WebGLBuffer | null;
  bindBuffer(target: number, buffer: WebGLBuffer | null): void;
  bufferData(target: number, data: ArrayBufferView | ArrayBuffer | number, usage: number): void;
  bufferSubData(target: number, dstByteOffset: number, srcData: ArrayBufferView): void;
  deleteBuffer(buffer: WebGLBuffer | null): void;

  enableVertexAttribArray(index: number): void;
  vertexAttribPointer(
    index: number,
    size: number,
    type: number,
    normalized: boolean,
    stride: number,
    offset: number,
  ): void;
  drawArrays(mode: number, first: number, count: number): void;

  createFramebuffer(): WebGLFramebuffer | null;
  bindFramebuffer(target: number, framebuffer: WebGLFramebuffer | null): void;
  framebufferRenderbuffer(
    target: number,
    attachment: number,
    renderbuffertarget: number,
    renderbuffer: WebGLRenderbuffer | null,
  ): void;
  framebufferTexture2D(
    target: number,
    attachment: number,
    textarget: number,
    texture: WebGLTexture | null,
    level: number,
  ): void;
  checkFramebufferStatus?(target: number): number;
  blitFramebuffer(
    srcX0: number,
    srcY0: number,
    srcX1: number,
    srcY1: number,
    dstX0: number,
    dstY0: number,
    dstX1: number,
    dstY1: number,
    mask: number,
    filter: number,
  ): void;
  deleteFramebuffer(framebuffer: WebGLFramebuffer | null): void;

  createRenderbuffer(): WebGLRenderbuffer | null;
  bindRenderbuffer(target: number, renderbuffer: WebGLRenderbuffer | null): void;
  renderbufferStorageMultisample(
    target: number,
    samples: number,
    internalformat: number,
    width: number,
    height: number,
  ): void;
  deleteRenderbuffer(renderbuffer: WebGLRenderbuffer | null): void;

  createTexture(): WebGLTexture | null;
  bindTexture(target: number, texture: WebGLTexture | null): void;
  activeTexture(texture: number): void;
  pixelStorei(pname: number, param: number): void;
  texParameteri(target: number, pname: number, param: number): void;
  texImage2D(
    target: number,
    level: number,
    internalformat: number,
    width: number,
    height: number,
    border: number,
    format: number,
    type: number,
    pixels: ArrayBufferView | null,
  ): void;
  deleteTexture(texture: WebGLTexture | null): void;
}

export interface GlCanvasLike {
  width: number;
  height: number;
  style?: { width?: string; height?: string };
  getContext(contextId: "webgl2", options?: WebGLContextAttributes): WebGL2ContextLike | null;
  addEventListener?(type: string, listener: (ev: Event) => void): void;
  removeEventListener?(type: string, listener: (ev: Event) => void): void;
}

export interface AdoptableRenderer {
  getOps(): ReadonlyArray<unknown[]>;
  getImage(key: string): unknown;
  getField?(key: string): RawFieldData | undefined;
}

interface RawImageEntry {
  key: string;
  width: number;
  height: number;
  data: Uint8Array | null;
  source: unknown;
}

interface NumericStop {
  offset: number;
  r: number;
  g: number;
  b: number;
  a: number;
}

interface CachedGeometry {
  key: string;
  firstVertex: number;
  vertexCount: number;
  coverFirstVertex: number;
  builtScale: number;
}

interface FastStrokeRef {
  width: number;
  cap: CanvasLineCap;
  join: CanvasLineJoin;
  miter: number;
  cosmetic: boolean;
  dash: ReadonlyArray<number> | null;
  m11: number;
  m12: number;
  m21: number;
  m22: number;
  maxEdge: number;
  entry: CachedGeometry;
}

const SHAPE_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
uniform vec2 u_resolution;
uniform float u_dpr;
uniform vec3 u_transform0;
uniform vec3 u_transform1;
out vec2 v_localPos;
void main() {
  v_localPos = a_pos;
  float px = (u_transform0.x * a_pos.x + u_transform0.y * a_pos.y + u_transform0.z) * u_dpr;
  float py = (u_transform1.x * a_pos.x + u_transform1.y * a_pos.y + u_transform1.z) * u_dpr;
  gl_Position = vec4((px / u_resolution.x) * 2.0 - 1.0, 1.0 - (py / u_resolution.y) * 2.0, 0.0, 1.0);
}
`;

const SHAPE_FS = `#version 300 es
precision highp float;
in vec2 v_localPos;
uniform int u_paintKind;
uniform vec4 u_color;
uniform float u_opacity;
uniform vec4 u_gradCoords0;
uniform vec2 u_gradCoords1;
uniform sampler2D u_gradTex;
out vec4 outColor;
void main() {
  if (u_paintKind == 0) {
    outColor = u_color;
    return;
  }
  float t = 0.0;
  if (u_paintKind == 1) {
    vec2 p1 = u_gradCoords0.xy;
    vec2 d = u_gradCoords0.zw - p1;
    float lenSq = dot(d, d);
    t = lenSq > 1e-12 ? dot(v_localPos - p1, d) / lenSq : 0.0;
  } else {
    vec2 f = u_gradCoords0.xy;
    float r0 = u_gradCoords0.z;
    vec2 c = vec2(u_gradCoords0.w, u_gradCoords1.x);
    float r1 = u_gradCoords1.y;
    vec2 p = v_localPos - f;
    vec2 d = c - f;
    float dr = r1 - r0;
    float A = dot(d, d) - dr * dr;
    float B = dot(p, d) + r0 * dr;
    float C0 = dot(p, p) - r0 * r0;
    if (abs(A) > 1e-7) {
      float disc = B * B - A * C0;
      if (disc < 0.0) {
        outColor = vec4(0.0);
        return;
      }
      float s = sqrt(max(0.0, disc));
      float t1 = (B + s) / A;
      float t2 = (B - s) / A;
      float tMax = max(t1, t2);
      float tMin = min(t1, t2);
      t = (r0 + tMax * dr >= 0.0) ? tMax : tMin;
      if (r0 + t * dr < 0.0) {
        outColor = vec4(0.0);
        return;
      }
    } else {
      t = abs(B) > 1e-7 ? C0 / (2.0 * B) : 0.0;
    }
  }
  vec4 c = texture(u_gradTex, vec2(clamp(t, 0.0, 1.0), 0.5));
  float a = c.a * u_opacity;
  outColor = vec4(c.rgb * a, a);
}
`;

export const FIELD_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
uniform vec2 u_resolution;
uniform float u_dpr;
uniform vec3 u_transform0;
uniform vec3 u_transform1;
uniform sampler2D u_fieldTex;
uniform vec4 u_fieldRect;
uniform highp ivec2 u_fieldGrid;
out vec2 v_localPos;
vec2 sampleFieldDisp(vec2 p) {
  float u = (u_fieldGrid.x > 1 && u_fieldRect.z > 0.0) ? (p.x - u_fieldRect.x) / u_fieldRect.z * float(u_fieldGrid.x - 1) : 0.0;
  float v = (u_fieldGrid.y > 1 && u_fieldRect.w > 0.0) ? (p.y - u_fieldRect.y) / u_fieldRect.w * float(u_fieldGrid.y - 1) : 0.0;
  float uc = clamp(u, 0.0, float(u_fieldGrid.x - 1));
  float vc = clamp(v, 0.0, float(u_fieldGrid.y - 1));
  int i0 = clamp(int(floor(uc)), 0, max(0, u_fieldGrid.x - 2));
  int j0 = clamp(int(floor(vc)), 0, max(0, u_fieldGrid.y - 2));
  int i1 = min(i0 + 1, u_fieldGrid.x - 1);
  int j1 = min(j0 + 1, u_fieldGrid.y - 1);
  float fu = clamp(uc - float(i0), 0.0, 1.0);
  float fv = clamp(vc - float(j0), 0.0, 1.0);
  vec2 d00 = texelFetch(u_fieldTex, ivec2(i0, j0), 0).xy;
  vec2 d10 = texelFetch(u_fieldTex, ivec2(i1, j0), 0).xy;
  vec2 d01 = texelFetch(u_fieldTex, ivec2(i0, j1), 0).xy;
  vec2 d11 = texelFetch(u_fieldTex, ivec2(i1, j1), 0).xy;
  return mix(mix(d00, d10, fu), mix(d01, d11, fu), fv);
}
void main() {
  v_localPos = a_pos;
  vec2 pos = a_pos + sampleFieldDisp(a_pos);
  float px = (u_transform0.x * pos.x + u_transform0.y * pos.y + u_transform0.z) * u_dpr;
  float py = (u_transform1.x * pos.x + u_transform1.y * pos.y + u_transform1.z) * u_dpr;
  gl_Position = vec4((px / u_resolution.x) * 2.0 - 1.0, 1.0 - (py / u_resolution.y) * 2.0, 0.0, 1.0);
}
`;

export const FIELD_FS = `${SHAPE_FS.replace(
  "void main() {",
  `uniform sampler2D u_fieldTex;
uniform vec4 u_fieldRect;
uniform highp ivec2 u_fieldGrid;
uniform vec2 u_tagRange;
uniform vec4 u_tint;
void computeShapeColor() {`,
)}
void main() {
  if (v_localPos.x < u_fieldRect.x || v_localPos.x > u_fieldRect.x + u_fieldRect.z ||
      v_localPos.y < u_fieldRect.y || v_localPos.y > u_fieldRect.y + u_fieldRect.w) {
    discard;
  }
  float u = (u_fieldGrid.x > 1 && u_fieldRect.z > 0.0) ? (v_localPos.x - u_fieldRect.x) / u_fieldRect.z * float(u_fieldGrid.x - 1) : 0.0;
  float v = (u_fieldGrid.y > 1 && u_fieldRect.w > 0.0) ? (v_localPos.y - u_fieldRect.y) / u_fieldRect.w * float(u_fieldGrid.y - 1) : 0.0;
  int ni = clamp(int(round(u)), 0, u_fieldGrid.x - 1);
  int nj = clamp(int(round(v)), 0, u_fieldGrid.y - 1);
  float tag = texelFetch(u_fieldTex, ivec2(ni, nj), 0).z;
  if (tag < u_tagRange.x || tag > u_tagRange.y) {
    discard;
  }
  computeShapeColor();
  if (u_tint.a > 0.0) {
    outColor = vec4(mix(outColor.rgb, u_tint.rgb * outColor.a, u_tint.a), outColor.a);
  }
}
`;

const IMAGE_VS = `#version 300 es
precision highp float;
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
uniform vec2 u_resolution;
uniform float u_dpr;
uniform vec3 u_transform0;
uniform vec3 u_transform1;
out vec2 v_uv;
void main() {
  v_uv = a_uv;
  float px = (u_transform0.x * a_pos.x + u_transform0.y * a_pos.y + u_transform0.z) * u_dpr;
  float py = (u_transform1.x * a_pos.x + u_transform1.y * a_pos.y + u_transform1.z) * u_dpr;
  gl_Position = vec4((px / u_resolution.x) * 2.0 - 1.0, 1.0 - (py / u_resolution.y) * 2.0, 0.0, 1.0);
}
`;

const IMAGE_FS = `#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_opacity;
uniform int u_passthrough;
out vec4 outColor;
void main() {
  vec4 c = texture(u_tex, v_uv);
  if (u_passthrough != 0) {
    outColor = c;
    return;
  }
  float a = c.a * u_opacity;
  outColor = vec4(c.rgb * a, a);
}
`;

function parseCssColorString(str: string): [number, number, number, number] {
  const m = str.match(/rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)(?:\s*,\s*([\d.]+))?\s*\)/i);
  if (!m) return [0, 0, 0, 0];
  const r = clampByte(Number(m[1]));
  const g = clampByte(Number(m[2]));
  const b = clampByte(Number(m[3]));
  const a = m[4] !== undefined ? clampByte(Number(m[4]) * 255) : 255;
  return [r, g, b, a];
}

function parseNumericStops(rawStops: unknown): NumericStop[] {
  if (!Array.isArray(rawStops)) return [];
  const out: NumericStop[] = [];
  for (const st of rawStops) {
    if (!Array.isArray(st)) continue;
    const offset = Math.max(0, Math.min(1, Number(st[0] ?? 0)));
    if (typeof st[1] === "string") {
      const [r, g, b, a] = parseCssColorString(st[1]);
      out.push({ offset, r, g, b, a });
    } else {
      const r = clampByte(st[1]);
      const g = clampByte(st[2]);
      const b = clampByte(st[3]);
      const a = st.length > 4 ? clampByte(st[4]) : 255;
      out.push({ offset, r, g, b, a });
    }
  }
  out.sort((a, b) => a.offset - b.offset);
  return out;
}

function bakeGradientLut(stops: ReadonlyArray<NumericStop>): Uint8Array {
  const lut = new Uint8Array(256 * 4);
  if (stops.length === 0) return lut;
  if (stops.length === 1) {
    const s = stops[0];
    for (let i = 0; i < 256; i++) {
      const idx = i * 4;
      lut[idx] = s.r;
      lut[idx + 1] = s.g;
      lut[idx + 2] = s.b;
      lut[idx + 3] = s.a;
    }
    return lut;
  }

  for (let i = 0; i < 256; i++) {
    const t = i / 255;
    const idx = i * 4;
    if (t <= stops[0].offset) {
      lut[idx] = stops[0].r;
      lut[idx + 1] = stops[0].g;
      lut[idx + 2] = stops[0].b;
      lut[idx + 3] = stops[0].a;
      continue;
    }
    const last = stops[stops.length - 1];
    if (t >= last.offset) {
      lut[idx] = last.r;
      lut[idx + 1] = last.g;
      lut[idx + 2] = last.b;
      lut[idx + 3] = last.a;
      continue;
    }
    let k = 0;
    while (k < stops.length - 2 && t > stops[k + 1].offset) {
      k++;
    }
    const s0 = stops[k];
    const s1 = stops[k + 1];
    const span = s1.offset - s0.offset;
    const u = span > 1e-7 ? (t - s0.offset) / span : 0;
    lut[idx] = Math.round(s0.r + (s1.r - s0.r) * u);
    lut[idx + 1] = Math.round(s0.g + (s1.g - s0.g) * u);
    lut[idx + 2] = Math.round(s0.b + (s1.b - s0.b) * u);
    lut[idx + 3] = Math.round(s0.a + (s1.a - s0.a) * u);
  }
  return lut;
}

export class GlBoardRenderer {
  private canvas: GlCanvasLike;
  private gl: WebGL2ContextLike;
  private view: string;
  private client?: BackendClient;
  private getDpr: () => number;
  private createCacheCanvas: (width: number, height: number) => CanvasLike;
  private onAck?: (view: string, frame: number) => void;

  private fullOps: unknown[][] = [];
  private cachedCount = 0;
  private cachedState: DrawState = defaultDrawState();
  private logicalW = 0;
  private logicalH = 0;
  private currentDpr = 1;
  private pw = 0;
  private ph = 0;
  private samples = 1;
  private lastDrawCalls = 0;

  // Keep the same image map format as BoardRenderer so both renderers can hand off via adopt().
  private images = new Map<string, unknown>();
  private rawImages = new Map<string, RawImageEntry>();
  private imageTextures = new Map<string, WebGLTexture>();
  private gradTextures = new Map<string, WebGLTexture>();
  private rawFields = new Map<string, RawFieldData>();
  private fieldTextures = new Map<string, WebGLTexture>();

  // All tessellated triangles live in one large vertex buffer so adjacent same-state
  // strokes occupy contiguous vertex ranges and draw in a single gl.drawArrays call.
  private arenaData = new Float32Array(262144);
  private arenaFloatCount = 0;
  private arenaUploadedFloats = 0;
  private arenaGpuCapFloats = 0;
  private arenaVbo: WebGLBuffer | null = null;
  private readonly maxArenaFloats = 128 * 1024 * 1024;

  private geoCache = new Map<string, CachedGeometry>();
  private strokeRefCache = new WeakMap<object, FastStrokeRef>();
  private readonly maxGeoCacheEntries = 256 * 1024;

  private shapeProg: WebGLProgram | null = null;
  private shapeLocs: {
    resolution: WebGLUniformLocation | null;
    dpr: WebGLUniformLocation | null;
    transform0: WebGLUniformLocation | null;
    transform1: WebGLUniformLocation | null;
    paintKind: WebGLUniformLocation | null;
    color: WebGLUniformLocation | null;
    opacity: WebGLUniformLocation | null;
    gradCoords0: WebGLUniformLocation | null;
    gradCoords1: WebGLUniformLocation | null;
    gradTex: WebGLUniformLocation | null;
  } | null = null;

  private imageProg: WebGLProgram | null = null;
  private imageLocs: {
    resolution: WebGLUniformLocation | null;
    dpr: WebGLUniformLocation | null;
    transform0: WebGLUniformLocation | null;
    transform1: WebGLUniformLocation | null;
    tex: WebGLUniformLocation | null;
    opacity: WebGLUniformLocation | null;
    passthrough: WebGLUniformLocation | null;
  } | null = null;

  private shapeVao: WebGLVertexArrayObject | null = null;
  private imageVao: WebGLVertexArrayObject | null = null;
  private imageQuadVbo: WebGLBuffer | null = null;

  private prefixFbo: WebGLFramebuffer | null = null;
  private prefixColorRb: WebGLRenderbuffer | null = null;
  private prefixDepthStencilRb: WebGLRenderbuffer | null = null;
  private prefixResolveFbo: WebGLFramebuffer | null = null;
  private prefixTexture: WebGLTexture | null = null;
  private prefixResolved = false;

  private workFbo: WebGLFramebuffer | null = null;
  private workColorRb: WebGLRenderbuffer | null = null;
  private workDepthStencilRb: WebGLRenderbuffer | null = null;

  private contextLost = false;
  private onContextLostBound: (ev: Event) => void;
  private onContextRestoredBound: (ev: Event) => void;

  private unsubBoard?: () => void;
  private unsubImage?: () => void;
  private unsubField?: () => void;

  private fieldProg: WebGLProgram | null = null;
  private fieldLocs: {
    resolution: WebGLUniformLocation | null;
    dpr: WebGLUniformLocation | null;
    transform0: WebGLUniformLocation | null;
    transform1: WebGLUniformLocation | null;
    paintKind: WebGLUniformLocation | null;
    color: WebGLUniformLocation | null;
    opacity: WebGLUniformLocation | null;
    gradCoords0: WebGLUniformLocation | null;
    gradCoords1: WebGLUniformLocation | null;
    gradTex: WebGLUniformLocation | null;
    fieldTex: WebGLUniformLocation | null;
    fieldRect: WebGLUniformLocation | null;
    fieldGrid: WebGLUniformLocation | null;
    tagRange: WebGLUniformLocation | null;
    tint: WebGLUniformLocation | null;
  } | null = null;

  private activeField: {
    tex: WebGLTexture;
    w: number;
    h: number;
    rect: [number, number, number, number];
    maxEdge: number;
    tagMin: number;
    tagMax: number;
    passOpacity: number;
    tint: [number, number, number, number];
  } | null = null;

  static isSupported(): boolean {
    try {
      if (typeof OffscreenCanvas !== "undefined") {
        const c = new OffscreenCanvas(1, 1);
        const gl = c.getContext("webgl2");
        if (gl) return true;
      }
      if (typeof document !== "undefined" && typeof document.createElement === "function") {
        const c = document.createElement("canvas");
        const gl = c.getContext("webgl2");
        if (gl) return true;
      }
    } catch {
      return false;
    }
    return false;
  }

  constructor(canvas: GlCanvasLike, options: BoardRendererOptions = {}) {
    const gl = canvas.getContext("webgl2", {
      antialias: false,
      stencil: false,
      alpha: true,
      premultipliedAlpha: true,
      preserveDrawingBuffer: true,
    });
    if (!gl) {
      throw new Error("无法获取 WebGL2 上下文");
    }

    this.canvas = canvas;
    this.gl = gl;
    this.view = options.view ?? "main";
    this.client = options.client;
    this.getDpr =
      options.getDpr ??
      (() => (typeof window !== "undefined" && window.devicePixelRatio ? window.devicePixelRatio : 1));
    this.createCacheCanvas = options.createCacheCanvas ?? createDefaultCacheCanvas;
    this.onAck = options.onAck;

    this.onContextLostBound = (ev: Event) => {
      ev.preventDefault?.();
      this.contextLost = true;
      this.invalidateGpuHandles();
    };

    this.onContextRestoredBound = () => {
      this.contextLost = false;
      this.initGlResources();
      this.cachedCount = 0;
      this.cachedState = defaultDrawState();
      if (this.logicalW > 0 && this.logicalH > 0) {
        this.setupFramebuffers(this.pw, this.ph);
        this.drawFrame({
          t: "board",
          view: this.view,
          frame: 0,
          w: this.logicalW,
          h: this.logicalH,
          keep: 0,
          ops: this.fullOps,
        });
      }
    };

    this.canvas.addEventListener?.("webglcontextlost", this.onContextLostBound);
    this.canvas.addEventListener?.("webglcontextrestored", this.onContextRestoredBound);

    this.initGlResources();

    if (this.client) {
      this.unsubImage = this.client.onImage?.((meta, data) => {
        if (!meta.key.startsWith("cur_")) {
          this.putImage(meta, data);
        }
      });
      this.unsubField = this.client.onField?.((meta, data) => {
        this.putField(meta, data);
      });
      this.unsubBoard = this.client.onBoard?.(this.view, (msg) => {
        this.renderFrame(msg);
      });
    }
  }

  getOps(): ReadonlyArray<unknown[]> {
    return this.fullOps;
  }

  getImage(key: string): unknown {
    return this.images.get(key);
  }

  getField(key: string): RawFieldData | undefined {
    return this.rawFields.get(key);
  }

  putField(meta: FieldMeta, data: Float32Array): void {
    const w = Math.max(1, Math.round(meta.w));
    const h = Math.max(1, Math.round(meta.h));
    const version = Math.round(meta.version ?? 0);
    const copy = new Float32Array(w * h * 4);
    copy.set(data.subarray(0, Math.min(data.length, copy.length)));

    const prevTex = this.fieldTextures.get(meta.key);
    if (prevTex && !this.contextLost) {
      this.gl.deleteTexture(prevTex);
    }
    this.fieldTextures.delete(meta.key);

    const entry: RawFieldData = {
      key: meta.key,
      w,
      h,
      version,
      data: copy,
    };
    this.rawFields.set(meta.key, entry);

    if (!this.contextLost) {
      this.createFieldTexture(entry);
    }
  }

  private createFieldTexture(raw: RawFieldData): WebGLTexture | null {
    const gl = this.gl;
    const tex = gl.createTexture();
    if (!tex) return null;
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, raw.w, raw.h, 0, gl.RGBA, gl.FLOAT, raw.data);
    this.fieldTextures.set(raw.key, tex);
    return tex;
  }

  private getFieldTexture(key: string, version: number): { tex: WebGLTexture; w: number; h: number } | null {
    const raw = this.rawFields.get(key);
    if (!raw || raw.version !== version) return null;
    let tex: WebGLTexture | null = this.fieldTextures.get(key) ?? null;
    if (!tex) {
      tex = this.createFieldTexture(raw);
    }
    if (!tex) return null;
    return { tex, w: raw.w, h: raw.h };
  }

  getLastDrawCalls(): number {
    return this.lastDrawCalls;
  }

  putImage(meta: ImageMeta, data: Uint8Array): void {
    const w = Math.max(1, Math.round(meta.w));
    const h = Math.max(1, Math.round(meta.h));
    const copy = new Uint8Array(w * h * 4);
    copy.set(data.subarray(0, Math.min(data.length, copy.length)));

    const prevTex = this.imageTextures.get(meta.key);
    if (prevTex && !this.contextLost) {
      this.gl.deleteTexture(prevTex);
    }
    this.imageTextures.delete(meta.key);

    let holderObj: unknown = { key: meta.key, width: w, height: h, data: copy };
    if (
      typeof ImageData !== "undefined" &&
      (typeof OffscreenCanvas !== "undefined" || typeof document !== "undefined")
    ) {
      const holder = this.createCacheCanvas(w, h);
      holder.width = w;
      holder.height = h;
      const hCtx = holder.getContext("2d");
      if (hCtx && typeof hCtx.putImageData === "function") {
        const clamped = new Uint8ClampedArray(copy.buffer, copy.byteOffset, copy.byteLength);
        const imgData = new ImageData(clamped, w, h);
        hCtx.putImageData(imgData, 0, 0);
        holderObj = holder;
      }
    }

    this.images.set(meta.key, holderObj);
    this.rawImages.set(meta.key, {
      key: meta.key,
      width: w,
      height: h,
      data: copy,
      source: holderObj,
    });
  }

  adopt(prev: AdoptableRenderer): void {
    const prevOps = prev.getOps();
    this.fullOps = prevOps.map((op) => (Array.isArray(op) ? [...op] : []));

    const prevAny = prev as unknown as Record<string, unknown>;
    if (prevAny.rawImages instanceof Map) {
      for (const [k, v] of prevAny.rawImages.entries()) {
        this.importImageValue(String(k), v);
      }
    }
    if (prevAny.images instanceof Map) {
      for (const [k, v] of prevAny.images.entries()) {
        // The raw bytes above are exact; reading a 2D canvas back is not.
        if (this.rawImages.has(String(k))) {
          this.images.set(String(k), v);
        } else {
          this.importImageValue(String(k), v);
        }
      }
    }
    for (const op of this.fullOps) {
      if (Array.isArray(op) && op[0] === "I") {
        const key = String(op[1] ?? "");
        if (key && !this.rawImages.has(key)) {
          const img = prev.getImage(key);
          if (img !== undefined) {
            this.importImageValue(key, img);
          }
        }
      }
    }

    if (prevAny.rawFields instanceof Map) {
      for (const [k, v] of prevAny.rawFields.entries()) {
        const fObj = v as RawFieldData;
        if (fObj && fObj.data instanceof Float32Array && typeof fObj.w === "number" && typeof fObj.h === "number") {
          this.putField(
            {
              t: "field",
              key: String(k),
              w: fObj.w,
              h: fObj.h,
              version: Number(fObj.version ?? 0),
              bytes: fObj.data.byteLength,
            },
            fObj.data,
          );
        }
      }
    }
    for (const op of this.fullOps) {
      if (Array.isArray(op) && op[0] === "F") {
        const key = String(op[1] ?? "");
        if (key && !this.rawFields.has(key) && typeof prev.getField === "function") {
          const fObj = prev.getField(key);
          if (fObj && fObj.data instanceof Float32Array) {
            this.putField(
              {
                t: "field",
                key,
                w: fObj.w,
                h: fObj.h,
                version: Number(fObj.version ?? 0),
                bytes: fObj.data.byteLength,
              },
              fObj.data,
            );
          }
        }
      }
    }

    const dpr = Math.max(0.1, Number(this.getDpr()) || 1);
    const prevW = typeof prevAny.logicalW === "number" && prevAny.logicalW > 0 ? prevAny.logicalW : 0;
    const prevH = typeof prevAny.logicalH === "number" && prevAny.logicalH > 0 ? prevAny.logicalH : 0;
    const w = prevW || this.logicalW || Math.max(1, Math.round((this.canvas.width || 1) / dpr));
    const h = prevH || this.logicalH || Math.max(1, Math.round((this.canvas.height || 1) / dpr));

    this.cachedCount = 0;
    this.cachedState = defaultDrawState();
    this.prefixResolved = false;
    if (w > 0 && h > 0) {
      this.drawFrame({
        t: "board",
        view: this.view,
        frame: 0,
        w,
        h,
        keep: 0,
        ops: this.fullOps,
      });
    }
  }

  private importImageValue(key: string, val: unknown): void {
    if (!val) return;
    this.images.set(key, val);
    const vObj = val as Record<string, unknown>;
    if (vObj.data instanceof Uint8Array && typeof vObj.width === "number" && typeof vObj.height === "number") {
      this.rawImages.set(key, {
        key,
        width: vObj.width,
        height: vObj.height,
        data: vObj.data,
        source: (vObj.source as unknown) ?? val,
      });
      return;
    }
    if (typeof vObj.width === "number" && typeof vObj.height === "number") {
      let extracted: Uint8Array | null = null;
      const getContextFn = vObj.getContext as ((id: string) => unknown) | undefined;
      if (typeof getContextFn === "function") {
        const ctx2d = getContextFn.call(val, "2d") as {
          getImageData?: (x: number, y: number, w: number, h: number) => { data: Uint8ClampedArray };
        } | null;
        if (ctx2d && typeof ctx2d.getImageData === "function") {
          const imgData = ctx2d.getImageData(0, 0, vObj.width, vObj.height);
          extracted = new Uint8Array(imgData.data.buffer, imgData.data.byteOffset, imgData.data.byteLength);
        }
      }
      this.rawImages.set(key, {
        key,
        width: vObj.width,
        height: vObj.height,
        data: extracted,
        source: val,
      });
    }
  }

  renderFrame(msg: BoardMessage): void {
    try {
      this.drawFrame(msg);
    } finally {
      // Without this ack the backend stops sending frames for this board.
      this.client?.sendAck(msg.view, msg.frame);
      this.onAck?.(msg.view, msg.frame);
    }
  }

  destroy(): void {
    this.unsubBoard?.();
    this.unsubImage?.();
    this.unsubBoard = undefined;
    this.unsubField?.();
    this.unsubImage = undefined;
    this.unsubField = undefined;
    this.canvas.removeEventListener?.("webglcontextlost", this.onContextLostBound);
    this.canvas.removeEventListener?.("webglcontextrestored", this.onContextRestoredBound);

    if (!this.contextLost) {
      const gl = this.gl;
      if (this.arenaVbo) gl.deleteBuffer(this.arenaVbo);
      for (const tex of this.imageTextures.values()) {
        gl.deleteTexture(tex);
      }
      for (const tex of this.gradTextures.values()) {
        gl.deleteTexture(tex);
      }
      if (this.imageQuadVbo) gl.deleteBuffer(this.imageQuadVbo);
      if (this.shapeVao) gl.deleteVertexArray(this.shapeVao);
      if (this.imageVao) gl.deleteVertexArray(this.imageVao);
      if (this.shapeProg) gl.deleteProgram(this.shapeProg);
      for (const tex of this.fieldTextures.values()) {
        gl.deleteTexture(tex);
      }
      if (this.imageProg) gl.deleteProgram(this.imageProg);
      if (this.fieldProg) gl.deleteProgram(this.fieldProg);
      this.deleteFramebuffers();
    }

    this.geoCache.clear();
    this.strokeRefCache = new WeakMap();
    this.arenaFloatCount = 0;
    this.arenaUploadedFloats = 0;
    this.arenaGpuCapFloats = 0;
    this.arenaVbo = null;
    this.imageTextures.clear();
    this.gradTextures.clear();
    this.images.clear();
    this.rawImages.clear();
    this.fieldTextures.clear();
    this.rawFields.clear();
    this.fullOps = [];
  }

  private invalidateGpuHandles(): void {
    this.arenaUploadedFloats = 0;
    this.arenaGpuCapFloats = 0;
    this.arenaVbo = null;
    this.imageTextures.clear();
    this.gradTextures.clear();
    this.shapeProg = null;
    this.fieldTextures.clear();
    this.imageProg = null;
    this.fieldProg = null;
    this.fieldLocs = null;
    this.shapeVao = null;
    this.imageVao = null;
    this.imageQuadVbo = null;
    this.prefixFbo = null;
    this.prefixColorRb = null;
    this.prefixDepthStencilRb = null;
    this.prefixResolveFbo = null;
    this.prefixTexture = null;
    this.prefixResolved = false;
    this.workFbo = null;
    this.workColorRb = null;
    this.workDepthStencilRb = null;
  }

  private compileProgram(vsSrc: string, fsSrc: string): WebGLProgram {
    const gl = this.gl;
    const vs = gl.createShader(gl.VERTEX_SHADER)!;
    gl.shaderSource(vs, vsSrc);
    gl.compileShader(vs);
    if (!gl.getShaderParameter(vs, gl.COMPILE_STATUS)) {
      const log = gl.getShaderInfoLog(vs);
      gl.deleteShader(vs);
      throw new Error(`顶点着色器编译失败: ${log}`);
    }

    const fs = gl.createShader(gl.FRAGMENT_SHADER)!;
    gl.shaderSource(fs, fsSrc);
    gl.compileShader(fs);
    if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS)) {
      const log = gl.getShaderInfoLog(fs);
      gl.deleteShader(vs);
      gl.deleteShader(fs);
      throw new Error(`片元着色器编译失败: ${log}`);
    }

    const prog = gl.createProgram()!;
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    gl.deleteShader(vs);
    gl.deleteShader(fs);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
      const log = gl.getProgramInfoLog(prog);
      gl.deleteProgram(prog);
      throw new Error(`着色器链接失败: ${log}`);
    }
    return prog;
  }

  private initGlResources(): void {
    const gl = this.gl;
    const maxSamples = Number(gl.getParameter(gl.MAX_SAMPLES)) || 1;
    this.samples = Math.max(1, Math.min(8, maxSamples));

    this.shapeProg = this.compileProgram(SHAPE_VS, SHAPE_FS);
    this.shapeLocs = {
      resolution: gl.getUniformLocation(this.shapeProg, "u_resolution"),
      dpr: gl.getUniformLocation(this.shapeProg, "u_dpr"),
      transform0: gl.getUniformLocation(this.shapeProg, "u_transform0"),
      transform1: gl.getUniformLocation(this.shapeProg, "u_transform1"),
      paintKind: gl.getUniformLocation(this.shapeProg, "u_paintKind"),
      color: gl.getUniformLocation(this.shapeProg, "u_color"),
      opacity: gl.getUniformLocation(this.shapeProg, "u_opacity"),
      gradCoords0: gl.getUniformLocation(this.shapeProg, "u_gradCoords0"),
      gradCoords1: gl.getUniformLocation(this.shapeProg, "u_gradCoords1"),
      gradTex: gl.getUniformLocation(this.shapeProg, "u_gradTex"),
    };

    this.fieldProg = this.compileProgram(FIELD_VS, FIELD_FS);
    this.fieldLocs = {
      resolution: gl.getUniformLocation(this.fieldProg, "u_resolution"),
      dpr: gl.getUniformLocation(this.fieldProg, "u_dpr"),
      transform0: gl.getUniformLocation(this.fieldProg, "u_transform0"),
      transform1: gl.getUniformLocation(this.fieldProg, "u_transform1"),
      paintKind: gl.getUniformLocation(this.fieldProg, "u_paintKind"),
      color: gl.getUniformLocation(this.fieldProg, "u_color"),
      opacity: gl.getUniformLocation(this.fieldProg, "u_opacity"),
      gradCoords0: gl.getUniformLocation(this.fieldProg, "u_gradCoords0"),
      gradCoords1: gl.getUniformLocation(this.fieldProg, "u_gradCoords1"),
      gradTex: gl.getUniformLocation(this.fieldProg, "u_gradTex"),
      fieldTex: gl.getUniformLocation(this.fieldProg, "u_fieldTex"),
      fieldRect: gl.getUniformLocation(this.fieldProg, "u_fieldRect"),
      fieldGrid: gl.getUniformLocation(this.fieldProg, "u_fieldGrid"),
      tagRange: gl.getUniformLocation(this.fieldProg, "u_tagRange"),
      tint: gl.getUniformLocation(this.fieldProg, "u_tint"),
    };

    for (const raw of this.rawFields.values()) {
      if (!this.fieldTextures.has(raw.key)) {
        this.createFieldTexture(raw);
      }
    }

    this.imageProg = this.compileProgram(IMAGE_VS, IMAGE_FS);
    this.imageLocs = {
      resolution: gl.getUniformLocation(this.imageProg, "u_resolution"),
      dpr: gl.getUniformLocation(this.imageProg, "u_dpr"),
      transform0: gl.getUniformLocation(this.imageProg, "u_transform0"),
      transform1: gl.getUniformLocation(this.imageProg, "u_transform1"),
      tex: gl.getUniformLocation(this.imageProg, "u_tex"),
      opacity: gl.getUniformLocation(this.imageProg, "u_opacity"),
      passthrough: gl.getUniformLocation(this.imageProg, "u_passthrough"),
    };

    this.shapeVao = gl.createVertexArray();
    this.arenaVbo = gl.createBuffer();
    this.arenaUploadedFloats = 0;
    this.arenaGpuCapFloats = 0;
    gl.bindVertexArray(this.shapeVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.arenaVbo);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);

    this.imageVao = gl.createVertexArray();
    this.imageQuadVbo = gl.createBuffer();
    gl.bindVertexArray(this.imageVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.imageQuadVbo);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 16, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 16, 8);
    gl.bindVertexArray(null);

    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);
    gl.enable(gl.BLEND);
  }

  // Only between frames: entries handed out earlier in a frame, and a batch being
  // gathered, still point into the arena, so a reset mid-frame draws wrong geometry.
  private trimGeometryCache(): void {
    if (this.arenaFloatCount <= this.maxArenaFloats && this.geoCache.size <= this.maxGeoCacheEntries) {
      return;
    }
    this.geoCache.clear();
    this.strokeRefCache = new WeakMap();
    this.arenaFloatCount = 0;
    this.arenaUploadedFloats = 0;
  }

  private appendArenaFloats(src: Float32Array): number {
    const len = src.length;
    if (len === 0) return 0;
    if (this.arenaFloatCount + len > this.arenaData.length) {
      let nextCap = Math.max(this.arenaData.length * 4, 8 * 1024 * 1024);
      while (nextCap < this.arenaFloatCount + len) {
        nextCap *= 4;
      }
      const grown = new Float32Array(nextCap);
      grown.set(this.arenaData.subarray(0, this.arenaFloatCount));
      this.arenaData = grown;
      // Re-upload full arena when the backing buffer reallocates.
      this.arenaUploadedFloats = 0;
    }
    const firstVertex = this.arenaFloatCount >> 1;
    this.arenaData.set(src, this.arenaFloatCount);
    this.arenaFloatCount += len;
    return firstVertex;
  }

  private ensureEntryCoverQuad(entry: CachedGeometry): number {
    if (entry.coverFirstVertex >= 0) {
      return entry.coverFirstVertex;
    }
    const startFloat = entry.firstVertex * 2;
    const endFloat = startFloat + entry.vertexCount * 2;
    const bounds = computeBounds(this.arenaData.subarray(startFloat, endFloat));
    const pad = 2;
    const minX = bounds[0] - pad;
    const minY = bounds[1] - pad;
    const maxX = bounds[2] + pad;
    const maxY = bounds[3] + pad;
    const quad = new Float32Array([
      minX, minY,
      maxX, minY,
      maxX, maxY,
      minX, minY,
      maxX, maxY,
      minX, maxY,
    ]);
    entry.coverFirstVertex = this.appendArenaFloats(quad);
    return entry.coverFirstVertex;
  }

  private syncArenaVbo(): void {
    if (this.arenaUploadedFloats >= this.arenaFloatCount) {
      return;
    }
    const gl = this.gl;
    if (!this.arenaVbo) {
      this.arenaVbo = gl.createBuffer();
      this.arenaUploadedFloats = 0;
      this.arenaGpuCapFloats = 0;
    }
    gl.bindBuffer(gl.ARRAY_BUFFER, this.arenaVbo);
    // Reserve the arena's whole capacity. A buffer sized to the data alone is
    // outgrown by the next append, and every new stroke re-sends all geometry.
    if (this.arenaGpuCapFloats < this.arenaData.length) {
      gl.bufferData(gl.ARRAY_BUFFER, this.arenaData.length * 4, gl.DYNAMIC_DRAW);
      this.arenaGpuCapFloats = this.arenaData.length;
      this.arenaUploadedFloats = 0;
    }
    const slice = this.arenaData.subarray(this.arenaUploadedFloats, this.arenaFloatCount);
    gl.bufferSubData(gl.ARRAY_BUFFER, this.arenaUploadedFloats * 4, slice);
    this.arenaUploadedFloats = this.arenaFloatCount;
  }

  private emitDrawArrays(mode: number, first: number, count: number): void {
    if (count <= 0) return;
    this.lastDrawCalls++;
    this.gl.drawArrays(mode, first, count);
  }

  private deleteFramebuffers(): void {
    const gl = this.gl;
    if (this.prefixFbo) gl.deleteFramebuffer(this.prefixFbo);
    if (this.prefixColorRb) gl.deleteRenderbuffer(this.prefixColorRb);
    if (this.prefixDepthStencilRb) gl.deleteRenderbuffer(this.prefixDepthStencilRb);
    if (this.prefixResolveFbo) gl.deleteFramebuffer(this.prefixResolveFbo);
    if (this.prefixTexture) gl.deleteTexture(this.prefixTexture);
    if (this.workFbo) gl.deleteFramebuffer(this.workFbo);
    if (this.workColorRb) gl.deleteRenderbuffer(this.workColorRb);
    if (this.workDepthStencilRb) gl.deleteRenderbuffer(this.workDepthStencilRb);
    this.prefixFbo = null;
    this.prefixColorRb = null;
    this.prefixDepthStencilRb = null;
    this.prefixResolveFbo = null;
    this.prefixTexture = null;
    this.prefixResolved = false;
    this.workFbo = null;
    this.workColorRb = null;
    this.workDepthStencilRb = null;
  }

  private setupFramebuffers(pw: number, ph: number): void {
    const gl = this.gl;
    this.deleteFramebuffers();

    this.prefixFbo = gl.createFramebuffer();
    this.prefixColorRb = gl.createRenderbuffer();
    this.prefixDepthStencilRb = gl.createRenderbuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.prefixFbo);
    gl.bindRenderbuffer(gl.RENDERBUFFER, this.prefixColorRb);
    gl.renderbufferStorageMultisample(gl.RENDERBUFFER, this.samples, gl.RGBA8, pw, ph);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, this.prefixColorRb);
    gl.bindRenderbuffer(gl.RENDERBUFFER, this.prefixDepthStencilRb);
    gl.renderbufferStorageMultisample(gl.RENDERBUFFER, this.samples, gl.DEPTH24_STENCIL8, pw, ph);
    gl.framebufferRenderbuffer(
      gl.FRAMEBUFFER,
      gl.DEPTH_STENCIL_ATTACHMENT,
      gl.RENDERBUFFER,
      this.prefixDepthStencilRb,
    );

    this.prefixResolveFbo = gl.createFramebuffer();
    this.prefixTexture = gl.createTexture();
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.prefixResolveFbo);
    gl.bindTexture(gl.TEXTURE_2D, this.prefixTexture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, pw, ph, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    gl.framebufferTexture2D(
      gl.FRAMEBUFFER,
      gl.COLOR_ATTACHMENT0,
      gl.TEXTURE_2D,
      this.prefixTexture,
      0,
    );
    this.prefixResolved = false;

    this.workFbo = gl.createFramebuffer();
    this.workColorRb = gl.createRenderbuffer();
    this.workDepthStencilRb = gl.createRenderbuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.workFbo);
    gl.bindRenderbuffer(gl.RENDERBUFFER, this.workColorRb);
    gl.renderbufferStorageMultisample(gl.RENDERBUFFER, this.samples, gl.RGBA8, pw, ph);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, this.workColorRb);
    gl.bindRenderbuffer(gl.RENDERBUFFER, this.workDepthStencilRb);
    gl.renderbufferStorageMultisample(gl.RENDERBUFFER, this.samples, gl.DEPTH24_STENCIL8, pw, ph);
    gl.framebufferRenderbuffer(
      gl.FRAMEBUFFER,
      gl.DEPTH_STENCIL_ATTACHMENT,
      gl.RENDERBUFFER,
      this.workDepthStencilRb,
    );

    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  private drawFrame(msg: BoardMessage): void {
    if (this.contextLost) return;
    this.trimGeometryCache();
    this.lastDrawCalls = 0;
    const gl = this.gl;
    const dpr = Math.max(0.1, Number(this.getDpr()) || 1);

    const w = Math.max(1, Math.round(msg.w));
    const h = Math.max(1, Math.round(msg.h));
    const pw = Math.max(1, Math.round(w * dpr));
    const ph = Math.max(1, Math.round(h * dpr));

    const sizeChanged = w !== this.logicalW || h !== this.logicalH || dpr !== this.currentDpr;
    if (sizeChanged || !this.prefixFbo || !this.prefixResolveFbo || !this.workFbo) {
      this.logicalW = w;
      this.logicalH = h;
      this.currentDpr = dpr;
      this.pw = pw;
      this.ph = ph;
      this.canvas.width = pw;
      this.canvas.height = ph;
      this.setupFramebuffers(pw, ph);
      this.cachedCount = 0;
      this.cachedState = defaultDrawState();
    }

    const rawKeep = Math.max(0, Math.min(Math.round(msg.keep ?? 0), this.fullOps.length));
    let keep = rawKeep;
    const incomingOps = Array.isArray(msg.ops) ? msg.ops : [];
    if (keep === 0) {
      this.fullOps = incomingOps.slice();
    } else {
      this.fullOps = this.fullOps.slice(0, keep).concat(incomingOps);
    }

    keep = findScopeStartContaining(this.fullOps, rawKeep);
    gl.viewport(0, 0, pw, ph);

    if (keep === 0) {
      const cState = defaultDrawState();
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.prefixFbo);
      this.clearCurrentTarget();
      const firstF = this.fullOps.findIndex((op) => Array.isArray(op) && op[0] === "F");
      if (firstF > 0) {
        this.executeOps(this.fullOps, 0, firstF, cState, dpr, pw, ph);
        this.cachedCount = firstF;
        this.cachedState = cloneDrawState(cState);
        this.resolvePrefixToTexture(pw, ph);
        this.prefixResolved = true;
        const suffixState = cloneDrawState(this.cachedState);
        gl.bindFramebuffer(gl.FRAMEBUFFER, this.workFbo);
        this.clearCurrentTarget();
        this.copyPrefixTextureToWork(pw, ph);
        this.executeOps(this.fullOps, firstF, this.fullOps.length, suffixState, dpr, pw, ph);
        this.resolveToScreen(this.workFbo, pw, ph);
        return;
      }
      this.executeOps(this.fullOps, 0, this.fullOps.length, cState, dpr, pw, ph);
      this.cachedCount = this.fullOps.length;
      this.cachedState = cloneDrawState(cState);
      this.prefixResolved = false;
      this.resolveToScreen(this.prefixFbo, pw, ph);
      return;
    }

    if (this.cachedCount > keep) {
      const cState = defaultDrawState();
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.prefixFbo);
      this.clearCurrentTarget();
      this.executeOps(this.fullOps, 0, keep, cState, dpr, pw, ph);
      this.cachedCount = keep;
      this.cachedState = cloneDrawState(cState);
      this.prefixResolved = false;
    } else if (this.cachedCount < keep) {
      const cState = cloneDrawState(this.cachedState);
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.prefixFbo);
      if (this.cachedCount === 0) {
        this.clearCurrentTarget();
      }
      this.executeOps(this.fullOps, this.cachedCount, keep, cState, dpr, pw, ph);
      this.cachedCount = keep;
      this.cachedState = cloneDrawState(cState);
      this.prefixResolved = false;
    }

    if (!this.prefixResolved) {
      this.resolvePrefixToTexture(pw, ph);
      this.prefixResolved = true;
    }

    const suffixState = cloneDrawState(this.cachedState);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.workFbo);
    this.clearCurrentTarget();
    this.copyPrefixTextureToWork(pw, ph);
    this.executeOps(this.fullOps, keep, this.fullOps.length, suffixState, dpr, pw, ph);
    this.resolveToScreen(this.workFbo, pw, ph);
  }

  private clearCurrentTarget(): void {
    const gl = this.gl;
    gl.colorMask(true, true, true, true);
    gl.stencilMask(0xff);
    gl.clearColor(0, 0, 0, 0);
    gl.clearStencil(0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.STENCIL_BUFFER_BIT);
  }

  private resolvePrefixToTexture(pw: number, ph: number): void {
    const gl = this.gl;
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.prefixFbo);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, this.prefixResolveFbo);
    gl.blitFramebuffer(0, 0, pw, ph, 0, 0, pw, ph, gl.COLOR_BUFFER_BIT, gl.NEAREST);
  }

  private copyPrefixTextureToWork(pw: number, ph: number): void {
    const gl = this.gl;
    const locs = this.imageLocs!;
    gl.useProgram(this.imageProg);
    gl.bindVertexArray(this.imageVao);
    gl.uniform2f(locs.resolution, pw, ph);
    gl.uniform1f(locs.dpr, 1);
    gl.uniform3f(locs.transform0, 1, 0, 0);
    gl.uniform3f(locs.transform1, 0, 1, 0);
    gl.uniform1f(locs.opacity, 1);
    gl.uniform1i(locs.passthrough, 1);

    gl.disable(gl.BLEND);
    gl.disable(gl.STENCIL_TEST);
    gl.colorMask(true, true, true, true);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.prefixTexture);
    gl.uniform1i(locs.tex, 0);

    // Framebuffer blit puts bottom row (y=ph) at v=0 and top row (y=0) at v=1.
    const quad = new Float32Array([
      0, 0, 0, 1,
      pw, 0, 1, 1,
      pw, ph, 1, 0,
      0, 0, 0, 1,
      pw, ph, 1, 0,
      0, ph, 0, 0,
    ]);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.imageQuadVbo);
    gl.bufferData(gl.ARRAY_BUFFER, quad, gl.DYNAMIC_DRAW);
    this.emitDrawArrays(gl.TRIANGLES, 0, 6);
    gl.enable(gl.BLEND);
  }

  private resolveToScreen(srcFbo: WebGLFramebuffer | null, pw: number, ph: number): void {
    const gl = this.gl;
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, srcFbo);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
    gl.blitFramebuffer(0, 0, pw, ph, 0, 0, pw, ph, gl.COLOR_BUFFER_BIT, gl.NEAREST);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  private applyBlendMode(mode: GlobalCompositeOperation): void {
    const gl = this.gl;
    switch (mode) {
      case "source-over":
        gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
        break;
      case "copy":
        gl.blendFunc(gl.ONE, gl.ZERO);
        break;
      case "destination-over":
        gl.blendFunc(gl.ONE_MINUS_DST_ALPHA, gl.ONE);
        break;
      case "destination-in":
        gl.blendFunc(gl.ZERO, gl.SRC_ALPHA);
        break;
      case "destination-out":
        gl.blendFunc(gl.ZERO, gl.ONE_MINUS_SRC_ALPHA);
        break;
      case "source-in":
        gl.blendFunc(gl.DST_ALPHA, gl.ZERO);
        break;
      case "source-out":
        gl.blendFunc(gl.ONE_MINUS_DST_ALPHA, gl.ZERO);
        break;
      case "source-atop":
        gl.blendFunc(gl.DST_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        break;
      case "destination-atop":
        gl.blendFunc(gl.ONE_MINUS_DST_ALPHA, gl.SRC_ALPHA);
        break;
      case "xor":
        gl.blendFunc(gl.ONE_MINUS_DST_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        break;
      case "lighter":
        gl.blendFunc(gl.ONE, gl.ONE);
        break;
      default:
        // Separable/non-separable backdrop blend modes (multiply, screen, etc.)
        // fall back to source-over because standard hardware blendFunc cannot read dst RGB.
        gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
        break;
    }
  }

  private getStrokeGeometry(
    path: ReadonlyArray<number>,
    pen: PenState,
    transform: [number, number, number, number, number, number],
    dpr: number,
  ): CachedGeometry {
    const currentScale = computeScaleFactor(transform, dpr, pen.cosmetic);
    const maxEdge = this.activeField ? this.activeField.maxEdge : 0;

    // Reference fast-path: skip hashing and Map lookup when the path array reference,
    // pen parameters, and scale tolerance match the cached entry.
    const fast = this.strokeRefCache.get(path as object);
    if (
      fast !== undefined &&
      fast.width === pen.width &&
      fast.cap === pen.cap &&
      fast.join === pen.join &&
      fast.miter === pen.miter &&
      fast.cosmetic === pen.cosmetic &&
      fast.dash === pen.dash &&
      (!pen.cosmetic ||
        (fast.m11 === transform[0] &&
          fast.m12 === transform[1] &&
          fast.m21 === transform[2] &&
          fast.m22 === transform[3])) &&
      fast.maxEdge === maxEdge &&
      currentScale <= fast.entry.builtScale * 1.75
    ) {
      return fast.entry;
    }

    const pathHash = hashNumbers(path);
    const dashKey = pen.dash && pen.dash.length > 0 ? pen.dash.join(",") : "";
    const cosmKey = pen.cosmetic
      ? `${transform[0]},${transform[1]},${transform[2]},${transform[3]}`
      : "0";
    const key = `S_${pathHash}_${pen.width}_${pen.cap}_${pen.join}_${pen.miter}_${cosmKey}_${dashKey}${maxEdge > 0 ? `_E${maxEdge}` : ""}`;

    const cached = this.geoCache.get(key);
    // Zooming out or zooming in within 1.75x reuses existing tessellation; only
    // zooming in beyond a bucket triggers re-tessellation.
    if (cached && currentScale <= cached.builtScale * 1.75) {
      this.strokeRefCache.set(path as object, {
        width: pen.width,
        cap: pen.cap,
        join: pen.join,
        miter: pen.miter,
        cosmetic: pen.cosmetic,
        dash: pen.dash,
        m11: transform[0],
        m12: transform[1],
        m21: transform[2],
        m22: transform[3],
        maxEdge,
        entry: cached,
      });
      return cached;
    }

    const builtScale = Math.pow(2, Math.ceil(Math.log2(Math.max(1, currentScale))));
    const linearTransform: [number, number, number, number, number, number] | undefined = pen.cosmetic
      ? [transform[0], transform[1], transform[2], transform[3], 0, 0]
      : undefined;
    const effPen =
      maxEdge > 0 && pen.cosmetic
        ? { ...pen, width: pen.width / Math.max(1e-6, computeScaleFactor(transform, 1, false)) }
        : pen;
    const effLinearTransform = maxEdge > 0 && pen.cosmetic ? undefined : linearTransform;
    const view = tessellateStrokeView(path, effPen, builtScale, 0.1, effLinearTransform, maxEdge);
    const firstVertex = this.appendArenaFloats(view);
    const entry: CachedGeometry = {
      key,
      firstVertex,
      vertexCount: view.length >> 1,
      coverFirstVertex: -1,
      builtScale,
    };
    this.geoCache.set(key, entry);
    this.strokeRefCache.set(path as object, {
      width: pen.width,
      cap: pen.cap,
      join: pen.join,
      miter: pen.miter,
      cosmetic: pen.cosmetic,
      dash: pen.dash,
      m11: transform[0],
      m12: transform[1],
      m21: transform[2],
      m22: transform[3],
      maxEdge,
      entry,
    });
    return entry;
  }

  private getFillGeometry(
    path: ReadonlyArray<number>,
    rule: CanvasFillRule,
    transform: [number, number, number, number, number, number],
    dpr: number,
  ): CachedGeometry {
    const currentScale = computeScaleFactor(transform, dpr, false);
    const pathHash = hashNumbers(path);
    const maxEdge = this.activeField ? this.activeField.maxEdge : 0;
    const key = maxEdge > 0 ? `F_${pathHash}_E${maxEdge}_${rule}` : `F_${pathHash}`;

    const cached = this.geoCache.get(key);
    if (cached && currentScale <= cached.builtScale * 1.75) {
      return cached;
    }

    const builtScale = Math.pow(2, Math.ceil(Math.log2(Math.max(1, currentScale))));
    const view = tessellateFillView(path, rule, builtScale, 0.1, undefined, maxEdge);
    const firstVertex = this.appendArenaFloats(view);
    const entry: CachedGeometry = {
      key,
      firstVertex,
      vertexCount: view.length >> 1,
      coverFirstVertex: -1,
      builtScale,
    };
    this.geoCache.set(key, entry);
    return entry;
  }

  private getRectFillGeometry(x: number, y: number, w: number, h: number): CachedGeometry {
    const key = `RF_${x}_${y}_${w}_${h}`;
    const cached = this.geoCache.get(key);
    if (cached) {
      return cached;
    }
    const x1 = x + w;
    const y1 = y + h;
    const vertices = new Float32Array([x, y, x1, y, x1, y1, x, y, x1, y1, x, y1]);
    const firstVertex = this.appendArenaFloats(vertices);
    const entry: CachedGeometry = {
      key,
      firstVertex,
      vertexCount: 6,
      coverFirstVertex: -1,
      builtScale: 1,
    };
    this.geoCache.set(key, entry);
    return entry;
  }

  private getGradientTexture(rawStops: unknown): WebGLTexture | null {
    const stops = parseNumericStops(rawStops);
    const key = stops.map((s) => `${s.offset}:${s.r},${s.g},${s.b},${s.a}`).join("|");
    const cached = this.gradTextures.get(key);
    if (cached) return cached;

    const gl = this.gl;
    const lut = bakeGradientLut(stops);
    const tex = gl.createTexture();
    if (!tex) return null;
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 256, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, lut);
    this.gradTextures.set(key, tex);
    return tex;
  }

  private getImageTexture(key: string): { tex: WebGLTexture; width: number; height: number } | null {
    const raw = this.rawImages.get(key);
    if (!raw) return null;
    const cached = this.imageTextures.get(key);
    if (cached) {
      return { tex: cached, width: raw.width, height: raw.height };
    }
    const gl = this.gl;
    const tex = gl.createTexture();
    if (!tex) return null;
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.RGBA,
      raw.width,
      raw.height,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      raw.data,
    );
    this.imageTextures.set(key, tex);
    return { tex, width: raw.width, height: raw.height };
  }

  private bindShapeProgramAndArena(
    pw: number,
    ph: number,
    dpr: number,
    transform: [number, number, number, number, number, number],
  ): void {
    const gl = this.gl;
    const locs = (this.activeField ? this.fieldLocs : this.shapeLocs)!;
    this.syncArenaVbo();
    gl.useProgram(this.activeField ? this.fieldProg : this.shapeProg);
    if (this.activeField) {
      const fLocs = this.fieldLocs!;
      const af = this.activeField;
      gl.activeTexture(gl.TEXTURE1);
      gl.bindTexture(gl.TEXTURE_2D, af.tex);
      gl.uniform1i(fLocs.fieldTex, 1);
      gl.uniform4f(fLocs.fieldRect, af.rect[0], af.rect[1], af.rect[2], af.rect[3]);
      gl.uniform2i(fLocs.fieldGrid, af.w, af.h);
      gl.uniform2f(fLocs.tagRange, af.tagMin, af.tagMax);
      gl.uniform4f(fLocs.tint, af.tint[0], af.tint[1], af.tint[2], af.tint[3]);
    }
    gl.bindVertexArray(this.shapeVao);
    gl.uniform2f(locs.resolution, pw, ph);
    gl.uniform1f(locs.dpr, dpr);
    gl.uniform3f(locs.transform0, transform[0], transform[2], transform[4]);
    gl.uniform3f(locs.transform1, transform[1], transform[3], transform[5]);
  }

  private setBrushUniforms(state: DrawState): void {
    const gl = this.gl;
    const locs = (this.activeField ? this.fieldLocs : this.shapeLocs)!;
    const brush = state.brush;
    const opacity = state.opacity;

    if (brush.gradient) {
      const g = brush.gradient;
      const tex = this.getGradientTexture(g.stops);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.uniform1i(locs.gradTex, 0);
      gl.uniform1f(locs.opacity, opacity);
      if (g.kind === "linear") {
        gl.uniform1i(locs.paintKind, 1);
        gl.uniform4f(locs.gradCoords0, g.x1, g.y1, g.x2, g.y2);
      } else {
        gl.uniform1i(locs.paintKind, 2);
        gl.uniform4f(locs.gradCoords0, g.fx, g.fy, g.fr, g.cx);
        gl.uniform2f(locs.gradCoords1, g.cy, g.cr);
      }
    } else {
      const effA = (brush.a / 255) * opacity;
      gl.uniform1i(locs.paintKind, 0);
      gl.uniform4f(
        locs.color,
        (brush.r / 255) * effA,
        (brush.g / 255) * effA,
        (brush.b / 255) * effA,
        effA,
      );
    }
  }

  private setPenUniforms(state: DrawState): void {
    const gl = this.gl;
    const locs = (this.activeField ? this.fieldLocs : this.shapeLocs)!;
    const pen = state.pen;
    const effA = (pen.a / 255) * state.opacity;
    gl.uniform1i(locs.paintKind, 0);
    gl.uniform4f(
      locs.color,
      (pen.r / 255) * effA,
      (pen.g / 255) * effA,
      (pen.b / 255) * effA,
      effA,
    );
  }

  private updateClipStencil(state: DrawState, pw: number, ph: number, dpr: number): void {
    const gl = this.gl;
    gl.stencilMask(0xff);
    gl.clearStencil(0);
    gl.clear(gl.STENCIL_BUFFER_BIT);

    if (!state.clip) {
      gl.disable(gl.STENCIL_TEST);
      return;
    }

    const clip = state.clip;
    const entry = this.getFillGeometry(clip.path, clip.rule, clip.transform, dpr);
    if (entry.vertexCount === 0) {
      return;
    }
    const coverFirst = this.ensureEntryCoverQuad(entry);
    this.bindShapeProgramAndArena(pw, ph, dpr, clip.transform);

    gl.enable(gl.STENCIL_TEST);
    gl.colorMask(false, false, false, false);
    gl.stencilMask(0x7f);
    gl.stencilFunc(gl.ALWAYS, 0, 0x7f);
    if (clip.rule === "evenodd") {
      gl.stencilOp(gl.KEEP, gl.KEEP, gl.INVERT);
    } else {
      gl.stencilOpSeparate(gl.FRONT, gl.KEEP, gl.KEEP, gl.INCR_WRAP);
      gl.stencilOpSeparate(gl.BACK, gl.KEEP, gl.KEEP, gl.DECR_WRAP);
    }
    this.emitDrawArrays(gl.TRIANGLES, entry.firstVertex, entry.vertexCount);

    // Since ref=0x80 & mask=0x7f is 0, NOTEQUAL passes iff (stencil & 0x7f) != 0,
    // and REPLACE writes ref=0x80 across all 8 bits.
    gl.stencilMask(0xff);
    gl.stencilFunc(gl.NOTEQUAL, 0x80, 0x7f);
    gl.stencilOp(gl.ZERO, gl.REPLACE, gl.REPLACE);
    this.emitDrawArrays(gl.TRIANGLES, coverFirst, 6);

    gl.colorMask(true, true, true, true);
    gl.stencilMask(0x00);
  }

  private drawFillWithStencil(
    entry: CachedGeometry,
    rule: CanvasFillRule,
    state: DrawState,
    pw: number,
    ph: number,
    dpr: number,
  ): void {
    if (entry.vertexCount === 0) return;
    const gl = this.gl;
    const coverFirst = this.ensureEntryCoverQuad(entry);
    this.bindShapeProgramAndArena(pw, ph, dpr, state.transform);
    this.applyBlendMode(state.composite);
    this.setBrushUniforms(state);

    gl.enable(gl.STENCIL_TEST);
    gl.colorMask(false, false, false, false);
    gl.stencilMask(0x7f);
    if (state.clip) {
      gl.stencilFunc(gl.EQUAL, 0x80, 0x80);
    } else {
      gl.stencilFunc(gl.ALWAYS, 0, 0x7f);
    }
    if (rule === "evenodd") {
      gl.stencilOp(gl.KEEP, gl.KEEP, gl.INVERT);
    } else {
      gl.stencilOpSeparate(gl.FRONT, gl.KEEP, gl.KEEP, gl.INCR_WRAP);
      gl.stencilOpSeparate(gl.BACK, gl.KEEP, gl.KEEP, gl.DECR_WRAP);
    }
    this.emitDrawArrays(gl.TRIANGLES, entry.firstVertex, entry.vertexCount);

    gl.colorMask(true, true, true, true);
    gl.stencilMask(0x7f);
    gl.stencilFunc(gl.NOTEQUAL, 0, 0x7f);
    gl.stencilOp(gl.KEEP, gl.ZERO, gl.ZERO);
    this.emitDrawArrays(gl.TRIANGLES, coverFirst, 6);
  }

  private drawStrokeWithStencil(
    entry: CachedGeometry,
    state: DrawState,
    pw: number,
    ph: number,
    dpr: number,
  ): void {
    if (entry.vertexCount === 0) return;
    const gl = this.gl;
    const coverFirst = this.ensureEntryCoverQuad(entry);
    const shaderTransform: [number, number, number, number, number, number] = state.pen.cosmetic && !this.activeField
      ? [1, 0, 0, 1, state.transform[4], state.transform[5]]
      : state.transform;

    this.bindShapeProgramAndArena(pw, ph, dpr, shaderTransform);
    this.applyBlendMode(state.composite);
    this.setPenUniforms(state);

    // Semi-transparent strokes use stencil so overlapping joints and curves are shaded once.
    gl.enable(gl.STENCIL_TEST);
    gl.colorMask(false, false, false, false);
    gl.stencilMask(0x7f);
    if (state.clip) {
      gl.stencilFunc(gl.EQUAL, 0x81, 0x80);
    } else {
      gl.stencilFunc(gl.ALWAYS, 1, 0x7f);
    }
    gl.stencilOp(gl.KEEP, gl.KEEP, gl.REPLACE);
    this.emitDrawArrays(gl.TRIANGLES, entry.firstVertex, entry.vertexCount);

    gl.colorMask(true, true, true, true);
    gl.stencilMask(0x7f);
    gl.stencilFunc(gl.NOTEQUAL, 0, 0x7f);
    gl.stencilOp(gl.KEEP, gl.ZERO, gl.ZERO);
    this.emitDrawArrays(gl.TRIANGLES, coverFirst, 6);
  }

  private drawDirectBrushRange(
    firstVertex: number,
    vertexCount: number,
    state: DrawState,
    pw: number,
    ph: number,
    dpr: number,
  ): void {
    if (vertexCount <= 0) return;
    const gl = this.gl;
    this.bindShapeProgramAndArena(pw, ph, dpr, state.transform);
    this.applyBlendMode(state.composite);
    this.setBrushUniforms(state);

    if (state.clip) {
      gl.enable(gl.STENCIL_TEST);
      gl.stencilMask(0x00);
      gl.stencilFunc(gl.EQUAL, 0x80, 0x80);
      gl.stencilOp(gl.KEEP, gl.KEEP, gl.KEEP);
    } else {
      gl.disable(gl.STENCIL_TEST);
    }

    gl.colorMask(true, true, true, true);
    this.emitDrawArrays(gl.TRIANGLES, firstVertex, vertexCount);
  }

  private drawDirectPenRange(
    firstVertex: number,
    vertexCount: number,
    state: DrawState,
    pw: number,
    ph: number,
    dpr: number,
  ): void {
    if (vertexCount <= 0) return;
    const gl = this.gl;
    const shaderTransform: [number, number, number, number, number, number] = state.pen.cosmetic && !this.activeField
      ? [1, 0, 0, 1, state.transform[4], state.transform[5]]
      : state.transform;

    this.bindShapeProgramAndArena(pw, ph, dpr, shaderTransform);
    this.applyBlendMode(state.composite);
    this.setPenUniforms(state);

    if (state.clip) {
      gl.enable(gl.STENCIL_TEST);
      gl.stencilMask(0x00);
      gl.stencilFunc(gl.EQUAL, 0x80, 0x80);
      gl.stencilOp(gl.KEEP, gl.KEEP, gl.KEEP);
    } else {
      gl.disable(gl.STENCIL_TEST);
    }

    gl.colorMask(true, true, true, true);
    this.emitDrawArrays(gl.TRIANGLES, firstVertex, vertexCount);
  }

  private drawImageOp(
    key: string,
    sx: number,
    sy: number,
    sw: number,
    sh: number,
    dx: number,
    dy: number,
    dw: number,
    dh: number,
    state: DrawState,
    pw: number,
    ph: number,
    dpr: number,
  ): void {
    const texInfo = this.getImageTexture(key);
    if (!texInfo) return;

    const gl = this.gl;
    const locs = this.imageLocs!;
    gl.useProgram(this.imageProg);
    gl.bindVertexArray(this.imageVao);
    gl.uniform2f(locs.resolution, pw, ph);
    gl.uniform1f(locs.dpr, dpr);
    gl.uniform3f(locs.transform0, state.transform[0], state.transform[2], state.transform[4]);
    gl.uniform3f(locs.transform1, state.transform[1], state.transform[3], state.transform[5]);
    gl.uniform1f(locs.opacity, state.opacity);
    gl.uniform1i(locs.passthrough, 0);

    this.applyBlendMode(state.composite);
    if (state.clip) {
      gl.enable(gl.STENCIL_TEST);
      gl.stencilMask(0x00);
      gl.stencilFunc(gl.EQUAL, 0x80, 0x80);
      gl.stencilOp(gl.KEEP, gl.KEEP, gl.KEEP);
    } else {
      gl.disable(gl.STENCIL_TEST);
    }
    gl.colorMask(true, true, true, true);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, texInfo.tex);
    const filter = state.smooth ? gl.LINEAR : gl.NEAREST;
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
    gl.uniform1i(locs.tex, 0);

    const u0 = sx / texInfo.width;
    const v0 = sy / texInfo.height;
    const u1 = (sx + sw) / texInfo.width;
    const v1 = (sy + sh) / texInfo.height;
    const x0 = dx;
    const y0 = dy;
    const x1 = dx + dw;
    const y1 = dy + dh;

    const quad = new Float32Array([
      x0, y0, u0, v0,
      x1, y0, u1, v0,
      x1, y1, u1, v1,
      x0, y0, u0, v0,
      x1, y1, u1, v1,
      x0, y1, u0, v1,
    ]);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.imageQuadVbo);
    gl.bufferData(gl.ARRAY_BUFFER, quad, gl.DYNAMIC_DRAW);
    this.emitDrawArrays(gl.TRIANGLES, 0, 6);
  }

  private executeOps(
    ops: unknown[][],
    start: number,
    end: number,
    state: DrawState,
    dpr: number,
    pw: number,
    ph: number,
  ): void {
    start = Math.max(0, Math.min(start, ops.length));
    end = Math.max(start, Math.min(end, ops.length));
    if (start > 0 && state.clip) {
      this.updateClipStencil(state, pw, ph, dpr);
    }

    for (let i = start; i < end; i++) {
      const op = ops[i];
      if (!Array.isArray(op) || op.length === 0) continue;
      const code = op[0];

      switch (code) {
        case "T": {
          state.transform = [
            Number(op[1] ?? 1),
            Number(op[2] ?? 0),
            Number(op[3] ?? 0),
            Number(op[4] ?? 1),
            Number(op[5] ?? 0),
            Number(op[6] ?? 0),
          ];
          break;
        }

        case "P": {
          const r = clampByte(op[1]);
          const g = clampByte(op[2]);
          const b = clampByte(op[3]);
          const a = clampByte(op[4]);
          const rawWidth = Number(op[5] ?? 1);
          const cap = mapLineCap(op[6]);
          const join = mapLineJoin(op[7]);
          const miter = Number(op[8] ?? 2);
          const cosmetic = Boolean(op[9]);
          const rawDash = Array.isArray(op[10]) ? (op[10] as unknown[]) : [];
          const dash = rawDash.length > 0 ? rawDash.map((n) => Number(n) || 0) : null;
          state.pen = {
            r,
            g,
            b,
            a,
            color: formatRgbaColor(r, g, b, a),
            width: rawWidth > 0 ? rawWidth : 1,
            cap,
            join,
            miter,
            cosmetic,
            dash,
          };
          break;
        }

        case "B": {
          const r = clampByte(op[1]);
          const g = clampByte(op[2]);
          const b = clampByte(op[3]);
          const a = clampByte(op[4]);
          state.brush = {
            r,
            g,
            b,
            a,
            color: formatRgbaColor(r, g, b, a),
            gradient: null,
          };
          break;
        }

        case "G": {
          const kind = String(op[1] ?? "linear");
          const coords = Array.isArray(op[2]) ? (op[2] as unknown[]).map((n) => Number(n) || 0) : [];
          const stops = parseGradientStops(op[3]);
          if (kind === "radial") {
            const fx = coords[0] ?? 0;
            const fy = coords[1] ?? 0;
            const fr = coords.length >= 6 ? (coords[2] ?? 0) : 0;
            const cx = coords.length >= 6 ? (coords[3] ?? fx) : fx;
            const cy = coords.length >= 6 ? (coords[4] ?? fy) : fy;
            const cr = coords.length >= 6 ? (coords[5] ?? 0) : (coords[2] ?? 0);
            state.brush = {
              r: 0,
              g: 0,
              b: 0,
              a: 255,
              color: state.brush.color,
              gradient: { kind: "radial", fx, fy, fr, cx, cy, cr, stops },
            };
          } else {
            const x1 = coords[0] ?? 0;
            const y1 = coords[1] ?? 0;
            const x2 = coords[2] ?? 0;
            const y2 = coords[3] ?? 0;
            state.brush = {
              r: 0,
              g: 0,
              b: 0,
              a: 255,
              color: state.brush.color,
              gradient: { kind: "linear", x1, y1, x2, y2, stops },
            };
          }
          break;
        }

        case "O": {
          state.opacity =
            Math.max(0, Math.min(1, Number(op[1] ?? 1))) * (this.activeField ? this.activeField.passOpacity : 1);
          break;
        }

        case "M": {
          state.composite = mapCompositeMode(op[1]);
          break;
        }

        case "K": {
          if (!Array.isArray(op[1])) {
            state.clip = null;
          } else {
            const path = op[1] as number[];
            const rule: CanvasFillRule = Number(op[2] ?? 0) === 0 ? "evenodd" : "nonzero";
            state.clip = {
              path,
              rule,
              transform: [...state.transform] as [number, number, number, number, number, number],
            };
          }
          this.updateClipStencil(state, pw, ph, dpr);
          break;
        }

        case "R": {
          const x = Number(op[1] ?? 0);
          const y = Number(op[2] ?? 0);
          const w = Number(op[3] ?? 0);
          const h = Number(op[4] ?? 0);
          if (state.brush.a > 0 || state.brush.gradient !== null) {
            if (this.activeField) {
              const rectPath = [0, x, y, 1, x + w, y, 1, x + w, y + h, 1, x, y + h, 3];
              const fillEntry = this.getFillGeometry(rectPath, "nonzero", state.transform, dpr);
              this.drawDirectBrushRange(fillEntry.firstVertex, fillEntry.vertexCount, state, pw, ph, dpr);
            } else {
              const fillEntry = this.getRectFillGeometry(x, y, w, h);
              this.drawDirectBrushRange(fillEntry.firstVertex, fillEntry.vertexCount, state, pw, ph, dpr);
            }
          }
          if (state.pen.a > 0) {
            const rectPath = [0, x, y, 1, x + w, y, 1, x + w, y + h, 1, x, y + h, 3];
            const strokeEntry = this.getStrokeGeometry(rectPath, state.pen, state.transform, dpr);
            const isOpaquePen =
              state.pen.a === 255 && state.opacity >= 1 && state.composite === "source-over";
            if (isOpaquePen || this.activeField !== null) {
              this.drawDirectPenRange(strokeEntry.firstVertex, strokeEntry.vertexCount, state, pw, ph, dpr);
            } else {
              this.drawStrokeWithStencil(strokeEntry, state, pw, ph, dpr);
            }
          }
          break;
        }

        case "S": {
          const hasBrush = state.brush.a > 0 || state.brush.gradient !== null;
          const hasPen = state.pen.a > 0;
          const isOpaquePen =
            hasPen && state.pen.a === 255 && state.opacity >= 1 && state.composite === "source-over";

          // Opaque stroke-only runs share the same state and do not need stencil;
          // merge adjacent contiguous ranges in the single arena VBO into 1 draw call.
          if (!hasBrush && isOpaquePen) {
            let rangeFirst = -1;
            let rangeCount = 0;
            let j = i;
            while (j < end) {
              const cur = ops[j];
              if (!Array.isArray(cur) || cur[0] !== "S") break;
              const rawP = cur[1];
              const p = Array.isArray(rawP) ? (rawP as ReadonlyArray<number>) : [];
              const e = this.getStrokeGeometry(p, state.pen, state.transform, dpr);
              if (e.vertexCount > 0) {
                if (rangeFirst < 0) {
                  rangeFirst = e.firstVertex;
                  rangeCount = e.vertexCount;
                } else if (e.firstVertex === rangeFirst + rangeCount) {
                  rangeCount += e.vertexCount;
                } else {
                  this.drawDirectPenRange(rangeFirst, rangeCount, state, pw, ph, dpr);
                  rangeFirst = e.firstVertex;
                  rangeCount = e.vertexCount;
                }
              }
              j++;
            }
            if (rangeCount > 0) {
              this.drawDirectPenRange(rangeFirst, rangeCount, state, pw, ph, dpr);
            }
            i = j - 1;
            break;
          }

          const path = Array.isArray(op[1]) ? (op[1] as ReadonlyArray<number>) : [];
          const rule: CanvasFillRule = normalizeFillRule(Number(op[2] ?? 0) === 0 ? "evenodd" : "nonzero");
          if (hasBrush) {
            const fillEntry = this.getFillGeometry(path, rule, state.transform, dpr);
            if (this.activeField) {
              this.drawDirectBrushRange(fillEntry.firstVertex, fillEntry.vertexCount, state, pw, ph, dpr);
            } else {
              this.drawFillWithStencil(fillEntry, rule, state, pw, ph, dpr);
            }
          }
          if (hasPen) {
            const strokeEntry = this.getStrokeGeometry(path, state.pen, state.transform, dpr);
            if (isOpaquePen || this.activeField !== null) {
              this.drawDirectPenRange(strokeEntry.firstVertex, strokeEntry.vertexCount, state, pw, ph, dpr);
            } else {
              this.drawStrokeWithStencil(strokeEntry, state, pw, ph, dpr);
            }
          }
          break;
        }

        case "I": {
          const key = String(op[1] ?? "");
          const sx = Number(op[2] ?? 0);
          const sy = Number(op[3] ?? 0);
          const sw = Number(op[4] ?? 0);
          const sh = Number(op[5] ?? 0);
          const dx = Number(op[6] ?? 0);
          const dy = Number(op[7] ?? 0);
          const dw = Number(op[8] ?? 0);
          const dh = Number(op[9] ?? 0);
          this.drawImageOp(key, sx, sy, sw, sh, dx, dy, dw, dh, state, pw, ph, dpr);
          break;
        }

        case "H": {
          state.smooth = Boolean(op[1]);
          break;
        }

        case "F": {
          let closeIdx = -1;
          for (let j = i + 1; j < end; j++) {
            const cur = ops[j];
            if (Array.isArray(cur) && cur[0] === "f") {
              closeIdx = j;
              break;
            }
          }
          const scopeEnd = closeIdx >= 0 ? closeIdx : end;
          const key = String(op[1] ?? "");
          const version = Math.round(Number(op[2] ?? 0));
          const rawRect = Array.isArray(op[3]) ? op[3] : [];
          const rx = Number(rawRect[0] ?? 0);
          const ry = Number(rawRect[1] ?? 0);
          const rw = Number(rawRect[2] ?? 0);
          const rh = Number(rawRect[3] ?? 0);
          const maxEdge = Math.max(0.5, Number(op[4] ?? 4));
          const passes = Array.isArray(op[5]) ? op[5] : [];
          const fieldTexInfo = key && rw > 0 && rh > 0 ? this.getFieldTexture(key, version) : null;

          if (fieldTexInfo && passes.length > 0 && i + 1 < scopeEnd) {
            const entryState = cloneDrawState(state);
            const prevField = this.activeField;
            let hasClipInScope = false;
            for (let j = i + 1; j < scopeEnd; j++) {
              const subOp = ops[j];
              if (Array.isArray(subOp) && subOp[0] === "K") {
                hasClipInScope = true;
                break;
              }
            }
            for (let pIdx = passes.length - 1; pIdx >= 0; pIdx--) {
              const rawPass = passes[pIdx];
              let tagMin = 0;
              let tagMax = 0;
              let passOpacity = 1;
              let tintR = 0;
              let tintG = 0;
              let tintB = 0;
              let tintA = 0;
              if (Array.isArray(rawPass)) {
                if (rawPass.length < 3) continue;
                tagMin = Number(rawPass[0] ?? 0);
                tagMax = Number(rawPass[1] ?? tagMin);
                passOpacity = Math.max(0, Math.min(1, Number(rawPass[2] ?? 1)));
                if (rawPass.length >= 7) {
                  tintR = clampByte(rawPass[3]) / 255;
                  tintG = clampByte(rawPass[4]) / 255;
                  tintB = clampByte(rawPass[5]) / 255;
                  tintA = clampByte(rawPass[6]) / 255;
                }
              } else if (rawPass && typeof rawPass === "object") {
                const pObj = rawPass as Record<string, unknown>;
                const rawTagRange = Array.isArray(pObj.tagRange) ? pObj.tagRange : [0, 0];
                tagMin = Number(rawTagRange[0] ?? 0);
                tagMax = Number(rawTagRange[1] ?? tagMin);
                passOpacity = Math.max(0, Math.min(1, Number(pObj.opacity ?? 1)));
                if (pObj.a !== undefined) {
                  tintR = clampByte(pObj.r) / 255;
                  tintG = clampByte(pObj.g) / 255;
                  tintB = clampByte(pObj.b) / 255;
                  tintA = clampByte(pObj.a) / 255;
                }
              } else {
                continue;
              }
              if (tintA <= 0) {
                tintR = 0;
                tintG = 0;
                tintB = 0;
                tintA = 0;
              }

              const passState = cloneDrawState(entryState);
              passState.opacity = entryState.opacity * passOpacity;
              if (hasClipInScope) {
                this.updateClipStencil(passState, pw, ph, dpr);
              }
              this.activeField = {
                tex: fieldTexInfo.tex,
                w: fieldTexInfo.w,
                h: fieldTexInfo.h,
                rect: [rx, ry, rw, rh],
                maxEdge,
                tagMin,
                tagMax,
                passOpacity,
                tint: [tintR, tintG, tintB, tintA],
              };
              this.executeOps(ops, i + 1, scopeEnd, passState, dpr, pw, ph);
            }
            this.activeField = prevField;
            const restored = cloneDrawState(entryState);
            state.transform = restored.transform;
            state.pen = restored.pen;
            state.brush = restored.brush;
            state.opacity = restored.opacity;
            state.composite = restored.composite;
            state.clip = restored.clip;
            state.smooth = restored.smooth;
            if (hasClipInScope) {
              this.updateClipStencil(state, pw, ph, dpr);
            }
          }

          i = closeIdx >= 0 ? closeIdx : end - 1;
          break;
        }

        case "f": {
          break;
        }
      }
    }
  }
}
