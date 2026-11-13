import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { PassThrough } from "node:stream";
import test from "node:test";

import {
  clearSessionMetadata,
  findGitCommonDir,
  findRepoRoot,
  getToolkitPaths,
  isPidAlive,
  readActiveSessionFile,
  resolveBackendEndpoint,
  writeSessionMetadata,
} from "../src/discovery.mjs";
import { createMcpHandler } from "../src/mcp_protocol.mjs";
import { encodeRgbaToPng } from "../src/png.mjs";
import { ToolRegistry } from "../src/registry.mjs";
import { createDefaultRegistry } from "../src/tools/index.mjs";
import { isLoopbackHost, startHttpTransport } from "../src/transport_http.mjs";
import { createStdioTransport } from "../src/transport_stdio.mjs";

test("encodeRgbaToPng emits valid PNG signature, IHDR, IDAT, and IEND", () => {
  const width = 4;
  const height = 3;
  const rgba = Buffer.alloc(width * height * 4);
  for (let i = 0; i < width * height; i += 1) {
    rgba[i * 4 + 0] = (i * 20) & 0xff;
    rgba[i * 4 + 1] = 120;
    rgba[i * 4 + 2] = 220;
    rgba[i * 4 + 3] = 255;
  }

  const png = encodeRgbaToPng(rgba, width, height);
  const magic = png.subarray(0, 8);
  assert.deepEqual(Array.from(magic), [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

  // First chunk after 8-byte signature must be IHDR with matching dimensions.
  const ihdrType = png.subarray(12, 16).toString("ascii");
  assert.equal(ihdrType, "IHDR");
  assert.equal(png.readUInt32BE(16), width);
  assert.equal(png.readUInt32BE(20), height);
  assert.equal(png[24], 8); // bit depth
  assert.equal(png[25], 6); // RGBA color type

  assert(png.includes(Buffer.from("IDAT", "ascii")), "PNG must contain IDAT chunk");
  const iendTail = png.subarray(png.length - 8, png.length - 4).toString("ascii");
  assert.equal(iendTail, "IEND");
});

test("discovery resolves worktree commondir, filters dead PIDs, and honors precedence", () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "animean-mcp-disc-"));
  const origEnvWs = process.env.ANIMEAN_BACKEND_WS_URL;

  try {
    const mainRepo = path.join(tmp, "main-repo");
    const mainGitDir = path.join(mainRepo, ".git");
    const wtGitMeta = path.join(mainGitDir, "worktrees", "wt-a");
    const wtRepo = path.join(tmp, "wt-a");

    fs.mkdirSync(wtGitMeta, { recursive: true });
    fs.mkdirSync(wtRepo, { recursive: true });
    fs.writeFileSync(path.join(wtGitMeta, "commondir"), "../..\n", "utf-8");
    fs.writeFileSync(path.join(wtRepo, ".git"), `gitdir: ${wtGitMeta}\n`, "utf-8");

    assert.equal(findRepoRoot(wtRepo), wtRepo);
    assert.equal(findGitCommonDir(wtRepo), mainGitDir);
    const paths = getToolkitPaths(wtRepo);
    assert.equal(paths.mainRepoRoot, mainRepo);

    // Dead PID detection using an exited short child process
    const deadProc = spawnSync(process.execPath, ["-e", "process.exit(0)"]);
    assert(deadProc.pid && deadProc.pid > 0);
    assert.equal(isPidAlive(process.pid), true);
    assert.equal(isPidAlive(deadProc.pid), false);

    // Stale session file with dead PID must be ignored
    writeSessionMetadata(
      {
        backendPid: deadProc.pid,
        wsUrl: "ws://127.0.0.1:49999/?token=dead",
      },
      wtRepo
    );
    assert.equal(readActiveSessionFile(wtRepo), null);

    // Active session written in wtRepo should also be visible from mainRepo via shared git dir
    writeSessionMetadata(
      {
        backendPid: process.pid,
        wsUrl: "ws://127.0.0.1:43432/?token=live",
      },
      wtRepo
    );
    const seenFromMain = readActiveSessionFile(mainRepo);
    assert(seenFromMain);
    assert.equal(seenFromMain.wsUrl, "ws://127.0.0.1:43432/?token=live");
    assert.equal(seenFromMain._source, "shared_git_session_file");

    delete process.env.ANIMEAN_BACKEND_WS_URL;
    assert.equal(
      resolveBackendEndpoint({ explicitWsUrl: "ws://127.0.0.1:9001/?token=x", repoRoot: wtRepo }).source,
      "explicit_argument"
    );

    process.env.ANIMEAN_BACKEND_WS_URL = "ws://127.0.0.1:9002/?token=env";
    assert.equal(resolveBackendEndpoint({ repoRoot: wtRepo }).source, "env_ANIMEAN_BACKEND_WS_URL");

    delete process.env.ANIMEAN_BACKEND_WS_URL;
    const fromFile = resolveBackendEndpoint({ repoRoot: mainRepo });
    assert.equal(fromFile.wsUrl, "ws://127.0.0.1:43432/?token=live");

    clearSessionMetadata(wtRepo);
    assert.equal(readActiveSessionFile(wtRepo), null);
    assert.equal(readActiveSessionFile(mainRepo), null);
    assert.equal(resolveBackendEndpoint({ repoRoot: wtRepo }).source, "default_dev_endpoint");
  } finally {
    if (origEnvWs === undefined) {
      delete process.env.ANIMEAN_BACKEND_WS_URL;
    } else {
      process.env.ANIMEAN_BACKEND_WS_URL = origEnvWs;
    }
    fs.rmSync(tmp, { recursive: true, force: true });
  }
});

test("ToolRegistry registers all 14 default tools and formats JSON, image, and errors", async () => {
  const defaultRegistry = createDefaultRegistry();
  const listed = defaultRegistry.listTools();
  assert.equal(listed.length, 14);
  for (const name of [
    "backend_status",
    "backend_connect",
    "backend_launch_dev",
    "get_live_state",
    "get_python_debug_logs",
    "get_board_drawlist_summary",
    "inspect_scene",
    "inspect_cell_geometry",
    "inspect_automapping",
    "trace_hooks",
    "render_frame_png",
    "export_repro_bundle",
    "eval_python",
    "call_backend_rpc",
  ]) {
    assert(
      listed.some((t) => t.name === name),
      `Missing default tool: ${name}`
    );
  }

  const reg = new ToolRegistry();
  reg.registerAll([
    {
      name: "echo",
      description: "Echo input",
      inputSchema: { type: "object", properties: { value: { type: "string" } } },
      handler: (args) => ({ echoed: args.value }),
    },
    {
      name: "img_tool",
      description: "Image tool",
      inputSchema: { type: "object", properties: {} },
      handler: () => ({
        ok: true,
        __mcpImage: { base64: "AAAA", mimeType: "image/png" },
      }),
    },
    {
      name: "boom",
      description: "Throws error",
      inputSchema: { type: "object", properties: {} },
      handler: () => {
        throw new Error("kaboom");
      },
    },
  ]);

  const echoRes = await reg.callTool("echo", { value: "hi" });
  assert.equal(echoRes.isError, false);
  assert.deepEqual(JSON.parse(echoRes.content[0].text), { echoed: "hi" });

  const imgRes = await reg.callTool("img_tool", {});
  assert.equal(imgRes.isError, false);
  assert.equal(imgRes.content.length, 2);
  assert.deepEqual(JSON.parse(imgRes.content[0].text), { ok: true });
  assert.equal(imgRes.content[1].type, "image");
  assert.equal(imgRes.content[1].data, "AAAA");

  const unknownRes = await reg.callTool("missing_tool", {});
  assert.equal(unknownRes.isError, true);

  const boomRes = await reg.callTool("boom", {});
  assert.equal(boomRes.isError, true);
  assert.match(boomRes.content[0].text, /kaboom/);
});

test("createMcpHandler negotiates versions, skips notifications, handles errors & batches", async () => {
  const reg = new ToolRegistry().register({
    name: "ping_tool",
    description: "Ping",
    inputSchema: { type: "object", properties: {} },
    handler: () => ({ pong: true }),
  });
  const handler = createMcpHandler({ registry: reg, context: {} });

  const initModern = await handler.handlePayload({
    jsonrpc: "2.0",
    id: 1,
    method: "initialize",
    params: { protocolVersion: "2025-03-26" },
  });
  assert.equal(initModern.result.protocolVersion, "2025-03-26");

  const initFallback = await handler.handlePayload({
    jsonrpc: "2.0",
    id: 2,
    method: "initialize",
    params: { protocolVersion: "1999-01-01" },
  });
  assert.equal(initFallback.result.protocolVersion, "2024-11-05");

  const notif = await handler.handlePayload({
    jsonrpc: "2.0",
    method: "notifications/initialized",
  });
  assert.equal(notif, null);

  const missingMethod = await handler.handlePayload({
    jsonrpc: "2.0",
    id: 3,
    method: "nonexistent/method",
  });
  assert.equal(missingMethod.error.code, -32601);

  const batchReply = await handler.handlePayload([
    { jsonrpc: "2.0", method: "notifications/cancelled" },
    { jsonrpc: "2.0", id: 4, method: "ping" },
    { jsonrpc: "2.0", id: 5, method: "tools/call", params: { name: "ping_tool", arguments: {} } },
  ]);
  assert.equal(batchReply.length, 2);
  assert.equal(batchReply[0].id, 4);
  assert.deepEqual(batchReply[0].result, {});
  assert.equal(batchReply[1].id, 5);
  assert.deepEqual(JSON.parse(batchReply[1].result.content[0].text), { pong: true });
});

test("createStdioTransport supports NDJSON, Content-Length UTF-8, header ordering, and chunked arrival", async () => {
  // 1) NDJSON round-trip
  {
    const input = new PassThrough();
    const output = new PassThrough();
    const outChunks = [];
    output.on("data", (c) => outChunks.push(c));

    const reg = new ToolRegistry().register({
      name: "say",
      description: "Say",
      inputSchema: { type: "object", properties: { msg: { type: "string" } } },
      handler: (args) => ({ text: args.msg }),
    });
    const handler = createMcpHandler({ registry: reg, context: {} });
    const transport = createStdioTransport({
      input,
      output,
      onMessage: (msg) => handler.handlePayload(msg),
    });

    input.write(`${JSON.stringify({ jsonrpc: "2.0", id: 10, method: "tools/call", params: { name: "say", arguments: { msg: "你好🎨" } } })}\n`);
    await new Promise((r) => setTimeout(r, 20));
    transport.close();

    const textOut = Buffer.concat(outChunks).toString("utf-8");
    assert(textOut.endsWith("\n"));
    const parsed = JSON.parse(textOut.trim());
    assert.equal(parsed.id, 10);
    assert.deepEqual(JSON.parse(parsed.result.content[0].text), { text: "你好🎨" });
  }

  // 2) Content-Length framing with Content-Type before Content-Length, multi-byte UTF-8, tiny chunks
  {
    const input = new PassThrough();
    const output = new PassThrough();
    const outChunks = [];
    output.on("data", (c) => outChunks.push(c));

    const reg = new ToolRegistry().register({
      name: "utf8",
      description: "UTF8 echo",
      inputSchema: { type: "object", properties: { s: { type: "string" } } },
      handler: (args) => ({ s: args.s }),
    });
    const handler = createMcpHandler({ registry: reg, context: {} });
    const transport = createStdioTransport({
      input,
      output,
      onMessage: (msg) => handler.handlePayload(msg),
    });

    const body = JSON.stringify({
      jsonrpc: "2.0",
      id: 21,
      method: "tools/call",
      params: { name: "utf8", arguments: { s: "中文画板测试🎨" } },
    });
    const frame = Buffer.from(
      `Content-Type: application/vscode-jsonrpc; charset=utf-8\r\nContent-Length: ${Buffer.byteLength(body, "utf-8")}\r\n\r\n${body}`,
      "utf-8"
    );

    for (let i = 0; i < frame.length; i += 5) {
      input.write(frame.subarray(i, i + 5));
    }
    await new Promise((r) => setTimeout(r, 30));
    transport.close();

    const rawOut = Buffer.concat(outChunks);
    const sep = rawOut.indexOf("\r\n\r\n");
    assert(sep > 0, "Expected Content-Length header in reply");
    const hdr = rawOut.subarray(0, sep).toString("utf-8");
    const m = hdr.match(/Content-Length:\s*(\d+)/i);
    assert(m);
    const replyBody = rawOut.subarray(sep + 4);
    assert.equal(replyBody.length, Number(m[1]));
    const parsed = JSON.parse(replyBody.toString("utf-8"));
    assert.equal(parsed.id, 21);
    assert.deepEqual(JSON.parse(parsed.result.content[0].text), { s: "中文画板测试🎨" });
  }
});

test("startHttpTransport serves /health and /mcp JSON-RPC over localhost", async () => {
  const handler = createMcpHandler({
    registry: createDefaultRegistry(),
    context: {},
  });
  const server = await startHttpTransport({ handler, port: 0, host: "127.0.0.1" });

  try {
    const healthRes = await fetch(server.healthUrl);
    assert.equal(healthRes.status, 200);
    const healthJson = await healthRes.json();
    assert.equal(healthJson.ok, true);
    assert.equal(healthJson.transport, "streamable-http");

    const initRes = await fetch(server.url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        jsonrpc: "2.0",
        id: 1,
        method: "initialize",
        params: { protocolVersion: "2025-03-26" },
      }),
    });
    assert.equal(initRes.status, 200);
    const initJson = await initRes.json();
    assert.equal(initJson.result.serverInfo.name, "animean-backend-debug");

    const notifRes = await fetch(server.url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        jsonrpc: "2.0",
        method: "notifications/initialized",
      }),
    });
    assert.equal(notifRes.status, 202);

    const listRes = await fetch(server.url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        jsonrpc: "2.0",
        id: 2,
        method: "tools/list",
      }),
    });
    assert.equal(listRes.status, 200);
    const listJson = await listRes.json();
    assert.equal(listJson.result.tools.length, 14);

    const badRes = await fetch(server.url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: "{not-json",
    });
    assert.equal(badRes.status, 400);

    // A browser sends Origin on a page's cross-site POST and on the preflight:
    // both are refused and no CORS header lets the page read anything.
    const listBody = JSON.stringify({ jsonrpc: "2.0", id: 3, method: "tools/list" });
    const fromPage = await fetch(server.url, {
      method: "POST",
      headers: { "Content-Type": "application/json", Origin: "https://evil.example" },
      body: listBody,
    });
    assert.equal(fromPage.status, 403);
    assert.equal(fromPage.headers.get("access-control-allow-origin"), null);
    const preflight = await fetch(server.url, {
      method: "OPTIONS",
      headers: { Origin: "http://localhost:5173", "Access-Control-Request-Method": "POST" },
    });
    assert.equal(preflight.status, 403);
    assert.equal(preflight.headers.get("access-control-allow-origin"), null);
  } finally {
    await server.close();
  }
});

test("isLoopbackHost accepts loopback Host headers only (DNS rebinding guard)", () => {
  assert.equal(isLoopbackHost("127.0.0.1:19440"), true);
  assert.equal(isLoopbackHost("localhost:19440"), true);
  assert.equal(isLoopbackHost("[::1]:19440"), true);
  assert.equal(isLoopbackHost("LOCALHOST"), true);
  assert.equal(isLoopbackHost("evil.example:19440"), false);
  assert.equal(isLoopbackHost("127.0.0.1.evil.example"), false);
  assert.equal(isLoopbackHost(undefined), false);
});
