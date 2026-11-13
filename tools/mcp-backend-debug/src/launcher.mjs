import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import {
  buildBackendEnv,
  clearSessionMetadata,
  DEFAULT_DEV_PORT,
  DEFAULT_DEV_TOKEN,
  DEFAULT_FRONTEND_PORT,
  getToolkitPaths,
  isPidAlive,
  isTcpPortOpen,
  locateBackendExecutable,
  readActiveSessionFile,
  writeSessionMetadata,
} from "./discovery.mjs";

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

/**
 * Start or reuse a managed AnimeAnBackend.exe + Vite dev environment for Chrome testing.
 */
export async function startDevEnvironment(options = {}) {
  const paths = getToolkitPaths(options.repoRoot);
  const requestedPort = options.port !== undefined ? Number(options.port) : DEFAULT_DEV_PORT;
  const token = String(options.token || DEFAULT_DEV_TOKEN);
  const frontendPort = options.frontendPort !== undefined ? Number(options.frontendPort) : DEFAULT_FRONTEND_PORT;
  const startVite = options.startVite !== false;
  const openBrowser = Boolean(options.openBrowser);
  const restart = Boolean(options.restart);

  if (restart) {
    await stopDevEnvironment({ repoRoot: paths.repoRoot });
  }

  const existing = readActiveSessionFile(paths.repoRoot);
  let backendPid = null;
  let backendPort = null;
  let wsUrl = null;
  let backendExe = null;
  let backendReused = false;

  if (existing && isPidAlive(existing.backendPid)) {
    try {
      const parsed = new URL(existing.wsUrl);
      const existingPort = Number(parsed.port);
      if (existingPort > 0 && (await isTcpPortOpen("127.0.0.1", existingPort, 400))) {
        backendPid = existing.backendPid;
        backendPort = existingPort;
        wsUrl = existing.wsUrl;
        backendExe = existing.backendExe || null;
        backendReused = true;
      }
    } catch {
      // Fall through to fresh spawn
    }
  }

  if (!wsUrl) {
    backendExe = locateBackendExecutable({
      explicitExe: options.backendExe,
      repoRoot: paths.repoRoot,
    });
    if (!backendExe) {
      throw new Error(
        "Could not locate AnimeAnBackend.exe. Build first or pass --backend-exe / ANIMEAN_BACKEND_EXE."
      );
    }

    fs.mkdirSync(paths.scratchDir, { recursive: true });
    const backendLogPath = path.join(paths.scratchDir, "backend_dev.log");
    const logFd = fs.openSync(backendLogPath, "w");

    const env = buildBackendEnv(backendExe, paths.repoRoot);
    const proc = spawn(backendExe, ["--port", String(requestedPort), "--token", token], {
      cwd: path.dirname(backendExe),
      env,
      detached: true,
      stdio: ["ignore", logFd, logFd],
      windowsHide: true,
    });
    fs.closeSync(logFd);

    backendPort = await waitForBackendPort(proc, backendLogPath, 15000);
    proc.unref();
    backendPid = proc.pid;
    wsUrl = `ws://127.0.0.1:${backendPort}/?token=${encodeURIComponent(token)}`;
  }

  let vitePid = existing?.vitePid && isPidAlive(existing.vitePid) ? existing.vitePid : null;
  let viteRoot = existing?.viteRoot || null;
  let viteReused = false;
  let viteReady = false;

  if (startVite) {
    if (await isTcpPortOpen("localhost", frontendPort, 300)) {
      viteReused = true;
      viteReady = true;
    } else {
      viteRoot = resolveViteRoot(paths.repoRoot);
      const viteScript = path.join(viteRoot, "node_modules", "vite", "bin", "vite.js");
      if (!fs.existsSync(viteScript)) {
        throw new Error(
          `Vite was not found at ${viteScript}. Run 'npm install' in webui/ or pass --no-vite.`
        );
      }
      fs.mkdirSync(paths.scratchDir, { recursive: true });
      const viteLogPath = path.join(paths.scratchDir, "vite_dev.log");
      const viteFd = fs.openSync(viteLogPath, "w");
      const viteProc = spawn(
        process.execPath,
        [viteScript, "--port", String(frontendPort), "--strictPort"],
        {
          cwd: viteRoot,
          detached: true,
          stdio: ["ignore", viteFd, viteFd],
          windowsHide: true,
        }
      );
      fs.closeSync(viteFd);
      viteProc.unref();
      vitePid = viteProc.pid;
      viteReady = await waitForPortOpen("localhost", frontendPort, 12000);
      if (!viteReady) {
        const vlog = fs.existsSync(viteLogPath) ? fs.readFileSync(viteLogPath, "utf-8") : "";
        throw new Error(`Vite did not listen on port ${frontendPort}.\n${vlog.slice(-2000)}`);
      }
    }
  }

  const chromeUrl = `http://localhost:${frontendPort}/?ws=${encodeURIComponent(wsUrl)}`;
  const meta = {
    startedAt: existing?.startedAt && backendReused ? existing.startedAt : new Date().toISOString(),
    updatedAt: new Date().toISOString(),
    repoRoot: paths.repoRoot,
    backendExe,
    backendPid,
    backendPort,
    token,
    wsUrl,
    frontendPort,
    vitePid,
    viteRoot,
    chromeUrl,
  };
  const sessionFiles = writeSessionMetadata(meta, paths.repoRoot);

  let browserLaunch = null;
  if (openBrowser) {
    browserLaunch = openInChrome(chromeUrl);
  }

  return {
    ...meta,
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
