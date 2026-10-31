import http from "node:http";
import crypto from "node:crypto";
import { fileURLToPath } from "node:url";

const WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/**
 * 编码单帧 WebSocket 数据（服务端 → 客户端，不掩码）。
 * @param {number} opcode 0x1=文本, 0x2=二进制, 0x8=关闭, 0xA=Pong
 * @param {Buffer} payload
 */
export function encodeWsFrame(opcode, payload) {
  const len = payload.length;
  let header;
  if (len < 126) {
    header = Buffer.alloc(2);
    header[0] = 0x80 | (opcode & 0x0f);
    header[1] = len;
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x80 | (opcode & 0x0f);
    header[1] = 126;
    header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x80 | (opcode & 0x0f);
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(len), 2);
  }
  return Buffer.concat([header, payload]);
}

export function sendWsText(socket, obj) {
  const text = typeof obj === "string" ? obj : JSON.stringify(obj);
  socket.write(encodeWsFrame(0x1, Buffer.from(text, "utf8")));
}

export function sendWsBinary(socket, buf) {
  const data = Buffer.isBuffer(buf) ? buf : Buffer.from(buf);
  socket.write(encodeWsFrame(0x2, data));
}

/**
 * 从接收缓冲区解析客户端发来的 WebSocket 帧。
 * @param {Buffer} buffer
 */
export function decodeWsFrames(buffer) {
  const frames = [];
  let offset = 0;
  while (offset + 2 <= buffer.length) {
    const first = buffer[offset];
    const second = buffer[offset + 1];
    const opcode = first & 0x0f;
    const masked = (second & 0x80) !== 0;
    let payloadLen = second & 0x7f;
    let hdrLen = 2;

    if (payloadLen === 126) {
      if (offset + 4 > buffer.length) break;
      payloadLen = buffer.readUInt16BE(offset + 2);
      hdrLen = 4;
    } else if (payloadLen === 127) {
      if (offset + 10 > buffer.length) break;
      payloadLen = Number(buffer.readBigUInt64BE(offset + 2));
      hdrLen = 10;
    }

    const maskLen = masked ? 4 : 0;
    if (offset + hdrLen + maskLen + payloadLen > buffer.length) break;

    const maskKey = masked ? buffer.subarray(offset + hdrLen, offset + hdrLen + 4) : null;
    const rawPayload = Buffer.from(
      buffer.subarray(offset + hdrLen + maskLen, offset + hdrLen + maskLen + payloadLen),
    );
    if (masked && maskKey) {
      for (let i = 0; i < rawPayload.length; i++) {
        rawPayload[i] ^= maskKey[i & 3];
      }
    }
    frames.push({ opcode, payload: rawPayload });
    offset += hdrLen + maskLen + payloadLen;
  }
  return { frames, remaining: buffer.subarray(offset) };
}

function sendInitialData(socket) {
  // 1. 发送若干初始 state
  sendWsText(socket, {
    t: "state",
    key: "theme",
    value: { name: "dark", accent: "#2563eb" },
  });
  sendWsText(socket, {
    t: "state",
    key: "tool",
    value: { current: "brush", width: 5, smooth: 50, color: "#000000ff" },
  });
  sendWsText(socket, {
    t: "state",
    key: "layer/visible",
    value: true,
  });
  sendWsText(socket, {
    t: "state",
    key: "layers/main",
    value: {
      visible: true,
      selectedIndex: 0,
      items: [
        { id: "grp_char", name: "角色图层组", kind: "group", visible: true },
        { id: "lyr_line", name: "人物线稿", kind: "Vector", visible: true },
      ],
    },
  });
  sendWsText(socket, {
    t: "state",
    key: "timeline/main",
    value: { currentFrame: 1, totalFrames: 24, fps: 24, playing: false },
  });
  sendWsText(socket, {
    t: "state",
    key: "status",
    value: { message: "Mock 后端已连接" },
  });

  // 2. 发送一张 2x2 示例图片（文本帧 + 紧跟的二进制帧）
  const imgW = 2;
  const imgH = 2;
  const pixels = new Uint8Array([
    255, 0, 0, 255, 0, 255, 0, 255,
    0, 0, 255, 255, 255, 255, 0, 255,
  ]);
  sendWsText(socket, {
    t: "image",
    key: "checker2x2",
    w: imgW,
    h: imgH,
    bytes: pixels.byteLength,
  });
  sendWsBinary(socket, pixels);

  // 3. 发送一帧主画板绘制指令
  sendWsText(socket, {
    t: "board",
    view: "main",
    frame: 1,
    w: 567,
    h: 690,
    keep: 0,
    ops: [
      ["T", 1, 0, 0, 1, 0, 0],
      ["P", 0, "#00000000", 0, 0, 0, 2, 0],
      ["B", 1, "#1e2430ff"],
      ["R", 0, 0, 567, 690],
      ["B", 1, "#ffffffff"],
      ["P", 1, "#64748bff", 1, 0, 0, 2, 1],
      ["R", 40, 40, 480, 320],
      ["I", "checker2x2", 0, 0, 2, 2, 60, 60, 32, 32],
      ["B", 0, "#00000000"],
      ["P", 1, "#2563ebff", 3, 32, 128, 2, 0],
      ["H", 0, [0, 1, 2, 3, 3], [80, 180, 160, 120, 220, 100, 300, 240, 420, 160]],
    ],
  });
}

export function createMockBackendServer({ port = 9001, host = "127.0.0.1" } = {}) {
  const sockets = new Set();

  const server = http.createServer((_req, res) => {
    res.writeHead(200, { "Content-Type": "application/json; charset=utf-8" });
    res.end(JSON.stringify({ status: "ok", service: "animean-mock-backend" }));
  });

  server.on("upgrade", (req, socket) => {
    const key = req.headers["sec-websocket-key"];
    if (!key || typeof key !== "string") {
      socket.destroy();
      return;
    }
    const accept = crypto.createHash("sha1").update(key + WS_GUID).digest("base64");
    const headers = [
      "HTTP/1.1 101 Switching Protocols",
      "Upgrade: websocket",
      "Connection: Upgrade",
      `Sec-WebSocket-Accept: ${accept}`,
      "\r\n",
    ];
    socket.write(headers.join("\r\n"));
    sockets.add(socket);

    let recvBuf = Buffer.alloc(0);
    let frameCounter = 1;

    sendInitialData(socket);

    socket.on("data", (chunk) => {
      recvBuf = Buffer.concat([recvBuf, chunk]);
      const { frames, remaining } = decodeWsFrames(recvBuf);
      recvBuf = remaining;
      for (const frame of frames) {
        if (frame.opcode === 0x8) {
          socket.end(encodeWsFrame(0x8, Buffer.alloc(0)));
          return;
        }
        if (frame.opcode === 0x9) {
          socket.write(encodeWsFrame(0x0a, frame.payload));
          continue;
        }
        if (frame.opcode === 0x1) {
          let msg;
          try {
            msg = JSON.parse(frame.payload.toString("utf8"));
          } catch {
            continue;
          }
          if (msg.t === "call") {
            sendWsText(socket, {
              t: "reply",
              id: msg.id,
              ok: true,
              result: { echoedMethod: msg.method, echoedParams: msg.params ?? {} },
            });
          } else if (msg.t === "input" && msg.kind === "press") {
            frameCounter++;
            sendWsText(socket, {
              t: "board",
              view: msg.view ?? "main",
              frame: frameCounter,
              w: 567,
              h: 690,
              keep: 11,
              ops: [
                ["P", 1, "#ef4444ff", 2, 32, 128, 2, 0],
                ["S", msg.x - 6, msg.y, msg.x + 6, msg.y, msg.x, msg.y - 6, msg.x, msg.y + 6],
              ],
            });
          }
        }
      }
    });

    socket.on("close", () => {
      sockets.delete(socket);
    });
    socket.on("error", () => {
      sockets.delete(socket);
    });
  });

  return {
    server,
    start() {
      return new Promise((resolve, reject) => {
        server.once("error", reject);
        server.listen(port, host, () => {
          const addr = server.address();
          const actualPort = typeof addr === "object" && addr ? addr.port : port;
          resolve({ host, port: actualPort, url: `ws://${host}:${actualPort}` });
        });
      });
    },
    stop() {
      for (const s of sockets) {
        s.destroy();
      }
      sockets.clear();
      return new Promise((resolve) => {
        server.close(() => resolve());
      });
    },
  };
}

const isMain = process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1];
if (isMain) {
  const port = Number(process.env.PORT ?? process.argv[2] ?? 9001);
  const mock = createMockBackendServer({ port });
  mock.start().then(({ url }) => {
    console.log(`Mock Backend WebSocket Server listening at ${url}`);
    console.log(`Open in browser: http://localhost:1430/?ws=${encodeURIComponent(url)}`);
  });
}
