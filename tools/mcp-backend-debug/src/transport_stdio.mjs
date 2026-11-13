/**
 * Dual-framing stdio transport for MCP JSON-RPC 2.0.
 *
 * Automatically detects whether the host uses newline-delimited JSON (NDJSON,
 * used by Claude Code, Codex CLI, Cursor, official MCP SDK) or HTTP-style
 * `Content-Length` headers, and replies in the exact same framing.
 */

function findLeadingNonWsByte(buf) {
  for (let i = 0; i < buf.length; i += 1) {
    const b = buf[i];
    if (b !== 0x20 && b !== 0x09 && b !== 0x0d && b !== 0x0a) {
      return i;
    }
  }
  return -1;
}

/**
 * Create a pure framing parser/serializer bound to any readable/writable streams.
 * Exported separately so unit tests can drive in-memory streams directly.
 */
export function createStdioTransport({
  input = process.stdin,
  output = process.stdout,
  onMessage,
  onParseError,
}) {
  let buffer = Buffer.alloc(0);
  let framing = "ndjson"; // "ndjson" | "content-length"

  function send(obj) {
    if (obj === null || obj === undefined) return;
    const json = JSON.stringify(obj);
    if (framing === "content-length") {
      const byteLen = Buffer.byteLength(json, "utf-8");
      output.write(`Content-Length: ${byteLen}\r\n\r\n${json}`, "utf-8");
    } else {
      output.write(`${json}\n`, "utf-8");
    }
  }

  function processBuffer() {
    while (buffer.length > 0) {
      const startIdx = findLeadingNonWsByte(buffer);
      if (startIdx === -1) {
        buffer = Buffer.alloc(0);
        return;
      }
      if (startIdx > 0) {
        buffer = buffer.subarray(startIdx);
      }

      const firstByte = buffer[0];
      // JSON messages always start with '{' (0x7b) or '[' (0x5b).
      if (firstByte === 0x7b || firstByte === 0x5b) {
        const nlIdx = buffer.indexOf(0x0a);
        if (nlIdx === -1) return;
        const line = buffer.subarray(0, nlIdx).toString("utf-8").trim();
        buffer = buffer.subarray(nlIdx + 1);
        if (!line) continue;
        framing = "ndjson";
        dispatchRaw(line);
        continue;
      }

      // Header-framed message (Content-Length: ... \r\n\r\n or \n\n)
      let sepIdx = buffer.indexOf("\r\n\r\n");
      let sepLen = 4;
      if (sepIdx === -1) {
        sepIdx = buffer.indexOf("\n\n");
        sepLen = 2;
      }
      if (sepIdx === -1) return;

      const headerStr = buffer.subarray(0, sepIdx).toString("utf-8");
      const match = headerStr.match(/Content-Length:\s*(\d+)/i);
      if (!match) {
        buffer = buffer.subarray(sepIdx + sepLen);
        continue;
      }

      const contentLen = Number(match[1]);
      const bodyStart = sepIdx + sepLen;
      if (buffer.length < bodyStart + contentLen) return;

      const jsonStr = buffer.subarray(bodyStart, bodyStart + contentLen).toString("utf-8");
      buffer = buffer.subarray(bodyStart + contentLen);
      framing = "content-length";
      dispatchRaw(jsonStr);
    }
  }

  function dispatchRaw(jsonStr) {
    let parsed;
    try {
      parsed = JSON.parse(jsonStr);
    } catch (err) {
      if (onParseError) onParseError(err, jsonStr);
      return;
    }
    Promise.resolve(onMessage(parsed))
      .then((reply) => {
        if (reply !== null && reply !== undefined) {
          send(reply);
        }
      })
      .catch((err) => {
        if (onParseError) onParseError(err, jsonStr);
      });
  }

  const onData = (chunk) => {
    const chunkBuf = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk, "utf-8");
    buffer = Buffer.concat([buffer, chunkBuf]);
    processBuffer();
  };

  input.on("data", onData);

  return {
    send,
    close() {
      input.off("data", onData);
    },
  };
}
