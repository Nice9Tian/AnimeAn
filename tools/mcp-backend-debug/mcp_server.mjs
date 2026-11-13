#!/usr/bin/env node
import { pathToFileURL } from "node:url";
import { BackendDebugClient } from "./src/backend_client.mjs";
import { findRepoRoot } from "./src/discovery.mjs";
import { createMcpHandler } from "./src/mcp_protocol.mjs";
import { createDefaultRegistry } from "./src/tools/index.mjs";
import { startHttpTransport } from "./src/transport_http.mjs";
import { createStdioTransport } from "./src/transport_stdio.mjs";

function parseArgs(argv) {
  const opts = {
    http: false,
    httpPort: 19440,
    host: "127.0.0.1",
    wsUrl: process.env.ANIMEAN_BACKEND_WS_URL || process.env.ANIMEAN_WS_URL || null,
    repoRoot: process.env.ANIMEAN_REPO_ROOT || null,
    help: false,
  };

  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--help" || arg === "-h") {
      opts.help = true;
    } else if (arg === "--http") {
      opts.http = true;
      const next = argv[i + 1];
      if (next !== undefined && !next.startsWith("-") && /^\d+$/.test(next)) {
        opts.httpPort = Number(next);
        i += 1;
      }
    } else if (arg.startsWith("--http=")) {
      opts.http = true;
      const val = arg.slice("--http=".length).trim();
      if (val) opts.httpPort = Number(val);
    } else if (arg === "--host" && argv[i + 1]) {
      opts.host = argv[++i];
    } else if (arg.startsWith("--host=")) {
      opts.host = arg.slice("--host=".length);
    } else if (arg === "--ws" && argv[i + 1]) {
      opts.wsUrl = argv[++i];
    } else if (arg.startsWith("--ws=")) {
      opts.wsUrl = arg.slice("--ws=".length);
    } else if (arg === "--repo-root" && argv[i + 1]) {
      opts.repoRoot = argv[++i];
    } else if (arg.startsWith("--repo-root=")) {
      opts.repoRoot = arg.slice("--repo-root=".length);
    }
  }
  return opts;
}

function printHelp() {
  process.stderr.write(
    [
      "Usage: node tools/mcp-backend-debug/mcp_server.mjs [options]",
      "",
      "Options:",
      "  --ws <url>          Explicit AnimeAnBackend WebSocket URL (default: auto-discover)",
      "  --repo-root <dir>   Explicit AnimeAn repository/worktree root",
      "  --http [port]       Run Streamable HTTP MCP server instead of stdio (default: 19440)",
      "  --host <addr>       Bind host for --http mode (default: 127.0.0.1)",
      "  -h, --help          Show this help message",
      "",
    ].join("\n")
  );
}

export async function runServer(argv = process.argv.slice(2)) {
  const opts = parseArgs(argv);
  if (opts.help) {
    printHelp();
    return;
  }

  const repoRoot = opts.repoRoot || findRepoRoot(process.cwd());
  const client = new BackendDebugClient({
    repoRoot,
    wsUrl: opts.wsUrl || undefined,
  });
  const registry = createDefaultRegistry();
  const handler = createMcpHandler({
    registry,
    context: { client },
  });

  if (opts.http) {
    const httpHandle = await startHttpTransport({
      handler,
      port: opts.httpPort,
      host: opts.host,
    });
    process.stderr.write(
      `[animean-backend-debug] HTTP MCP listening at ${httpHandle.url} (health: ${httpHandle.healthUrl})\n`
    );

    const shutdown = async () => {
      client.disconnect("MCP server shutting down");
      await httpHandle.close().catch(() => {});
      process.exit(0);
    };
    process.once("SIGINT", shutdown);
    process.once("SIGTERM", shutdown);
    return;
  }

  const inFlight = new Set();
  let transport = null;
  transport = createStdioTransport({
    input: process.stdin,
    output: process.stdout,
    onMessage: (payload) => {
      const p = Promise.resolve(handler.handlePayload(payload));
      inFlight.add(p);
      p.finally(() => inFlight.delete(p));
      return p;
    },
    onParseError: (err) => {
      process.stderr.write(
        `[animean-backend-debug] JSON parse error: ${err instanceof Error ? err.message : String(err)}\n`
      );
      if (transport) {
        transport.send({
          jsonrpc: "2.0",
          id: null,
          error: { code: -32700, message: "Parse error" },
        });
      }
    },
  });

  let closing = false;
  const cleanupAndExit = async () => {
    if (closing) return;
    closing = true;
    while (inFlight.size > 0) {
      await Promise.allSettled(Array.from(inFlight));
    }
    transport.close();
    client.disconnect("MCP stdio transport closed");
    // Flush queued stdout writes before terminating on Windows pipes.
    process.stdout.write("", () => {
      process.exit(0);
    });
  };

  process.stdin.once("end", cleanupAndExit);
  process.stdin.once("close", cleanupAndExit);
  process.once("SIGINT", () => {
    transport.close();
    client.disconnect("SIGINT");
    process.exit(0);
  });
  process.once("SIGTERM", () => {
    transport.close();
    client.disconnect("SIGTERM");
    process.exit(0);
  });
}

if (process.argv[1] && pathToFileURL(process.argv[1]).href === import.meta.url) {
  runServer().catch((err) => {
    process.stderr.write(`[animean-backend-debug] Fatal: ${err?.stack || err}\n`);
    process.exit(1);
  });
}
