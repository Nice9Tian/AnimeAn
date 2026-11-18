import { spawn, spawnSync } from "node:child_process";
import { randomBytes } from "node:crypto";
import fs from "node:fs";
import net from "node:net";
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

/**
 * Backend and frontend ports: the running session's when it is still alive,
 * then the ones asked for, then (with auto) fresh ones from the port gate.
 */
async function choosePorts(existing, options, startVite) {
  const sessionAlive = Boolean(existing && isPidAlive(existing.backendPid));
  if (options.auto) {
    if (sessionAlive && existing.backendPort && existing.frontendPort) {
      return { backendPort: existing.backendPort, frontendPort: existing.frontendPort, portSource: "session" };
    }
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
 * Reuse the backend recorded in the session file when it still answers,
 * otherwise spawn a fresh one.
 */
async function startOrReuseBackend(existing, paths, { requestedPort, token, backendExe: explicitExe }) {
  if (existing && isPidAlive(existing.backendPid)) {
    try {
      const existingPort = Number(new URL(existing.wsUrl).port);
      if (existingPort > 0 && (await isTcpPortOpen("127.0.0.1", existingPort, 400))) {
        return {
          backendPid: existing.backendPid,
          backendPort: existingPort,
          wsUrl: existing.wsUrl,
          backendExe: existing.backendExe || null,
          token: existing.token || tokenFromWsUrl(existing.wsUrl) || token,
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

/**
 * Reuse the Vite this session started when it still listens for the same
 * backend on the same address, otherwise spawn one. With a host, Vite listens
 * there instead of localhost and proxies the backend.
 */
async function startOrReuseVite(existing, paths, { frontendPort, host, backendPort }) {
  const listenHost = host || "localhost";
  const portOpen = await isTcpPortOpen(listenHost, frontendPort, 300);
  const ours =
    existing?.vitePid &&
    isPidAlive(existing.vitePid) &&
    existing.frontendPort === frontendPort &&
    (existing.frontendHost ?? null) === host &&
    existing.viteBackendPort === backendPort;
  if (portOpen && ours) {
    return {
      vitePid: existing.vitePid,
      viteRoot: existing.viteRoot || null,
      viteBackendPort: backendPort,
      viteReused: true,
      viteReady: true,
    };
  }
  failIfTaken(portOpen, listenHost, frontendPort, "Frontend");

  const viteRoot = resolveViteRoot(paths.repoRoot);
  const viteScript = path.join(viteRoot, "node_modules", "vite", "bin", "vite.js");
  if (!fs.existsSync(viteScript)) {
    throw new Error(`Vite was not found at ${viteScript}. Run 'npm install' in webui/ or pass --no-vite.`);
  }
  fs.mkdirSync(paths.scratchDir, { recursive: true });
  const viteLogPath = path.join(paths.scratchDir, "vite_dev.log");
  const viteFd = fs.openSync(viteLogPath, "w");
  const args = [viteScript, "--port", String(frontendPort), "--strictPort", ...(host ? ["--host", host] : [])];
  const viteProc = spawn(process.execPath, args, {
    cwd: viteRoot,
    env: {
      ...process.env,
      ANIMEAN_DEV_BACKEND_PORT: String(backendPort),
      // Vite answers only host names it knows; IP addresses it always accepts.
      ...(host && net.isIP(host) === 0 ? { ANIMEAN_DEV_ALLOWED_HOST: host } : {}),
    },
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
  return { vitePid: viteProc.pid, viteRoot, viteBackendPort: backendPort, viteReused: false, viteReady: true };
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
  const host = resolveListenHost(options.host);
  const startVite = options.startVite !== false;
  if (host && !startVite) {
    throw new Error("--host serves the page from Vite; it cannot be combined with --no-vite.");
  }

  if (options.restart) {
    await stopDevEnvironment({ repoRoot: paths.repoRoot });
  }

  const existing = readActiveSessionFile(paths.repoRoot);
  const { backendPort: requestedPort, frontendPort, portSource } = await choosePorts(existing, options, startVite);
  const { backendCopiedFrom, backendCopyWarning, backendReused, ...backend } = await startOrReuseBackend(
    existing,
    paths,
    { requestedPort, token: freshToken, backendExe: options.backendExe }
  );
  const { viteReused, viteReady, ...vite } = startVite
    ? await startOrReuseVite(existing, paths, { frontendPort, host, backendPort: backend.backendPort })
    : { vitePid: null, viteRoot: null, viteBackendPort: null, viteReused: false, viteReady: false };

  const meta = {
    startedAt: existing?.startedAt && backendReused ? existing.startedAt : new Date().toISOString(),
    updatedAt: new Date().toISOString(),
    repoRoot: paths.repoRoot,
    ...backend,
    frontendPort,
    frontendHost: host,
    ...vite,
    ...pageUrls(host, frontendPort, backend),
  };
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
 * Stop managed dev backend and Vite processes recorded in the session files.
 */
export async function stopDevEnvironment(options = {}) {
  const paths = getToolkitPaths(options.repoRoot);
  const existing = readActiveSessionFile(paths.repoRoot);
  const stopped = [];

  if (existing) {
    for (const [label, pid] of [
      ["backend", existing.backendPid],
      ["vite", existing.vitePid],
    ]) {
      if (pid && isPidAlive(pid)) {
        try {
          process.kill(pid);
          stopped.push({ target: label, pid, stopped: true });
        } catch (err) {
          stopped.push({
            target: label,
            pid,
            stopped: false,
            error: err instanceof Error ? err.message : String(err),
          });
        }
      }
    }
  }

  clearSessionMetadata(paths.repoRoot);
  return {
    hadSession: Boolean(existing),
    stopped,
  };
}
