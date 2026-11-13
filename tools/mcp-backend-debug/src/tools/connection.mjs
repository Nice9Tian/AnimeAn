import { resolveBackendEndpoint } from "../discovery.mjs";
import { startDevEnvironment } from "../launcher.mjs";
import { buildStatusSummary, withBackendClient } from "./helpers.mjs";

export const connectionTools = [
  {
    name: "backend_status",
    description:
      "Connect (or inspect) the live AnimeAnBackend session and return endpoint discovery info, active tool, frame/layer selection, open modal dialogs, history status, board sizes, and recent pythonDebug summary.",
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
    inputSchema: {
      type: "object",
      properties: {
        wsUrl: {
          type: "string",
          description: "Optional explicit WebSocket URL override (ws://127.0.0.1:<port>/?token=...).",
        },
        connect: {
          type: "boolean",
          description: "Whether to connect to the backend if not yet connected (default: true).",
        },
      },
    },
    async handler(args, context) {
      const shouldConnect = args.connect !== false;
      if (!shouldConnect) {
        return buildStatusSummary(context.client);
      }
      return withBackendClient(context, { wsUrl: args.wsUrl }, async (client) => buildStatusSummary(client));
    },
  },
  {
    name: "backend_connect",
    description:
      "Connect or switch the debug observer to an AnimeAnBackend WebSocket URL (or re-run auto-discovery).",
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
    inputSchema: {
      type: "object",
      properties: {
        wsUrl: {
          type: "string",
          description: "Optional explicit ws://127.0.0.1:<port>/?token=... URL. Omit to reconnect current/discovered target.",
        },
        forceReconnect: {
          type: "boolean",
          description: "Force closing any existing socket and re-handshaking (default: true).",
        },
        rediscover: {
          type: "boolean",
          description: "Clear any explicit wsUrl override and re-run automatic session discovery (default: false).",
        },
      },
    },
    async handler(args, context) {
      const { client } = context;
      if (args.rediscover) {
        client.explicitWsUrl = null;
      }
      const forceReconnect = args.forceReconnect !== false;
      const connInfo = await client.ensureConnected({
        wsUrl: args.wsUrl,
        forceReconnect,
      });
      const summary = buildStatusSummary(client);
      const released = !client.traceActive && !client.isBrowserSized();
      client.releaseIfNoBrowserPresenter();
      return {
        ...connInfo,
        releasedUntilBrowserConnects: released,
        summary,
      };
    },
  },
  {
    name: "backend_launch_dev",
    description:
      "Start or reuse the local AnimeAnBackend.exe + Vite frontend dev environment and write shared session metadata so Chrome and any AI agent connect to the same instance.",
    annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: true },
    inputSchema: {
      type: "object",
      properties: {
        port: {
          type: "number",
          description: "Backend WebSocket port (default: 3432; 0 picks a random free port).",
        },
        token: {
          type: "string",
          description: "Backend auth token (default: a fresh random token per launch, recorded in the session file).",
        },
        frontendPort: {
          type: "number",
          description: "Vite dev server port (default: 1430).",
        },
        backendExe: {
          type: "string",
          description: "Optional explicit path to AnimeAnBackend.exe.",
        },
        startVite: {
          type: "boolean",
          description: "Also start or reuse Vite dev server (default: true).",
        },
        openBrowser: {
          type: "boolean",
          description: "Open the Chrome URL automatically on the host desktop (default: false).",
        },
        restart: {
          type: "boolean",
          description: "Stop any existing managed dev session before launching fresh processes (default: false).",
        },
      },
    },
    async handler(args, context) {
      const launched = await startDevEnvironment({
        repoRoot: context.client.paths.repoRoot,
        port: args.port,
        token: args.token,
        frontendPort: args.frontendPort,
        backendExe: args.backendExe,
        startVite: args.startVite,
        openBrowser: args.openBrowser,
        restart: args.restart,
      });
      const resolved = resolveBackendEndpoint({ repoRoot: context.client.paths.repoRoot });
      return {
        ...launched,
        discoveredBy: resolved.source,
      };
    },
  },
];
