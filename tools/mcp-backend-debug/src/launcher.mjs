import { spawn, spawnSync } from "node:child_process";
import { randomBytes } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import {
  buildBackendEnv,
  clearSessionMetadata,
  DEFAULT_DEV_PORT,
  DEFAULT_FRONTEND_PORT,
  getToolkitPaths,
  isPidAlive,
  isTcpPortOpen,
  locateBackendExecutable,
  readActiveSessionFile,
  writeSessionMetadata,
} from "./discovery.mjs";
import { allocatePorts, assertPortsAllowed, resolveListenHost, urlHost } from "./ports.mjs";

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function resolveViteRoot(repoRoot) {
  const { mainRepoRoot } = getToolkitPaths(repoRoot);
  const worktreeWebui = path.join(repoRoot, "webui");
  if (fs.existsSync(path.join(worktreeWebui, "node_modules"))) {
    return worktreeWebui;
  }
  const mainWebui = path.join(mainRepoRoot, "webui");
  if (fs.existsSync(path.join(mainWebui, "node_modules"))) {
    return mainWebui;
  }
  return worktreeWebui;
}

/**
 * A backend left running from a packaged `dist\AnimeAn` folder locks files
 * that `agent_build.ps1` has to replace, so the next rebuild fails. A packaged
 * backend is therefore mirrored into the worktree's `.scratch` and run from
 * there; it still loads the `pyfile\` of the checkout it was built from.
 * Build-tree executables run in place.
 * @param {string} exePath
 * @param {string} scratchDir
 * @returns {{ exePath: string, copiedFrom: string | null, copyWarning: string | null }}
 */
function stageBackendExecutable(exePath, scratchDir) {
  const exeDir = path.dirname(exePath);
  const isPackaged =
    path.basename(exeDir).toLowerCase() === "animean" &&
    path.basename(path.dirname(exeDir)).toLowerCase() === "dist";
  if (!isPackaged) {
    return { exePath, copiedFrom: null, copyWarning: null };
  }
  const targetDir = path.join(scratchDir, "dev_backend_dist", "AnimeAn");
  const targetExe = path.join(targetDir, path.basename(exePath));
  fs.mkdirSync(targetDir, { recursive: true });
  let copyError = null;
  if (process.platform === "win32") {
    // Only changed files are copied; /R:1 /W:1 so a locked file fails fast.
    const res = spawnSync(
      "robocopy",
      [exeDir, targetDir, "/MIR", "/R:1", "/W:1", "/NFL", "/NDL", "/NJH", "/NJS", "/NP"],
      { encoding: "utf-8", windowsHide: true }
    );
    // robocopy exit codes 0-7 mean success; 8 and above mean some copy failed.
    if (res.error || res.status === null || res.status >= 8) {
      copyError = (res.error && res.error.message) || (res.stdout || "").trim().slice(-1000) || `exit ${res.status}`;
    }
  } else {
    try {
      fs.cpSync(exeDir, targetDir, { recursive: true, force: true });
    } catch (err) {
      copyError = err instanceof Error ? err.message : String(err);
    }
  }
  if (copyError) {
    if (!fs.existsSync(targetExe)) {
      throw new Error(`Could not copy ${exeDir} to ${targetDir}: ${copyError}`);
    }
    // Usually an older backend still runs from the copy and holds its files.
    return {
      exePath: targetExe,
      copiedFrom: exeDir,
      copyWarning: `Copy of ${exeDir} is incomplete (${copyError}); running the existing copy.`,
    };
  }
  return { exePath: targetExe, copiedFrom: exeDir, copyWarning: null };
}

function tokenFromWsUrl(wsUrl) {
  try {
    return new URL(wsUrl).searchParams.get("token");
  } catch {
    return null;
  }
}

async function waitForBackendPort(child, logPath, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (fs.existsSync(logPath)) {
      const content = fs.readFileSync(logPath, "utf-8");
      const match = content.match(/ANIMEAN_BACKEND_PORT=(\d+)/);
      if (match) {
        return Number(match[1]);
      }
    }
    if (child.exitCode !== null) {
      const logContent = fs.existsSync(logPath) ? fs.readFileSync(logPath, "utf-8") : "";
      throw new Error(
        `AnimeAnBackend.exe exited early with code ${child.exitCode}.\n${logContent.slice(-2000)}`
      );
    }
    await sleep(60);
  }
  const logContent = fs.existsSync(logPath) ? fs.readFileSync(logPath, "utf-8") : "";
  throw new Error(`Timed out waiting for ANIMEAN_BACKEND_PORT in ${logPath}.\n${logContent.slice(-2000)}`);
}

async function waitForPortOpen(host, port, timeoutMs = 12000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await isTcpPortOpen(host, port, 300)) {
      return true;
    }
    await sleep(100);
  }
  return false;
}

/**
 * Open a URL in Google Chrome (falling back to default Windows browser).
 * @param {string} url
 */
export function openInChrome(url) {
  if (process.platform === "win32") {
    const chromeCandidates = [
      path.join(process.env["ProgramFiles"] || "C:\\Program Files", "Google\\Chrome\\Application\\chrome.exe"),
      path.join(
        process.env["ProgramFiles(x86)"] || "C:\\Program Files (x86)",
        "Google\\Chrome\\Application\\chrome.exe"
      ),
      path.join(process.env.LOCALAPPDATA || "", "Google\\Chrome\\Application\\chrome.exe"),
    ].filter(Boolean);
    const chromeExe = chromeCandidates.find((p) => fs.existsSync(p));
    if (chromeExe) {
      const proc = spawn(chromeExe, [url], { detached: true, stdio: "ignore", windowsHide: false });
      proc.unref();
      return { launcher: chromeExe };
    }
    const proc = spawn("cmd.exe", ["/d", "/s", "/c", "start", "", url], {
      detached: true,
      stdio: "ignore",
      windowsHide: true,
    });
    proc.unref();
    return { launcher: "cmd-start" };
  }
  const cmd = process.platform === "darwin" ? "open" : "xdg-open";
  const proc = spawn(cmd, [url], { detached: true, stdio: "ignore" });
  proc.unref();
  return { launcher: cmd };
}

// Path under which Vite proxies the backend WebSocket when the page is served
// on another address; webui/vite.config.ts uses the same path.
const BACKEND_PROXY_PATH = "/__backend";

function failIfTaken(isOpen, host, port, what) {
  if (isOpen) {
    throw new Error(
      `${what} port ${host}:${port} is already in use by a process this session did not start. ` +
        "Pick another port or pass --auto."
    );
  }
}

/** @returns {{ target: string, pid: number, stopped: boolean, error?: string }} */
function killPid(label, pid) {
  try {
    process.kill(pid);
    return { target: label, pid, stopped: true };
  } catch (err) {
    return { target: label, pid, stopped: false, error: err instanceof Error ? err.message : String(err) };
  }
}

async function waitForPortClosed(host, port, timeoutMs = 5000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline && (await isTcpPortOpen(host, port, 200))) {
    await sleep(100);
  }
}

/**
 * Where Vite serves the page: the address asked for, else (like the ports) the
 * live session's, so a later start without --host does not pull the page away
 * from the browser on another machine.
 */
function chooseHost(live, options, startVite) {
  if (options.host !== undefined) return resolveListenHost(options.host);
  const explicitPorts = options.port !== undefined || options.frontendPort !== undefined;
  return startVite && live && !explicitPorts ? (live.frontendHost ?? null) : null;
}

/**
 * Backend and frontend ports: the live session's unless ports were asked for,
 * then (with auto) fresh ones from the port gate, then the ones asked for or
 * the defaults, which the port gate must allow.
 */
async function choosePorts(live, options, startVite) {
  const explicit = options.port !== undefined || options.frontendPort !== undefined;
  if (live && !explicit && live.backendPort && live.frontendPort) {
    return { backendPort: live.backendPort, frontendPort: live.frontendPort, portSource: "session" };
  }
  if (options.auto) {
    const { ports, source } = await allocatePorts(2);
    return { backendPort: ports[0], frontendPort: ports[1], portSource: source };
  }
  const backendPort = options.port !== undefined ? Number(options.port) : DEFAULT_DEV_PORT;
  const frontendPort = options.frontendPort !== undefined ? Number(options.frontendPort) : DEFAULT_FRONTEND_PORT;
  // Without Vite the frontend port is never opened.
  assertPortsAllowed(startVite ? [backendPort, frontendPort] : [backendPort]);
  return { backendPort, frontendPort, portSource: "options" };
}

/**
 * Reuse the backend of the live session when it still answers, otherwise
 * spawn a fresh one.
 */
async function startOrReuseBackend(live, paths, { requestedPort, token, backendExe: explicitExe }) {
  if (live) {
    try {
      const livePort = Number(new URL(live.wsUrl).port);
      if (livePort > 0 && (await isTcpPortOpen("127.0.0.1", livePort, 400))) {
        return {
          backendPid: live.backendPid,
          backendPort: livePort,
          wsUrl: live.wsUrl,
          backendExe: live.backendExe || null,
          token: live.token || tokenFromWsUrl(live.wsUrl) || token,
          backendReused: true,
        };
      }
    } catch {
      // Fall through to fresh spawn
    }
  }
  if (requestedPort > 0) {
    failIfTaken(await isTcpPortOpen("127.0.0.1", requestedPort, 300), "127.0.0.1", requestedPort, "Backend");
  }

  const located = locateBackendExecutable({ explicitExe, repoRoot: paths.repoRoot });
  if (!located) {
    throw new Error(
      "Could not locate AnimeAnBackend.exe. Build first or pass --backend-exe / ANIMEAN_BACKEND_EXE."
    );
  }
  fs.mkdirSync(paths.scratchDir, { recursive: true });
  const staged = stageBackendExecutable(located, paths.scratchDir);
  const backendLogPath = path.join(paths.scratchDir, "backend_dev.log");
  const logFd = fs.openSync(backendLogPath, "w");
  const proc = spawn(staged.exePath, ["--port", String(requestedPort), "--token", token], {
    cwd: path.dirname(staged.exePath),
    env: buildBackendEnv(staged.exePath, paths.repoRoot),
    detached: true,
    stdio: ["ignore", logFd, logFd],
    windowsHide: true,
  });
  fs.closeSync(logFd);

  const backendPort = await waitForBackendPort(proc, backendLogPath, 15000);
  proc.unref();
  return {
    backendPid: proc.pid,
    backendPort,
    wsUrl: `ws://127.0.0.1:${backendPort}/?token=${encodeURIComponent(token)}`,
    backendExe: staged.exePath,
    token,
    backendReused: false,
    ...(staged.copiedFrom ? { backendCopiedFrom: staged.copiedFrom } : {}),
    ...(staged.copyWarning ? { backendCopyWarning: staged.copyWarning } : {}),
  };
}

/** The Vite this session recorded, while it still runs; it may outlive its backend. */
function recordedVite(recorded) {
  if (!recorded?.vitePid || !isPidAlive(recorded.vitePid)) return null;
  return {
    vitePid: recorded.vitePid,
    viteRoot: recorded.viteRoot || null,
    frontendPort: recorded.frontendPort,
    frontendHost: recorded.frontendHost ?? null,
    viteBackendPort: recorded.viteBackendPort ?? null,
  };
}

/**
 * Reuse the Vite this session started when it serves the same address and
 * port (and, with a host, proxies the same backend). A recorded Vite that does
 * not match is stopped first, so no Vite is left running unrecorded. With a
 * host, Vite listens there instead of localhost and proxies the backend.
 */
async function startOrReuseVite(recorded, paths, { frontendPort, host, backendPort }) {
  const listenHost = host || "localhost";
  const viteBackendPort = host ? backendPort : null;
  const old = recordedVite(recorded);
  if (old) {
    const same = old.frontendPort === frontendPort && old.frontendHost === host && old.viteBackendPort === viteBackendPort;
    if (same && (await isTcpPortOpen(listenHost, frontendPort, 300))) {
      const { frontendPort: _port, ...kept } = old;
      return { ...kept, viteReused: true, viteReady: true };
    }
    killPid("vite", old.vitePid);
    await waitForPortClosed(old.frontendHost || "localhost", old.frontendPort);
  }
  failIfTaken(await isTcpPortOpen(listenHost, frontendPort, 300), listenHost, frontendPort, "Frontend");

  const viteRoot = resolveViteRoot(paths.repoRoot);
  const viteScript = path.join(viteRoot, "node_modules", "vite", "bin", "vite.js");
  if (!fs.existsSync(viteScript)) {
    throw new Error(`Vite was not found at ${viteScript}. Run 'npm install' in webui/ or pass --no-vite.`);
  }
  fs.mkdirSync(paths.scratchDir, { recursive: true });
  const viteLogPath = path.join(paths.scratchDir, "vite_dev.log");
  const viteFd = fs.openSync(viteLogPath, "w");
  const args = [viteScript, "--port", String(frontendPort), "--strictPort", ...(host ? ["--host", host] : [])];
  // vite.config.ts turns the backend proxy on only when these are set.
  const proxyEnv = host ? { ANIMEAN_DEV_HOST: host, ANIMEAN_DEV_BACKEND_PORT: String(backendPort) } : {};
  const viteProc = spawn(process.execPath, args, {
    cwd: viteRoot,
    env: { ...process.env, ...proxyEnv },
    detached: true,
    stdio: ["ignore", viteFd, viteFd],
    windowsHide: true,
  });
  fs.closeSync(viteFd);
  viteProc.unref();
  if (!(await waitForPortOpen(listenHost, frontendPort, 12000))) {
    const vlog = fs.existsSync(viteLogPath) ? fs.readFileSync(viteLogPath, "utf-8") : "";
    throw new Error(`Vite did not listen on ${listenHost}:${frontendPort}.\n${vlog.slice(-2000)}`);
  }
  return {
    vitePid: viteProc.pid,
    viteRoot,
    frontendHost: host,
    viteBackendPort,
    viteReused: false,
    viteReady: true,
  };
}

/**
 * The Vite part of the session. Without Vite (--no-vite) a Vite this session
 * already runs is kept and stays recorded, with its own port.
 */
async function viteForSession(recorded, paths, { startVite, frontendPort, host, backendPort }) {
  if (startVite) {
    const { viteReused, viteReady, ...vite } = await startOrReuseVite(recorded, paths, { frontendPort, host, backendPort });
    return { vite, frontendPort, viteReused, viteReady };
  }
  const kept = recordedVite(recorded);
  if (!kept) {
    const none = { vitePid: null, viteRoot: null, frontendHost: null, viteBackendPort: null };
    return { vite: none, frontendPort, viteReused: false, viteReady: false };
  }
  const { frontendPort: keptPort, ...vite } = kept;
  return { vite, frontendPort: keptPort, viteReused: true, viteReady: true };
}

function pageUrls(host, frontendPort, backend) {
  const pageHost = urlHost(host || "localhost");
  const pageWsUrl = host
    ? `ws://${pageHost}:${frontendPort}${BACKEND_PROXY_PATH}/?token=${encodeURIComponent(backend.token)}`
    : backend.wsUrl;
  return { chromeUrl: `http://${pageHost}:${frontendPort}/?ws=${encodeURIComponent(pageWsUrl)}` };
}

/**
 * Start or reuse a managed AnimeAnBackend.exe + Vite dev environment for Chrome testing.
 *
 * options.auto: take both ports from the port gate (see ports.mjs).
 * options.host: serve the page on this address ("tailscale" for this
 * machine's Tailscale IP) so a browser on another machine can open it. The
 * backend stays on 127.0.0.1 and the page reaches it through Vite's proxy.
 */
export async function startDevEnvironment(options = {}) {
  const paths = getToolkitPaths(options.repoRoot);
  // A fresh random token per launch: the backend accepts any http://localhost
  // page origin and python.run executes code, so a well-known token would let
  // any local web page in.
  const freshToken = String(options.token || randomBytes(16).toString("hex"));
  const startVite = options.startVite !== false;
  if (options.host && !startVite) {
    throw new Error("--host serves the page from Vite; it cannot be combined with --no-vite.");
  }

  if (options.restart) {
    await stopDevEnvironment({ repoRoot: paths.repoRoot });
  }

  // The recorded session, even when its backend has died: its Vite may still run.
  const recorded = readActiveSessionFile(paths.repoRoot, { requireLiveBackend: false });
  const live = recorded && isPidAlive(recorded.backendPid) ? recorded : null;
  const host = chooseHost(live, options, startVite);
  const { backendPort: requestedPort, frontendPort, portSource } = await choosePorts(live, options, startVite);
  const { backendCopiedFrom, backendCopyWarning, backendReused, ...backend } = await startOrReuseBackend(
    live,
    paths,
    { requestedPort, token: freshToken, backendExe: options.backendExe }
  );
  const { vite, viteReused, viteReady, ...front } = await viteForSession(recorded, paths, {
    startVite,
    frontendPort,
    host,
    backendPort: backend.backendPort,
  });

  const meta = {
    startedAt: live?.startedAt && backendReused ? live.startedAt : new Date().toISOString(),
    updatedAt: new Date().toISOString(),
    repoRoot: paths.repoRoot,
    ...backend,
    frontendPort: front.frontendPort,
    ...vite,
  };
  Object.assign(meta, pageUrls(meta.frontendHost, meta.frontendPort, backend));
  const sessionFiles = writeSessionMetadata(meta, paths.repoRoot);
  const browserLaunch = options.openBrowser ? openInChrome(meta.chromeUrl) : null;

  return {
    ...meta,
    ...(backendCopiedFrom ? { backendCopiedFrom } : {}),
    ...(backendCopyWarning ? { backendCopyWarning } : {}),
    portSource,
    backendReused,
    viteReused,
    viteReady,
    sessionFiles,
    browserLaunch,
  };
}

/**
 * Stop managed dev backend and Vite processes recorded in the session files,
 * also when the backend has already died and only Vite is left.
 */
export async function stopDevEnvironment(options = {}) {
  const paths = getToolkitPaths(options.repoRoot);
  const recorded = readActiveSessionFile(paths.repoRoot, { requireLiveBackend: false });
  const stopped = [];
  for (const [label, pid] of [
    ["backend", recorded?.backendPid],
    ["vite", recorded?.vitePid],
  ]) {
    if (pid && isPidAlive(pid)) {
      stopped.push(killPid(label, pid));
    }
  }
  clearSessionMetadata(paths.repoRoot);
  return {
    hadSession: Boolean(recorded),
    stopped,
  };
}
