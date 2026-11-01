// Feel bench dev server. Serves the page, the Pyodide runtime and pyfile/*.py on
// localhost only, and tells the page which .py changed so it can reload it.
// Usage: node server.mjs <pyfile_dir> <pyodide_dir> <numpy_whl> [port]
import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const [pyfileDir, pyodideDir, numpyWheel, portArg] = process.argv.slice(2);
const port = Number(portArg || 8770);
const here = path.dirname(fileURLToPath(import.meta.url));
const SKIP = new Set(["three_vendor.py"]);  // 12k lines of base64, To 3D only
const clients = new Set();

const types = { ".html": "text/html; charset=utf-8", ".mjs": "text/javascript", ".js": "text/javascript",
  ".wasm": "application/wasm", ".json": "application/json", ".zip": "application/zip",
  ".whl": "application/zip", ".py": "text/plain; charset=utf-8" };

function send(res, file) {
  fs.readFile(file, (error, data) => {
    if (error) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { "Content-Type": types[path.extname(file)] || "application/octet-stream",
      "Cache-Control": "no-store" });
    res.end(data);
  });
}

http.createServer((req, res) => {
  const url = new URL(req.url, "http://localhost");
  if (url.pathname === "/") return send(res, path.join(here, "index.html"));
  if (url.pathname === "/py-list") {
    const names = fs.readdirSync(pyfileDir).filter((n) => n.endsWith(".py") && !SKIP.has(n));
    res.writeHead(200, { "Content-Type": "application/json" });
    return res.end(JSON.stringify(names));
  }
  if (url.pathname.startsWith("/py/")) return send(res, path.join(pyfileDir, path.basename(url.pathname)));
  if (url.pathname.startsWith("/pyodide/")) {
    const name = path.basename(url.pathname);
    return send(res, name.startsWith("numpy-") ? numpyWheel : path.join(pyodideDir, name));
  }
  if (url.pathname === "/events") {
    res.writeHead(200, { "Content-Type": "text/event-stream", "Cache-Control": "no-store" });
    res.write(": hello\n\n");
    clients.add(res);
    req.on("close", () => clients.delete(res));
    return;
  }
  res.writeHead(404); res.end();
}).listen(port, "127.0.0.1", () => console.log(`feel bench: http://localhost:${port}/  watching ${pyfileDir}`));

// Editors fire several events per save; settle before announcing.
const pending = new Map();
fs.watch(pyfileDir, (event, name) => {
  if (!name || !name.endsWith(".py") || SKIP.has(name)) return;
  clearTimeout(pending.get(name));
  pending.set(name, setTimeout(() => {
    pending.delete(name);
    for (const res of clients) res.write(`data: ${name}\n\n`);
    console.log("changed", name);
  }, 60));
});
