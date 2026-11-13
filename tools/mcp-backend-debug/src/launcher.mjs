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

/**
 * Start or reuse a managed AnimeAnBackend.exe + Vite dev environment for Chrome testing.
 */
export async function startDevEnvironment(options = {}) {
  const paths = getToolkitPaths(options.repoRoot);
  const requestedPort = options.port !== undefined ? Number(options.port) : DEFAULT_DEV_PORT;
  // A fresh random token per launch: the backend accepts any http://localhost
  // page origin and python.run executes code, so a well-known token would let
  // any local web page in.
  let token = String(options.token || randomBytes(16).toString("hex"));
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
  let backendSourceDir = null;
  let backendCopyWarning = null;

  if (existing && isPidAlive(existing.backendPid)) {
    try {
      const parsed = new URL(existing.wsUrl);
      const existingPort = Number(parsed.port);
      if (existingPort > 0 && (await isTcpPortOpen("127.0.0.1", existingPort, 400))) {
        backendPid = existing.backendPid;
        backendPort = existingPort;
        wsUrl = existing.wsUrl;
        backendExe = existing.backendExe || null;
        token = existing.token || tokenFromWsUrl(existing.wsUrl) || token;
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
    const staged = stageBackendExecutable(backendExe, paths.scratchDir);
    backendExe = staged.exePath;
    backendSourceDir = staged.copiedFrom;
    backendCopyWarning = staged.copyWarning;
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
    ...(backendSourceDir ? { backendCopiedFrom: backendSourceDir } : {}),
    ...(backendCopyWarning ? { backendCopyWarning } : {}),
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
