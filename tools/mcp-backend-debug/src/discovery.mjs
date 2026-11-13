import { execFileSync } from "node:child_process";
import fs from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

export const DEFAULT_DEV_PORT = 3432;
export const DEFAULT_DEV_TOKEN = "animean-dev";
export const DEFAULT_FRONTEND_PORT = 1430;

/**
 * Locate the AnimeAn repository/worktree root from a starting directory or module path.
 * @param {string} [startDir]
 * @returns {string}
 */
export function findRepoRoot(startDir = __dirname) {
  let current = path.resolve(startDir);
  while (true) {
    if (
      fs.existsSync(path.join(current, ".git")) ||
      (fs.existsSync(path.join(current, "CMakeLists.txt")) &&
        fs.existsSync(path.join(current, "backend")))
    ) {
      return current;
    }
    const parent = path.dirname(current);
    if (parent === current) {
      return path.resolve(__dirname, "..", "..", "..");
    }
    current = parent;
  }
}

/**
 * Resolve the shared Git common directory across the main repo and all worktrees.
 * @param {string} repoRoot
 * @returns {string}
 */
export function findGitCommonDir(repoRoot) {
  const dotGit = path.join(repoRoot, ".git");
  try {
    if (fs.existsSync(dotGit)) {
      const stat = fs.statSync(dotGit);
      if (stat.isDirectory()) {
        return dotGit;
      }
      if (stat.isFile()) {
        const text = fs.readFileSync(dotGit, "utf-8");
        const match = text.match(/gitdir:\s*(.+)/i);
        if (match) {
          const gitDir = path.resolve(repoRoot, match[1].trim());
          const commonDirFile = path.join(gitDir, "commondir");
          if (fs.existsSync(commonDirFile)) {
            const rel = fs.readFileSync(commonDirFile, "utf-8").trim();
            return path.resolve(gitDir, rel);
          }
          return gitDir;
        }
      }
    }
  } catch {
    // Fall back to git rev-parse
  }

  try {
    const out = execFileSync("git", ["rev-parse", "--git-common-dir"], {
      cwd: repoRoot,
      encoding: "utf-8",
      stdio: ["ignore", "pipe", "ignore"],
    }).trim();
    if (out) {
      return path.resolve(repoRoot, out);
    }
  } catch {
    // Fall back to local .scratch when not in a git checkout
  }
  return path.join(repoRoot, ".scratch");
}

/**
 * Compute standard paths used by the MCP server, Python probe IPC, and dev launcher.
 * @param {string} [repoRoot]
 */
export function getToolkitPaths(repoRoot = findRepoRoot()) {
  const gitCommonDir = findGitCommonDir(repoRoot);
  const mainRepoRoot =
    path.basename(gitCommonDir) === ".git" ? path.dirname(gitCommonDir) : repoRoot;
  const toolRoot = path.join(repoRoot, "tools", "mcp-backend-debug");
  return {
    repoRoot,
    mainRepoRoot,
    gitCommonDir,
    toolRoot,
    pythonProbeDir: path.join(toolRoot, "python"),
    scratchDir: path.join(repoRoot, ".scratch"),
    snapshotsDir: path.join(repoRoot, ".scratch", "mcp-snapshots"),
    ipcDir: path.join(gitCommonDir, "mcp_ipc"),
    worktreeSessionPath: path.join(repoRoot, ".scratch", "dev_backend_session.json"),
    sharedSessionPath: path.join(gitCommonDir, "animean_dev_backend_session.json"),
  };
}

/**
 * Check if an OS process ID is still running.
 * @param {number|undefined|null} pid
 * @returns {boolean}
 */
export function isPidAlive(pid) {
  const n = Number(pid);
  if (!Number.isInteger(n) || n <= 0) {
    return false;
  }
  try {
    process.kill(n, 0);
    return true;
  } catch (err) {
    return err?.code === "EPERM";
  }
}

/**
 * Probe whether a local TCP port accepts connections.
 * @param {string} host
 * @param {number} port
 * @param {number} [timeoutMs]
 * @returns {Promise<boolean>}
 */
export function isTcpPortOpen(host, port, timeoutMs = 400) {
  return new Promise((resolve) => {
    const socket = net.createConnection({ host, port });
    let settled = false;
    const finish = (ok) => {
      if (settled) return;
      settled = true;
      socket.destroy();
      resolve(ok);
    };
    socket.setTimeout(timeoutMs);
    socket.once("connect", () => finish(true));
    socket.once("timeout", () => finish(false));
    socket.once("error", () => finish(false));
  });
}

function readJsonFileSafe(filePath) {
  try {
    if (!fs.existsSync(filePath)) {
      return null;
    }
    return JSON.parse(fs.readFileSync(filePath, "utf-8"));
  } catch {
    return null;
  }
}

/**
 * Read the most valid active dev session file across worktree and shared git metadata.
 * @param {string} [repoRoot]
 */
export function readActiveSessionFile(repoRoot = findRepoRoot()) {
  const paths = getToolkitPaths(repoRoot);
  const candidates = [
    { source: "worktree_session_file", filePath: paths.worktreeSessionPath },
    { source: "shared_git_session_file", filePath: paths.sharedSessionPath },
  ];
  for (const candidate of candidates) {
    const data = readJsonFileSafe(candidate.filePath);
    if (!data || typeof data.wsUrl !== "string" || !data.wsUrl.trim()) {
      continue;
    }
    if (data.backendPid && !isPidAlive(data.backendPid)) {
      continue;
    }
    return {
      ...data,
      _source: candidate.source,
      _filePath: candidate.filePath,
    };
  }
  return null;
}

/**
 * Write dev session metadata to both the current worktree and shared git common dir.
 * @param {Record<string, any>} meta
 * @param {string} [repoRoot]
 */
export function writeSessionMetadata(meta, repoRoot = findRepoRoot()) {
  const paths = getToolkitPaths(repoRoot);
  const serialized = JSON.stringify(meta, null, 2);
  for (const target of [paths.worktreeSessionPath, paths.sharedSessionPath]) {
    fs.mkdirSync(path.dirname(target), { recursive: true });
    const tmp = `${target}.tmp`;
    fs.writeFileSync(tmp, serialized, "utf-8");
    fs.renameSync(tmp, target);
  }
  return {
    worktreeSessionPath: paths.worktreeSessionPath,
    sharedSessionPath: paths.sharedSessionPath,
  };
}

/**
 * Remove dev session metadata files when stopping the managed dev server.
 * @param {string} [repoRoot]
 */
export function clearSessionMetadata(repoRoot = findRepoRoot()) {
  const paths = getToolkitPaths(repoRoot);
  for (const target of [paths.worktreeSessionPath, paths.sharedSessionPath]) {
    try {
      if (fs.existsSync(target)) {
        fs.unlinkSync(target);
      }
    } catch {
      // Ignore cleanup errors
    }
  }
}

/**
 * Discover the target AnimeAnBackend WebSocket URL using deterministic precedence.
 * @param {{ explicitWsUrl?: string, repoRoot?: string }} [options]
 */
export function resolveBackendEndpoint(options = {}) {
  const repoRoot = options.repoRoot || findRepoRoot();
  if (options.explicitWsUrl && String(options.explicitWsUrl).trim()) {
    return {
      wsUrl: String(options.explicitWsUrl).trim(),
      source: "explicit_argument",
      sessionMeta: null,
    };
  }
  if (process.env.ANIMEAN_BACKEND_WS_URL && process.env.ANIMEAN_BACKEND_WS_URL.trim()) {
    return {
      wsUrl: process.env.ANIMEAN_BACKEND_WS_URL.trim(),
      source: "env_ANIMEAN_BACKEND_WS_URL",
      sessionMeta: null,
    };
  }
  const active = readActiveSessionFile(repoRoot);
  if (active) {
    return {
      wsUrl: active.wsUrl,
      source: active._source,
      sessionMeta: active,
    };
  }
  return {
    wsUrl: `ws://127.0.0.1:${DEFAULT_DEV_PORT}/?token=${DEFAULT_DEV_TOKEN}`,
    source: "default_dev_endpoint",
    sessionMeta: null,
  };
}

/**
 * Locate an AnimeAnBackend.exe binary across explicit path, current worktree, or main repo.
 * @param {{ explicitExe?: string, repoRoot?: string }} [options]
 * @returns {string|null}
 */
export function locateBackendExecutable(options = {}) {
  const repoRoot = options.repoRoot || findRepoRoot();
  const { mainRepoRoot } = getToolkitPaths(repoRoot);
  const candidates = [
    options.explicitExe,
    process.env.ANIMEAN_BACKEND_EXE,
    path.join(repoRoot, "build", "Release", "AnimeAnBackend.exe"),
    path.join(repoRoot, "build", "wt-check", "AnimeAnBackend.exe"),
    path.join(repoRoot, "dist", "AnimeAn", "AnimeAnBackend.exe"),
    path.join(mainRepoRoot, "dist", "AnimeAn", "AnimeAnBackend.exe"),
    path.join(mainRepoRoot, "build", "Release", "AnimeAnBackend.exe"),
    path.join(mainRepoRoot, "build", "wt-check", "AnimeAnBackend.exe"),
  ].filter(Boolean);

  for (const candidate of candidates) {
    const resolved = path.resolve(candidate);
    if (fs.existsSync(resolved)) {
      return resolved;
    }
  }
  return null;
}

/**
 * Build child process environment variables needed by AnimeAnBackend.exe on Windows.
 * @param {string} exePath
 * @param {string} [repoRoot]
 */
export function buildBackendEnv(exePath, repoRoot = findRepoRoot()) {
  const { mainRepoRoot } = getToolkitPaths(repoRoot);
  const candidateDirs = [
    path.dirname(path.resolve(exePath)),
    path.join(repoRoot, "tools", "python312"),
    path.join(mainRepoRoot, "tools", "python312"),
    path.win32.normalize("C:/Qt/Tools/mingw1310_64/bin"),
    path.win32.normalize("C:/Qt/6.9.1/mingw_64/bin"),
  ].filter((dir) => fs.existsSync(dir));

  const currentPath = process.env.PATH || process.env.Path || "";
  const combinedPath = [...new Set(candidateDirs), currentPath].filter(Boolean).join(path.delimiter);
  return {
    ...process.env,
    PATH: combinedPath,
  };
}
