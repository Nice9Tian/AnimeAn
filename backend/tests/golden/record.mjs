import { spawn } from "node:child_process";
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import zlib from "node:zlib";
import { buildScenarios } from "./scenarios.mjs";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const REPO_ROOT = path.resolve(__dirname, "..", "..", "..");

function ensureCrlfGoldenFiles() {
  for (const name of ["record.mjs", "compare.mjs", "scenarios.mjs", "hook_recorder.py"]) {
    const filePath = path.join(__dirname, name);
    try {
      const raw = fs.readFileSync(filePath, "utf8");
      const crlf = raw.replace(/\r?\n/g, "\r\n");
      if (crlf !== raw) {
        fs.writeFileSync(filePath, crlf, "utf8");
      }
    } catch {
      // Ignore if read-only environment.
    }
  }
}

function parseArgs(argv) {
  let backendExe = "";
  let outFile = "";
  let onlyScenario = "";
  let dumpOpsDir = "";

  for (let i = 2; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === "--backend" && i + 1 < argv.length) {
      backendExe = argv[++i];
    } else if (arg === "--out" && i + 1 < argv.length) {
      outFile = argv[++i];
    } else if (arg === "--only" && i + 1 < argv.length) {
      onlyScenario = argv[++i];
    } else if (arg === "--dump-ops" && i + 1 < argv.length) {
      dumpOpsDir = argv[++i];
    } else {
      throw new Error(`Unknown or incomplete argument: ${arg}`);
    }
  }

  if (!backendExe) {
    throw new Error("Missing required argument: --backend <exe>");
  }
  if (!outFile) {
    throw new Error("Missing required argument: --out <file.jsonl>");
  }

  return {
    backendExe: path.resolve(backendExe),
    outFile: path.resolve(outFile),
    onlyScenario: onlyScenario.trim(),
    dumpOpsDir: dumpOpsDir ? path.resolve(dumpOpsDir) : "",
  };
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function crc32(buf) {
  let crc = 0xffffffff;
  for (let i = 0; i < buf.length; i++) {
    crc ^= buf[i];
    for (let bit = 0; bit < 8; bit++) {
      const mask = -(crc & 1);
      crc = (crc >>> 1) ^ (0xedb88320 & mask);
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function makePngChunk(typeStr, dataBuf) {
  const typeBuf = Buffer.from(typeStr, "ascii");
  const lenBuf = Buffer.alloc(4);
  lenBuf.writeUInt32BE(dataBuf.length, 0);
  const crcInput = Buffer.concat([typeBuf, dataBuf]);
  const crcBuf = Buffer.alloc(4);
  crcBuf.writeUInt32BE(crc32(crcInput), 0);
  return Buffer.concat([lenBuf, typeBuf, dataBuf, crcBuf]);
}

function writeDeterministicPng(filePath, width = 32, height = 32) {
  const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; // bit depth
  ihdr[9] = 6; // RGBA
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;

  const stride = width * 4 + 1;
  const raw = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const rowStart = y * stride;
    raw[rowStart] = 0; // filter: None
    for (let x = 0; x < width; x++) {
      const px = rowStart + 1 + x * 4;
      const border = x < 4 || x >= width - 4 || y < 4 || y >= height - 4;
      const diag = Math.abs(x - y) <= 2;
      if (border) {
        raw[px] = 30;
        raw[px + 1] = 60;
        raw[px + 2] = 180;
        raw[px + 3] = 255;
      } else if (diag) {
        raw[px] = 230;
        raw[px + 1] = 70;
        raw[px + 2] = 50;
        raw[px + 3] = 255;
      } else {
        raw[px] = 245;
        raw[px + 1] = 240;
        raw[px + 2] = 220;
        raw[px + 3] = 255;
      }
    }
  }

  const idatData = zlib.deflateSync(raw, { level: 9 });
  const pngBuf = Buffer.concat([
    signature,
    makePngChunk("IHDR", ihdr),
    makePngChunk("IDAT", idatData),
    makePngChunk("IEND", Buffer.alloc(0)),
  ]);
  fs.writeFileSync(filePath, pngBuf);
}

function createNormalizer({ tmpDir, repoRoot, backendDir, token, port }) {
  const mainRepoRoot = path.resolve(repoRoot, "..", "..", "..");
  const worktreesRoot = path.resolve(repoRoot, "..");

  const pathRules = [
    { prefix: tmpDir, token: "<TMP_DIR>" },
    { prefix: backendDir, token: "<BACKEND_DIR>" },
    { prefix: repoRoot, token: "<REPO_ROOT>" },
    { prefix: mainRepoRoot, token: "<REPO_ROOT>" },
  ];

  function normalizeString(str) {
    let out = str;
    // Unify worktree siblings (e.g. noqt-baseline vs noqt-golden) to <REPO_ROOT>.
    const wtSlash = worktreesRoot.replace(/\\/g, "/");
    const wtBack = worktreesRoot.replace(/\//g, "\\");
    const escapedSlash = wtSlash.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    const escapedBack = wtBack.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    out = out.replace(new RegExp(`${escapedSlash}/[^/\\s"']+`, "gi"), "<REPO_ROOT>");
    out = out.replace(new RegExp(`${escapedBack}\\\\[^\\\\\\s"']+`, "gi"), "<REPO_ROOT>");

    for (const rule of pathRules) {
      if (!rule.prefix) continue;
      const pSlash = rule.prefix.replace(/\\/g, "/");
      const pBack = rule.prefix.replace(/\//g, "\\");
      const escSlash = pSlash.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
      const escBack = pBack.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
      out = out.replace(new RegExp(escSlash, "gi"), rule.token);
      out = out.replace(new RegExp(escBack, "gi"), rule.token);
    }

    if (token) {
      out = out.split(token).join("<TOKEN>");
    }
    if (port) {
      out = out.replace(new RegExp(`127\\.0\\.0\\.1:${port}\\b`, "g"), "127.0.0.1:<PORT>");
      out = out.replace(new RegExp(`\\bport=${port}\\b`, "g"), "port=<PORT>");
    }

    out = out.replace(/\b0x[0-9a-fA-F]+\b/g, "0xADDR");
    // Normalize Windows backslashes following placeholder roots.
    out = out.replace(
      /(<(?:TMP_DIR|REPO_ROOT|BACKEND_DIR)>)([\\/][^\s"'<>|]*)/g,
      (_, rootTok, rest) => rootTok + rest.replace(/\\/g, "/")
    );
    return out;
  }

  function normalizeValue(val) {
    if (val === null || val === undefined) {
      return null;
    }
    if (typeof val === "boolean") {
      return val;
    }
    if (typeof val === "number") {
      if (!Number.isFinite(val)) {
        return String(val);
      }
      return Object.is(val, -0) || val === 0 ? 0 : Number(val.toFixed(6));
    }
    if (typeof val === "string") {
      return normalizeString(val);
    }
    if (Array.isArray(val)) {
      return val.map((item) => normalizeValue(item));
    }
    if (typeof val === "object") {
      const sorted = {};
      for (const key of Object.keys(val).sort()) {
        sorted[normalizeString(key)] = normalizeValue(val[key]);
      }
      return sorted;
    }
    return normalizeString(String(val));
  }

  return { normalizeValue, normalizeString };
}

function sha256Hex(input) {
  return crypto.createHash("sha256").update(input).digest("hex");
}

async function main() {
  ensureCrlfGoldenFiles();
  const { backendExe, outFile, onlyScenario, dumpOpsDir } = parseArgs(process.argv);
  if (!fs.existsSync(backendExe)) {
    throw new Error(`Backend executable not found: ${backendExe}`);
  }

  fs.mkdirSync(path.dirname(outFile), { recursive: true });
  if (dumpOpsDir) {
    fs.mkdirSync(dumpOpsDir, { recursive: true });
  }

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "animean-golden-"));
  const hookLogPath = path.join(tmpDir, "hooks.jsonl");
  fs.writeFileSync(hookLogPath, "", "utf8");

  const generatedRasterPng = path.join(tmpDir, "golden_raster_sample.png");
  writeDeterministicPng(generatedRasterPng, 32, 32);

  const token = "golden_tok_" + crypto.randomBytes(8).toString("hex");
  const backendDir = path.dirname(backendExe);
  // Hash seeds change the iteration order of QSet and of Python sets, and with it
  // the order of equally distant onion ghosts, from one start to the next.
  const env = { ...process.env, QT_HASH_SEED: "0", PYTHONHASHSEED: "0" };
  const extraPaths = [
    "C:/Qt/6.9.1/mingw_64/bin",
    "C:/Qt/Tools/mingw1310_64/bin",
    "C:/Users/admin/Documents/AnimeAn/tools/python312",
    backendDir,
  ].join(path.delimiter);
  env.PATH = `${extraPaths}${path.delimiter}${env.PATH || ""}`;
  env.ANIMEAN_GOLDEN_HOOK_LOG = hookLogPath;
  env.ANIMEAN_GOLDEN_TMP_DIR = tmpDir;

  const child = spawn(backendExe, ["--port", "0", "--token", token], {
    env,
    stdio: ["ignore", "pipe", "pipe"],
  });

  let stderrLog = "";
  child.stderr.on("data", (chunk) => {
    stderrLog += chunk.toString("utf8");
  });

  let stdoutBuf = "";
  const port = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      reject(new Error(`Timed out waiting for ANIMEAN_BACKEND_PORT. stdout:\n${stdoutBuf}\nstderr:\n${stderrLog}`));
    }, 20000);

    child.stdout.on("data", (chunk) => {
      stdoutBuf += chunk.toString("utf8");
      const match = stdoutBuf.match(/ANIMEAN_BACKEND_PORT=(\d+)/);
      if (match) {
        clearTimeout(timer);
        resolve(parseInt(match[1], 10));
      }
    });

    child.on("exit", (code) => {
      clearTimeout(timer);
      reject(new Error(`Backend exited early with code ${code}. stderr:\n${stderrLog}`));
    });
  });

  const { normalizeValue } = createNormalizer({
    tmpDir,
    repoRoot: REPO_ROOT,
    backendDir,
    token,
    port,
  });

  const wsUrl = `ws://127.0.0.1:${port}/?token=${token}`;
  const ws = new WebSocket(wsUrl);
  ws.binaryType = "arraybuffer";

  const latestStates = new Map();
  let stepStates = {};
  let stepEvents = [];
  let stepImages = [];
  const pendingImageHeaders = [];
  const pendingCalls = new Map();
  let activity = 0;

  const boards = {
    main: { w: 0, h: 0, ops: [] },
    child: { w: 0, h: 0, ops: [] },
  };

  ws.addEventListener("message", (event) => {
    activity += 1;
    if (typeof event.data !== "string") {
      const buf = Buffer.from(event.data);
      const hdr = pendingImageHeaders.shift();
      if (hdr) {
        stepImages.push({
          key: hdr.key,
          w: hdr.w,
          h: hdr.h,
          sha256: sha256Hex(buf),
        });
      }
      return;
    }

    let msg;
    try {
      msg = JSON.parse(event.data);
    } catch {
      return;
    }

    if (msg.t === "state") {
      latestStates.set(msg.key, msg.value);
      if (msg.key === "event") {
        stepEvents.push(msg.value);
      } else {
        stepStates[msg.key] = msg.value;
      }
    } else if (msg.t === "board") {
      const view = msg.view === "child" ? "child" : "main";
      const b = boards[view];
      b.w = msg.w;
      b.h = msg.h;
      const keep = Math.max(0, Math.min(b.ops.length, msg.keep ?? 0));
      b.ops.length = keep;
      if (Array.isArray(msg.ops)) {
        for (const op of msg.ops) {
          b.ops.push(op);
        }
      }
      ws.send(JSON.stringify({ t: "ack", view: msg.view, frame: msg.frame }));
    } else if (msg.t === "image") {
      pendingImageHeaders.push({
        key: msg.key,
        w: msg.w,
        h: msg.h,
        bytes: msg.bytes,
      });
    } else if (msg.t === "reply") {
      const waiter = pendingCalls.get(msg.id);
      if (waiter) {
        pendingCalls.delete(msg.id);
        waiter.resolve(msg);
      }
    }
  });

  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("WebSocket open timeout")), 10000);
    ws.addEventListener("open", () => {
      clearTimeout(timer);
      resolve();
    });
    ws.addEventListener("error", (err) => {
      clearTimeout(timer);
      reject(err);
    });
  });

  // The backend is single threaded, so a reply proves it finished everything
  // sent before the call. A time window alone is not enough: an Auto Mapping
  // rerun can stay silent for longer than any window and land in the next step.
  async function waitForQuiet(quietMs = 70, maxWaitMs = 60000) {
    const start = Date.now();
    while (Date.now() - start < maxWaitMs) {
      const seen = activity;
      await rawCall("golden.ping");
      await sleep(quietMs);
      await rawCall("golden.ping");
      if (activity === seen + 2 && pendingImageHeaders.length === 0) {
        return;
      }
    }
    throw new Error("Timed out waiting for backend WebSocket quiet period");
  }

  let nextCallId = 1;
  function rawCall(method, params = {}) {
    const id = nextCallId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        pendingCalls.delete(id);
        reject(new Error(`Timeout waiting for reply to ${method} (id=${id})`));
      }, 30000);
      pendingCalls.set(id, {
        resolve: (msg) => {
          clearTimeout(timer);
          resolve(msg);
        },
      });
      ws.send(JSON.stringify({ t: "call", id, method, params }));
    });
  }

  let hookByteOffset = 0;
  function consumeRecordedHooks() {
    if (!fs.existsSync(hookLogPath)) {
      return [];
    }
    const buf = fs.readFileSync(hookLogPath);
    if (buf.length <= hookByteOffset) {
      return [];
    }
    const slice = buf.subarray(hookByteOffset).toString("utf8");
    hookByteOffset = buf.length;
    const lines = slice
      .split(/\r?\n/)
      .map((l) => l.trim())
      .filter((l) => l.length > 0);
    return lines.map((line) => normalizeValue(JSON.parse(line)));
  }

  function clearStepBuffers() {
    stepStates = {};
    stepEvents = [];
    stepImages = [];
    consumeRecordedHooks();
  }

  let globalStepCounter = 0;
  const recordedLines = [];

  function snapshotStep(scenarioName, stepIndex, stepName, actionDesc, replyObj) {
    const stateOut = {};
    for (const key of Object.keys(stepStates).sort()) {
      stateOut[key] = normalizeValue(stepStates[key]);
    }
    if (stepEvents.length === 1) {
      stateOut.event = normalizeValue(stepEvents[0]);
    } else if (stepEvents.length > 1) {
      stateOut.event = normalizeValue(stepEvents);
    }

    const boardsOut = {};
    for (const view of ["main", "child"]) {
      const normalizedOps = normalizeValue(boards[view].ops);
      const opsCanonicalJson = JSON.stringify(normalizedOps);
      boardsOut[view] = {
        w: boards[view].w,
        h: boards[view].h,
        count: normalizedOps.length,
        sha256: sha256Hex(opsCanonicalJson),
      };
      if (dumpOpsDir) {
        const dumpName = `${String(globalStepCounter).padStart(4, "0")}_${scenarioName}_${String(stepIndex).padStart(2, "0")}_${stepName}_${view}.json`;
        fs.writeFileSync(
          path.join(dumpOpsDir, dumpName),
          JSON.stringify(
            {
              scenario: scenarioName,
              step: stepIndex,
              name: stepName,
              view,
              w: boards[view].w,
              h: boards[view].h,
              count: normalizedOps.length,
              ops: normalizedOps,
            },
            null,
            2
          ) + "\r\n",
          "utf8"
        );
      }
    }

    const hooksOut = consumeRecordedHooks();
    const imagesOut = stepImages.map((img) => ({
      key: img.key,
      w: img.w,
      h: img.h,
      sha256: img.sha256,
    }));

    const record = {
      scenario: scenarioName,
      step: stepIndex,
      name: stepName,
      action: normalizeValue(actionDesc),
      reply: replyObj
        ? {
            ok: Boolean(replyObj.ok),
            result: normalizeValue(replyObj.result ?? null),
            error: normalizeValue(replyObj.error ?? null),
          }
        : null,
      state: stateOut,
      boards: boardsOut,
      images: imagesOut,
      hooks: hooksOut,
    };

    recordedLines.push(JSON.stringify(record));
    globalStepCounter++;
    clearStepBuffers();
  }

  let initialThemeMode = "dark";
  let initialTimelineLayout = null;

  try {
    // Wait for initial connection burst (all 20 state keys + initial main/child boards).
    await waitForQuiet(120);

    initialThemeMode = latestStates.get("theme")?.mode || "dark";
    const winTimeline = latestStates.get("windows")?.timeline;
    if (winTimeline) {
      initialTimelineLayout = {
        visible: Boolean(winTimeline.visible),
        collapsed: Boolean(winTimeline.collapsed),
        floating: Boolean(winTimeline.floating),
        dockArea: String(winTimeline.dockArea || "bottom"),
      };
    }

    // Standardize persistent QSettings state (theme & timeline layout) before recording
    // so even if a machine had a non-default preference, startup and all scenarios match.
    if (initialThemeMode !== "dark") {
      await rawCall("theme.set", { mode: "dark" });
      await waitForQuiet(60);
    }
    if (
      initialTimelineLayout &&
      (!initialTimelineLayout.visible ||
        initialTimelineLayout.collapsed ||
        initialTimelineLayout.floating ||
        initialTimelineLayout.dockArea !== "bottom")
    ) {
      await rawCall("timeline.setLayout", {
        visible: true,
        collapsed: false,
        floating: false,
        dockArea: "bottom",
      });
      await waitForQuiet(60);
    }

    // Ensure stepStates holds the full 20 initial state keys for the startup snapshot.
    for (const [k, v] of latestStates.entries()) {
      if (k !== "event") {
        stepStates[k] = v;
      }
    }

    const scenarios = buildScenarios({
      tmpDir: tmpDir.replace(/\\/g, "/"),
      repoRoot: REPO_ROOT.replace(/\\/g, "/"),
    });

    const filteredScenarios = onlyScenario
      ? scenarios.filter((s) => s.name === onlyScenario)
      : scenarios;

    if (filteredScenarios.length === 0) {
      throw new Error(`No scenario matched --only ${onlyScenario}`);
    }

    for (const s of filteredScenarios) {
      if (s.isStartup) {
        snapshotStep(
          "startup",
          0,
          "connect_initial_state",
          { kind: "connect" },
          null
        );
      }
    }

    // Install hook_recorder.py via python.run (also disables os.startfile).
    const hookRecorderPath = path.join(__dirname, "hook_recorder.py").replace(/\\/g, "/");
    const hookInstallReply = await rawCall("python.run", { command: hookRecorderPath });
    if (!hookInstallReply.ok) {
      throw new Error(`Failed to install hook_recorder.py: ${JSON.stringify(hookInstallReply)}`);
    }
    await waitForQuiet(80);

    // Save a clean blank project so every scenario starts from an identical clean slate.
    const blankProjectPath = path.join(tmpDir, "_clean_blank.anproj").replace(/\\/g, "/");
    await rawCall("project.saveAs", { path: blankProjectPath });
    await waitForQuiet(80);
    clearStepBuffers();

    async function resetScenarioBaseline() {
      await rawCall("theme.set", { mode: "dark" });
      await rawCall("project.open", { path: blankProjectPath });
      await rawCall("view.setActive", { view: "main" });
      await rawCall("board.resetView", { view: "main" });
      await rawCall("board.resetView", { view: "child" });
      await rawCall("menu.trigger", { id: "child.setting.background.white" });
      await rawCall("tool.select", { tool: "Pen" });
      await rawCall("color.set", { r: 0, g: 0, b: 0, a: 255 });
      await rawCall("draw.setPenWidth", { width: 2 });
      await rawCall("draw.setSmooth", { value: 35 });
      await rawCall("draw.setSettings", { simplify: 50, corner: 50 });
      await rawCall("fill.setScope", { scope: "current" });
      await rawCall("onion.setEnabled", { enabled: false });
      await rawCall("onion.setLines", { enabled: true });
      await rawCall("onion.setFills", { enabled: true });
      await rawCall("onion.setAmLayers", { enabled: true });
      await rawCall("onion.setGuideLines", { enabled: false });
      await waitForQuiet(100);
      clearStepBuffers();
    }

    let logicalTime = 1000;

    for (const scenario of filteredScenarios) {
      if (scenario.isStartup) {
        continue;
      }

      await resetScenarioBaseline();

      for (let stepIdx = 0; stepIdx < scenario.steps.length; stepIdx++) {
        const step = scenario.steps[stepIdx];
        clearStepBuffers();

        if (step.kind === "call") {
          const reply = await rawCall(step.method, step.params);
          await waitForQuiet(70);
          snapshotStep(
            scenario.name,
            stepIdx,
            step.name,
            { kind: "call", method: step.method, params: step.params },
            reply
          );
        } else if (step.kind === "inputs") {
          const sentEvents = [];
          for (const ev of step.events) {
            logicalTime += 40;
            const wireMsg = {
              t: "input",
              view: ev.view || "main",
              kind: ev.kind,
            };
            if (ev.x !== undefined) wireMsg.x = ev.x;
            if (ev.y !== undefined) wireMsg.y = ev.y;
            if (ev.dx !== undefined) wireMsg.dx = ev.dx;
            if (ev.dy !== undefined) wireMsg.dy = ev.dy;
            if (ev.w !== undefined) wireMsg.w = ev.w;
            if (ev.h !== undefined) wireMsg.h = ev.h;
            if (ev.dpr !== undefined) wireMsg.dpr = ev.dpr;
            if (ev.button !== undefined) wireMsg.button = ev.button;
            if (ev.buttons !== undefined) wireMsg.buttons = ev.buttons;
            if (ev.modifiers !== undefined) wireMsg.modifiers = ev.modifiers;
            if (ev.pressure !== undefined) wireMsg.pressure = ev.pressure;
            wireMsg.time = logicalTime;

            ws.send(JSON.stringify(wireMsg));
            sentEvents.push({
              view: wireMsg.view,
              kind: wireMsg.kind,
              x: wireMsg.x,
              y: wireMsg.y,
              w: wireMsg.w,
              h: wireMsg.h,
              dpr: wireMsg.dpr,
              button: wireMsg.button,
              buttons: wireMsg.buttons,
              modifiers: wireMsg.modifiers,
            });

            const waitAfter = ev.waitAfterMs ?? 45;
            if (waitAfter > 0) {
              await sleep(waitAfter);
            }
          }
          await waitForQuiet(70);
          snapshotStep(
            scenario.name,
            stepIdx,
            step.name,
            { kind: "inputs", count: sentEvents.length, events: sentEvents },
            null
          );
        } else {
          throw new Error(`Unsupported step kind: ${step.kind}`);
        }
      }
    }

    fs.writeFileSync(outFile, recordedLines.join("\r\n") + "\r\n", "utf8");
  } finally {
    try {
      if (ws.readyState === WebSocket.OPEN) {
        await rawCall("theme.set", { mode: initialThemeMode });
        if (initialTimelineLayout) {
          await rawCall("timeline.setLayout", initialTimelineLayout);
        }
        await waitForQuiet(50);
        ws.close();
      }
    } catch {
      // Ignore cleanup errors if socket already closed.
    }
    try {
      child.kill();
    } catch {
      // Ignore kill errors.
    }
    try {
      fs.rmSync(tmpDir, { recursive: true, force: true });
    } catch {
      // Ignore temp removal errors.
    }
  }
}

main().catch((err) => {
  console.error("GOLDEN_RECORD_FAILED:", err && err.stack ? err.stack : err);
  process.exit(1);
});
