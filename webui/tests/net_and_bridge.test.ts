import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { loadProjectFiles } from "../vf/src/loader.ts";
import { Runtime } from "../vf/src/runtime.ts";
import { attachBridge } from "../src/bridge.ts";
import { BackendClient, type BoardMessage, type FieldMeta, type ImageMeta, type WebSocketLike } from "../src/net.ts";

class MockWebSocket implements WebSocketLike {
  binaryType = "arraybuffer";
  readyState = 0;
  sent: Array<string | ArrayBuffer | Uint8Array> = [];
  private listeners = new Map<string, Set<(ev: unknown) => void>>();

  send(data: string | ArrayBuffer | Uint8Array): void {
    this.sent.push(data);
  }

  close(): void {
    this.readyState = 3;
    this.emit("close", { code: 1000 });
  }

  addEventListener(type: string, listener: (ev: unknown) => void): void {
    let set = this.listeners.get(type);
    if (!set) {
      set = new Set();
      this.listeners.set(type, set);
    }
    set.add(listener);
  }

  removeEventListener(type: string, listener: (ev: unknown) => void): void {
    this.listeners.get(type)?.delete(listener);
  }

  open(): void {
    this.readyState = 1;
    this.emit("open", {});
  }

  receiveText(obj: unknown): void {
    const data = typeof obj === "string" ? obj : JSON.stringify(obj);
    this.emit("message", { data });
  }

  receiveBinary(bytes: Uint8Array | ArrayBuffer): void {
    const data = bytes instanceof ArrayBuffer ? bytes : bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
    this.emit("message", { data });
  }

  private emit(type: string, ev: unknown): void {
    for (const fn of this.listeners.get(type) ?? []) {
      fn(ev);
    }
  }
}

describe("BackendClient & Bridge", () => {
  it("matches call and reply messages (including out-of-order and error replies)", async () => {
    let mockWs!: MockWebSocket;
    const client = new BackendClient({
      createWebSocket: () => {
        mockWs = new MockWebSocket();
        queueMicrotask(() => mockWs.open());
        return mockWs;
      },
    });

    await client.connect("ws://127.0.0.1:9999");
    assert.equal(client.connected, true);

    const p1 = client.call("layer.add", { view: "main" });
    const p2 = client.call("layer.remove", { id: 99 });

    assert.equal(mockWs.sent.length, 2);
    const req1 = JSON.parse(String(mockWs.sent[0]));
    const req2 = JSON.parse(String(mockWs.sent[1]));
    assert.deepEqual(req1, { t: "call", id: 1, method: "layer.add", params: { view: "main" } });
    assert.deepEqual(req2, { t: "call", id: 2, method: "layer.remove", params: { id: 99 } });

    // 先回复 id=2（失败），再回复 id=1（成功）
    mockWs.receiveText({ t: "reply", id: 2, ok: false, error: "图层不存在" });
    mockWs.receiveText({ t: "reply", id: 1, ok: true, result: { layerId: "lyr_new" } });

    await assert.rejects(p2, /图层不存在/);
    const res1 = await p1;
    assert.deepEqual(res1, { layerId: "lyr_new" });
  });

  it("pairs image text frame with the immediately following binary frame", async () => {
    let mockWs!: MockWebSocket;
    const client = new BackendClient({
      createWebSocket: () => {
        mockWs = new MockWebSocket();
        mockWs.readyState = 1;
        return mockWs;
      },
    });

    await client.connect("ws://127.0.0.1:9999");

    const receivedImages: Array<{ meta: ImageMeta; data: number[] }> = [];
    const receivedStates: Array<{ key: string; value: unknown }> = [];
    const receivedBoards: BoardMessage[] = [];

    client.onImage((meta, data) => {
      receivedImages.push({ meta, data: Array.from(data) });
    });
    client.onState("theme", (value, key) => {
      receivedStates.push({ key, value });
    });
    client.onBoard("main", (msg) => {
      receivedBoards.push(msg);
    });

    mockWs.receiveText({ t: "image", key: "a1b2", w: 2, h: 1, bytes: 8 });
    mockWs.receiveBinary(new Uint8Array([10, 20, 30, 255, 40, 50, 60, 128]));

    assert.equal(receivedImages.length, 1);
    assert.deepEqual(receivedImages[0].meta, {
      t: "image",
      key: "a1b2",
      w: 2,
      h: 1,
      bytes: 8,
    });
    assert.deepEqual(receivedImages[0].data, [10, 20, 30, 255, 40, 50, 60, 128]);

    // 测试 state 和 board 分发
    mockWs.receiveText({ t: "state", key: "theme", value: { mode: "dark" } });
    mockWs.receiveText({ t: "board", view: "main", frame: 7, w: 400, h: 300, keep: 0, ops: [["K", 0]] });
    assert.equal(receivedStates.length, 1);
    assert.deepEqual(receivedStates[0], { key: "theme", value: { mode: "dark" } });
    assert.equal(receivedBoards.length, 1);
    assert.equal(receivedBoards[0].frame, 7);
  });

  it("notifies on disconnect and rejects pending calls without reconnecting", async () => {
    let createCount = 0;
    let mockWs!: MockWebSocket;
    let disconnected = 0;

    const client = new BackendClient({
      createWebSocket: () => {
        createCount++;
        mockWs = new MockWebSocket();
        mockWs.readyState = 1;
        return mockWs;
      },
      onDisconnect: () => {
        disconnected++;
      },
    });

    await client.connect("ws://127.0.0.1:9999");
    const pending = client.call("project.save", {});
    mockWs.close();

    await assert.rejects(pending, /WebSocket 已断开/);
    assert.equal(disconnected, 1);
    assert.equal(createCount, 1);
    assert.equal(client.connected, false);
  });

  it("bridges Runtime raiseBusiness, raiseAction, and incoming state to setBusinessState", async () => {
    let mockWs!: MockWebSocket;
    const client = new BackendClient({
      createWebSocket: () => {
        mockWs = new MockWebSocket();
        mockWs.readyState = 1;
        return mockWs;
      },
    });
    await client.connect("ws://127.0.0.1:9999");

    const files: Record<string, string> = {
      "project.json": JSON.stringify({ version: 1, name: "BridgeTest", windows: [{ id: "window" }] }),
      "ui/scene.json": JSON.stringify({
        version: 1,
        root: "window",
        nodes: {
          window: { type: "Window", width: 400, height: 300, children: ["btnBiz", "btnAct"] },
          btnBiz: { type: "Button", width: 80, height: 30 },
          btnAct: { type: "Button", width: 80, height: 30 },
        },
      }),
      "logic/app.logic.yaml": `
version: 1
state:
  layer:
    visible:
      type: boolean
      initial: true
      owner: business
  exportStatus:
    type: string
    initial: "idle"
events:
  layer.toggle:
    owner: business
actions:
  doExport:
    input:
      fmt: "'mp4'"
    onSuccess:
      - set: exportStatus = result
objects:
  btnBiz:
    events:
      click:
        - emit:
            name: layer.toggle
            args:
              target: "'lyr_1'"
  btnAct:
    events:
      click:
        - call: doExport
`,
    };

    const project = await loadProjectFiles(
      {
        root: async () => "/test",
        readText: async (p) => files[p] ?? null,
        listDir: async () => [],
      },
      null,
    );

    const runtime = new Runtime(project);
    const bridge = attachBridge(runtime, client, {
      "layer/visible": "layer.visible",
    });

    // 1. 触发业务事件 -> 转成 client.call("layer.toggle", { target: "lyr_1" })
    runtime.dispatch("btnBiz", "click");
    assert.equal(mockWs.sent.length, 1);
    assert.deepEqual(JSON.parse(String(mockWs.sent[0])), {
      t: "call",
      id: 1,
      method: "layer.toggle",
      params: { target: "lyr_1" },
    });
    mockWs.receiveText({ t: "reply", id: 1, ok: true, result: {} });

    // 2. 触发动作 -> 转成 client.call("doExport", { fmt: "mp4" }) -> 完成后调用 finishActionSuccess
    runtime.dispatch("btnAct", "click");
    assert.equal(mockWs.sent.length, 2);
    assert.deepEqual(JSON.parse(String(mockWs.sent[1])), {
      t: "call",
      id: 2,
      method: "doExport",
      params: { fmt: "mp4" },
    });
    mockWs.receiveText({ t: "reply", id: 2, ok: true, result: "done_mp4" });
    await new Promise((r) => setTimeout(r, 10));
    assert.equal(runtime.getState("exportStatus"), "done_mp4");

    // 3. 后端下发 state -> 映射写入 setBusinessState("layer.visible", false)
    mockWs.receiveText({ t: "state", key: "layer/visible", value: false });
    assert.equal(runtime.getState("layer.visible"), false);

    bridge.dispose();
  });

  it("pairs field text frame with binary frame, handles unaligned offset, and isolates from image meta", async () => {
    let mockWs!: MockWebSocket;
    const client = new BackendClient({
      createWebSocket: () => {
        mockWs = new MockWebSocket();
        mockWs.readyState = 1;
        return mockWs;
      },
    });

    await client.connect("ws://127.0.0.1:9999");

    const fields: Array<{ meta: FieldMeta; data: Float32Array }> = [];
    const images: Array<{ meta: ImageMeta; data: Uint8Array }> = [];
    const unsubField = client.onField((meta, data) => fields.push({ meta, data }));
    const unsubImg = client.onImage((meta, data) => images.push({ meta, data }));

    // 1. Non-4-byte-aligned binary buffer (offset = 1, 2x1 RGBA32F = 8 floats = 32 bytes)
    const rawBuf = new ArrayBuffer(36);
    const alignedFloats = new Float32Array([1.5, -2.5, 1, 0, 3.25, 4.75, 0, 0]);
    new Uint8Array(rawBuf, 1, 32).set(new Uint8Array(alignedFloats.buffer));
    const unalignedView = new Uint8Array(rawBuf, 1, 32);

    mockWs.receiveText({ t: "field", key: "fld_0", w: 2, h: 1, version: 3, bytes: 32 });
    mockWs.receiveBinary(unalignedView);

    assert.equal(fields.length, 1);
    assert.equal(fields[0].meta.key, "fld_0");
    assert.equal(fields[0].meta.version, 3);
    assert.deepEqual(Array.from(fields[0].data), Array.from(alignedFloats));
    assert.equal(images.length, 0);

    // 2. Interleaved field meta replaced by image meta (and vice versa) must not cross-pair
    mockWs.receiveText({ t: "field", key: "fld_stale", w: 1, h: 1, version: 1, bytes: 16 });
    mockWs.receiveText({ t: "image", key: "img_ok", w: 1, h: 1, fmt: "rgba8888", bytes: 4 });
    mockWs.receiveBinary(new Uint8Array([10, 20, 30, 255]));
    assert.equal(fields.length, 1);
    assert.equal(images.length, 1);
    assert.equal(images[0].meta.key, "img_ok");

    mockWs.receiveText({ t: "image", key: "img_stale", w: 1, h: 1, fmt: "rgba8888", bytes: 4 });
    mockWs.receiveText({ t: "field", key: "fld_ok", w: 1, h: 1, version: 2, bytes: 16 });
    const f2 = new Float32Array([5, 6, 1, 0]);
    mockWs.receiveBinary(f2.buffer);
    assert.equal(images.length, 1);
    assert.equal(fields.length, 2);
    assert.equal(fields[1].meta.key, "fld_ok");
    assert.deepEqual(Array.from(fields[1].data), [5, 6, 1, 0]);

    unsubField();
    unsubImg();
  });
});
