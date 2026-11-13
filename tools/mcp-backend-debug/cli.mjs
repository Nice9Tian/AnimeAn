#!/usr/bin/env node
import path from "node:path";
import { pathToFileURL } from "node:url";
import { BackendDebugClient } from "./src/backend_client.mjs";
import {
  DEFAULT_DEV_PORT,
  DEFAULT_DEV_TOKEN,
  DEFAULT_FRONTEND_PORT,
  findRepoRoot,
  getToolkitPaths,
  locateBackendExecutable,
  readActiveSessionFile,
  resolveBackendEndpoint,
} from "./src/discovery.mjs";
import { openInChrome, startDevEnvironment, stopDevEnvironment } from "./src/launcher.mjs";
import { buildStatusSummary } from "./src/tools/helpers.mjs";

function printHelp() {
  console.log(
    [
      "Usage: node tools/mcp-backend-debug/cli.mjs <command> [options]",
      "",
      "Commands:",
      "  start         Start (or reuse) managed AnimeAnBackend.exe + Vite dev server",
      "  status        Check endpoint discovery and inspect live backend status",
      "  stop          Stop managed AnimeAnBackend.exe + Vite dev processes",
      "  print-config  Print MCP client configurations for Antigravity, Codex, Claude, Cursor, Gemini CLI",
      "",
      "Options for 'start':",
      `  --port <n>            Backend WebSocket port (default: ${DEFAULT_DEV_PORT}; 0 for ephemeral)`,
      `  --token <s>           Backend token (default: ${DEFAULT_DEV_TOKEN})`,
      `  --frontend-port <n>   Vite dev port (default: ${DEFAULT_FRONTEND_PORT})`,
      "  --backend-exe <path>  Explicit path to AnimeAnBackend.exe",
      "  --no-vite             Start backend only without starting Vite",
      "  --open-chrome         Open Chrome to the Vite dev URL after startup",
      "  --restart             Restart any existing managed dev session",
      "  --repo-root <dir>     Override repository/worktree root",
      "",
      "Options for 'status' / 'stop':",
      "  --ws <url>            Explicit WebSocket URL (status only)",
      "  --repo-root <dir>     Override repository/worktree root",
      "",
    ].join("\n")
  );
}

function parseFlags(argv) {
  const opts = {};
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--help" || arg === "-h") {
      opts.help = true;
    } else if (arg === "--no-vite") {
      opts.startVite = false;
    } else if (arg === "--open-chrome") {
      opts.openBrowser = true;
    } else if (arg === "--restart") {
      opts.restart = true;
    } else if (arg === "--port" && argv[i + 1] !== undefined) {
      opts.port = Number(argv[++i]);
    } else if (arg.startsWith("--port=")) {
      opts.port = Number(arg.slice("--port=".length));
    } else if (arg === "--token" && argv[i + 1] !== undefined) {
      opts.token = argv[++i];
    } else if (arg.startsWith("--token=")) {
      opts.token = arg.slice("--token=".length);
    } else if (arg === "--frontend-port" && argv[i + 1] !== undefined) {
      opts.frontendPort = Number(argv[++i]);
    } else if (arg.startsWith("--frontend-port=")) {
      opts.frontendPort = Number(arg.slice("--frontend-port=".length));
    } else if (arg === "--backend-exe" && argv[i + 1] !== undefined) {
      opts.backendExe = argv[++i];
    } else if (arg.startsWith("--backend-exe=")) {
      opts.backendExe = arg.slice("--backend-exe=".length);
    } else if (arg === "--ws" && argv[i + 1] !== undefined) {
      opts.wsUrl = argv[++i];
    } else if (arg.startsWith("--ws=")) {
      opts.wsUrl = arg.slice("--ws=".length);
    } else if (arg === "--repo-root" && argv[i + 1] !== undefined) {
      opts.repoRoot = argv[++i];
    } else if (arg.startsWith("--repo-root=")) {
      opts.repoRoot = arg.slice("--repo-root=".length);
    }
  }
  return opts;
}

function printConfigs(repoRoot) {
  const paths = getToolkitPaths(repoRoot);
  const serverScript = path.join(paths.toolRoot, "mcp_server.mjs").replace(/\\/g, "/");
  const jsonStdio = {
    command: "node",
    args: [serverScript],
  };

  console.log("=== AnimeAn Backend Debug MCP Configurations ===\n");
  console.log(`Server script : ${serverScript}`);
  console.log(`Plugin dir    : ${path.join(repoRoot, ".agents", "plugins", "animean-backend-debug")}\n`);

  console.log("1) Workspace .mcp.json (Claude Code / Cursor / generic repo MCP — already included in repo root):");
  console.log(
    JSON.stringify(
      {
        mcpServers: {
          animean_backend_debug: {
            command: "node",
            args: ["tools/mcp-backend-debug/mcp_server.mjs"],
          },
        },
      },
      null,
      2
    )
  );
  console.log("");

  console.log("2) Antigravity / Claude Desktop / Gemini CLI (~/.gemini/.../mcp_config.json, claude_desktop_config.json, ~/.gemini/settings.json):");
  console.log(
    JSON.stringify(
      {
        mcpServers: {
          animean_backend_debug: jsonStdio,
        },
      },
      null,
      2
    )
  );
  console.log("");

  console.log("3) Codex CLI (~/.codex/config.toml):");
  console.log(
    [
      "[mcp_servers.animean_backend_debug]",
      'command = "node"',
      `args = ["${serverScript}"]`,
    ].join("\n")
  );
  console.log("");

  console.log("4) Streamable HTTP mode (shared localhost URL for any HTTP-capable agent):");
  console.log(`  Start server : node "${serverScript}" --http 19440`);
  console.log("  Endpoint URL : http://127.0.0.1:19440/mcp");
  console.log("  Health URL   : http://127.0.0.1:19440/health");
}

export async function runCli(argv = process.argv.slice(2)) {
  const command = argv[0] && !argv[0].startsWith("-") ? argv[0] : "help";
  const opts = parseFlags(command === "help" ? argv : argv.slice(1));

  if (opts.help || command === "help") {
    printHelp();
    return 0;
  }

  const repoRoot = opts.repoRoot ? path.resolve(opts.repoRoot) : findRepoRoot(process.cwd());

  if (command === "print-config") {
    printConfigs(repoRoot);
    return 0;
  }

  if (command === "start") {
    const launched = await startDevEnvironment({
      repoRoot,
      port: opts.port,
      token: opts.token,
      frontendPort: opts.frontendPort,
      backendExe: opts.backendExe,
      startVite: opts.startVite,
      openBrowser: opts.openBrowser,
      restart: opts.restart,
    });
    console.log(JSON.stringify(launched, null, 2));
    return 0;
  }

  if (command === "stop") {
    const res = await stopDevEnvironment({ repoRoot });
    console.log(JSON.stringify(res, null, 2));
    return 0;
  }

  if (command === "open") {
    const session = readActiveSessionFile(repoRoot);
    if (!session?.chromeUrl) {
      throw new Error("No active dev session found. Run 'start' first.");
    }
    const launched = openInChrome(session.chromeUrl);
    console.log(JSON.stringify({ chromeUrl: session.chromeUrl, ...launched }, null, 2));
    return 0;
  }

  if (command === "status") {
    const endpoint = resolveBackendEndpoint({
      explicitWsUrl: opts.wsUrl,
      repoRoot,
    });
    const session = readActiveSessionFile(repoRoot);
    const exeDetected = locateBackendExecutable({ repoRoot });
    const client = new BackendDebugClient({ repoRoot, wsUrl: opts.wsUrl });

    try {
      await client.ensureConnected({ wsUrl: opts.wsUrl, timeoutMs: 2500 });
      const summary = buildStatusSummary(client);
      console.log(
        JSON.stringify(
          {
            ...summary,
            backendExecutable: exeDetected,
          },
          null,
          2
        )
      );
      return 0;
    } catch (err) {
      console.log(
        JSON.stringify(
          {
            connected: false,
            wsUrl: endpoint.wsUrl,
            endpointSource: endpoint.source,
            sessionMeta: session,
            backendExecutable: exeDetected,
            error: err instanceof Error ? err.message : String(err),
          },
          null,
          2
        )
      );
      return 0;
    } finally {
      client.releaseIfNoBrowserPresenter();
      client.disconnect("CLI status check complete");
    }
  }

  throw new Error(`Unknown command '${command}'. Run with --help for usage.`);
}

if (process.argv[1] && pathToFileURL(process.argv[1]).href === import.meta.url) {
  runCli().then(
    (code) => process.exit(code ?? 0),
    (err) => {
      console.error(`[animean-dev] ${err instanceof Error ? err.message : String(err)}`);
      process.exit(1);
    }
  );
}
