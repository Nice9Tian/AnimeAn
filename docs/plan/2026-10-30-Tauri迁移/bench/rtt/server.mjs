// Local WebSocket echo plus a test page: measures what one browser -> native
// backend -> browser round trip costs. Python here stands for the real backend:
// it receives the same JSON message and replies from a separate process.
import http from "node:http";
import net from "node:net";
import crypto from "node:crypto";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";
import fs from "node:fs";

const here = path.dirname(fileURLToPath(import.meta.url));
const python = process.argv[2];
const port = 8772;
const backend = spawn(python, ["-u", path.join(here, "backend.py"), "8773"], { stdio: ["ignore", "pipe", "inherit"] });
await new Promise((resolve) => backend.stdout.once("data", resolve));

function frame(payload) {
  const n = payload.length;
  const head = n < 126 ? Buffer.from([0x81, n])
    : n < 65536 ? Buffer.from([0x81, 126, n >> 8, n & 255])
    : Buffer.concat([Buffer.from([0x81, 127, 0, 0, 0, 0]), (() => { const b = Buffer.alloc(4); b.writeUInt32BE(n); return b; })()]);
  return Buffer.concat([head, payload]);
}

const server = http.createServer((req, res) => {
  res.writeHead(200, { "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store" });
  res.end(fs.readFileSync(path.join(here, "index.html")));
});
server.on("upgrade", (req, socket) => {
  const accept = crypto.createHash("sha1")
    .update(req.headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest("base64");
  socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  socket.setNoDelay(true);
  const py = net.connect(8773, "127.0.0.1");
  py.setNoDelay(true);
  py.on("error", () => socket.destroy());
  let up = Buffer.alloc(0), down = Buffer.alloc(0), parts = [];
  socket.on("data", (chunk) => {
    up = Buffer.concat([up, chunk]);
    for (;;) {
      if (up.length < 2) return;
      const fin = (up[0] & 0x80) !== 0;
      let len = up[1] & 127, at = 2;
      if (len === 126) { if (up.length < 4) return; len = up.readUInt16BE(2); at = 4; }
      else if (len === 127) { if (up.length < 10) return; len = up.readUInt32BE(6); at = 10; }
      if (up.length < at + 4 + len) return;
      const mask = up.subarray(at, at + 4), data = Buffer.from(up.subarray(at + 4, at + 4 + len));
      for (let i = 0; i < len; i++) data[i] ^= mask[i & 3];
      up = up.subarray(at + 4 + len);
      parts.push(data);
      if (!fin) continue;   // browsers fragment large messages
      const whole = Buffer.concat(parts);
      parts = [];
      const head = Buffer.alloc(4); head.writeUInt32BE(whole.length);
      py.write(Buffer.concat([head, whole]));
    }
  });
  py.on("data", (chunk) => {
    down = Buffer.concat([down, chunk]);
    while (down.length >= 4 && down.length >= 4 + down.readUInt32BE(0)) {
      const len = down.readUInt32BE(0);
      socket.write(frame(down.subarray(4, 4 + len)));
      down = down.subarray(4 + len);
    }
  });
  socket.on("close", () => py.destroy());
  socket.on("error", () => py.destroy());
});
server.listen(port, "127.0.0.1", () => console.log("ready"));
setTimeout(() => { backend.kill(); process.exit(0); }, 120000);
