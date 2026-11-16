// What a program built from this repository needs around it to start, on any
// system: its file name, and the folders its DLLs load from. Everything that
// starts AnimeAnBackend or a test program from Node comes here (the backend
// tests, the web UI test that talks to a real backend, the dev launcher in
// tools/mcp-backend-debug), so no script names a machine's folders itself.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));

/** The checkout this file belongs to. */
export const repoRoot = path.resolve(scriptDir, "..");

const isWindows = process.platform === "win32";

/**
 * The file a build gives a program: "AnimeAnBackend.exe" on Windows,
 * "AnimeAnBackend" everywhere else.
 * @param {string} name
 * @returns {string}
 */
export function programFileName(name) {
  return isWindows ? `${name}.exe` : name;
}

/**
 * The MinGW bin folder of the Windows toolchain. build_scripts/toolchain_location.ps1
 * is the one place that says where the toolchain is; this reads it from there.
 * @returns {string | null}
 */
function mingwBinFolder() {
  try {
    const text = fs.readFileSync(path.join(scriptDir, "toolchain_location.ps1"), "utf-8");
    const found = text.match(/^\s*\$mingwBin\s*=\s*"([^"]+)"/m);
    return found ? found[1] : null;
  } catch {
    return null;
  }
}

/**
 * The environment to start a built program with.
 *
 * On Windows a program run from the build tree finds the compiler's runtime
 * DLLs and the Python DLLs through PATH (a packaged folder has them beside the
 * program). Elsewhere a program already knows where its libraries are and PATH
 * is left alone apart from the program's own folder.
 *
 * @param {string} exePath The program about to be started.
 * @param {{ pathFolders?: (string | null | undefined)[], env?: Record<string, string> }} [options]
 *   pathFolders: more folders to put on PATH (missing ones are skipped);
 *   env: variables to set on top.
 * @returns {Record<string, string | undefined>}
 */
export function backendEnv(exePath, options = {}) {
  const folders = [path.dirname(path.resolve(exePath)), ...(options.pathFolders || [])];
  if (isWindows) {
    folders.push(
      mingwBinFolder(),
      process.env.ANIMEAN_PYTHON_HOME,
      path.join(repoRoot, "tools", "python312")
    );
  }
  const usable = [...new Set(folders.filter((folder) => folder && fs.existsSync(folder)))];
  const current = process.env.PATH || process.env.Path || "";
  return {
    ...process.env,
    PATH: [...usable, current].filter(Boolean).join(path.delimiter),
    ...(options.env || {}),
  };
}
