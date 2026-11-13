import http from "node:http";

/**
 * Start a minimal Streamable HTTP MCP transport bound to 127.0.0.1:<port>.
 *
 * Exposes:
 * - `POST /mcp` : JSON-RPC 2.0 request/notification or batch
 * - `GET /health` : Health probe returning server status
 */
export async function startHttpTransport({
  handler,
  port = 19440,
  host = "127.0.0.1",
  serverName = "animean-backend-debug",
}) {
  const server = http.createServer(async (req, res) => {
    const urlPath = (req.url || "/").split("?")[0];

    res.setHeader("Access-Control-Allow-Origin", "*");
    res.setHeader("Access-Control-Allow-Headers", "Content-Type, Accept, Mcp-Session-Id");
    res.setHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");

    if (req.method === "OPTIONS") {
      res.writeHead(204);
      res.end();
      return;
    }

    if (req.method === "GET" && urlPath === "/health") {
      res.writeHead(200, { "Content-Type": "application/json; charset=utf-8" });
      res.end(JSON.stringify({ ok: true, server: serverName, transport: "streamable-http" }));
      return;
    }

    if (req.method === "POST" && urlPath === "/mcp") {
      const chunks = [];
      let totalBytes = 0;
      const MAX_BODY = 16 * 1024 * 1024;
      for await (const chunk of req) {
        totalBytes += chunk.length;
        if (totalBytes > MAX_BODY) {
          res.writeHead(413, { "Content-Type": "application/json; charset=utf-8" });
          res.end(JSON.stringify({ error: "Payload too large" }));
          return;
        }
        chunks.push(chunk);
      }
      const rawBody = Buffer.concat(chunks).toString("utf-8");
      let parsed;
      try {
        parsed = JSON.parse(rawBody);
      } catch {
        res.writeHead(400, { "Content-Type": "application/json; charset=utf-8" });
        res.end(
          JSON.stringify({
            jsonrpc: "2.0",
            id: null,
            error: { code: -32700, message: "Parse error" },
          })
        );
        return;
      }

      const reply = await handler.handlePayload(parsed);
      if (reply === null || reply === undefined) {
        res.writeHead(202);
        res.end();
        return;
      }
      res.writeHead(200, { "Content-Type": "application/json; charset=utf-8" });
      res.end(JSON.stringify(reply));
      return;
    }

    res.writeHead(404, { "Content-Type": "application/json; charset=utf-8" });
    res.end(JSON.stringify({ error: "Not found" }));
  });

  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(port, host, () => {
      server.off("error", reject);
      resolve();
    });
  });

  const addr = server.address();
  const actualPort = typeof addr === "object" && addr ? addr.port : port;
  return {
    server,
    port: actualPort,
    url: `http://${host}:${actualPort}/mcp`,
    healthUrl: `http://${host}:${actualPort}/health`,
    close: () =>
      new Promise((resolve) => {
        server.close(() => resolve());
      }),
  };
}
