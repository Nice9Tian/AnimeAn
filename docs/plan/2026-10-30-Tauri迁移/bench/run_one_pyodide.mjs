// Runs run_one.py under Pyodide in Node (V8, the Chrome engine), fresh instance
// per process. Usage: node run_one_pyodide.mjs <pyodide_dir> <repo> <bench_dir> <name>
import { createRequire } from "node:module";
import { pathToFileURL } from "node:url";
import path from "node:path";

const [pyodideDir, repo, benchDir, name] = process.argv.slice(2);
const { loadPyodide } = await import(pathToFileURL(path.join(pyodideDir, "pyodide.mjs")).href);

const t0 = performance.now();
const py = await loadPyodide({
  indexURL: pyodideDir + path.sep,
  packageCacheDir: path.join(pyodideDir, "..", "..", "pkgcache"),
  stdout: () => {},
  stderr: () => {},
});
await py.loadPackage("numpy", { messageCallback: () => {} });
const startupMs = performance.now() - t0;

py.FS.mkdir("/repo");
py.FS.mount(py.FS.filesystems.NODEFS, { root: repo }, "/repo");
py.FS.mkdir("/bench");
py.FS.mount(py.FS.filesystems.NODEFS, { root: benchDir }, "/bench");

py.globals.set("bench_name", name);
const out = py.runPython(`
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, "/bench")
import run_one
run_one.run_one("/repo", bench_name)
`);
const data = JSON.parse(out);
data.startup_ms = startupMs;
console.log("RESULT " + JSON.stringify(data));
