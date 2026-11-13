import { withBackendClient } from "./helpers.mjs";

export const stateTools = [
  {
    name: "get_live_state",
    description:
      "Read live backend pushed state keys (window, tools, toolOptions, drawColor, drawSettings, layers/main, layers/child, timeline, history, dialogs, windows, viewport/*, py/*) plus recent transient UI events.",
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
    inputSchema: {
      type: "object",
      properties: {
        keys: {
          type: "array",
          items: { type: "string" },
          description: "Specific state keys to return. Omit to return a summary of all keys (or full dump with includeFull=true).",
        },
        includeFull: {
          type: "boolean",
          description: "When 'keys' is omitted, return full values for all state keys instead of a compact index (default: false).",
        },
        includeEvents: {
          type: "boolean",
          description: "Include recent transient 'event' messages (board.effect, contextMenu, etc.) (default: true).",
        },
        eventLimit: {
          type: "number",
          description: "Maximum recent UI events to include (default: 20).",
        },
      },
    },
    async handler(args, context) {
      return withBackendClient(context, {}, async (client) => {
        const wantedKeys = Array.isArray(args.keys) && args.keys.length > 0 ? args.keys : null;
        const includeFull = Boolean(args.includeFull);
        const includeEvents = args.includeEvents !== false;
        const eventLimit = Math.max(1, Math.min(100, Number(args.eventLimit || 20)));

        const state = {};
        if (wantedKeys) {
          for (const key of wantedKeys) {
            state[key] = client.stateMap.has(key) ? client.stateMap.get(key) : null;
          }
        } else if (includeFull) {
          for (const [key, val] of client.stateMap.entries()) {
            state[key] = val;
          }
        } else {
          for (const [key, val] of client.stateMap.entries()) {
            if (Array.isArray(val)) {
              state[key] = { type: "array", length: val.length };
            } else if (val && typeof val === "object") {
              const objKeys = Object.keys(val);
              state[key] = { type: "object", keyCount: objKeys.length, keys: objKeys.slice(0, 20) };
            } else {
              state[key] = val;
            }
          }
        }

        return {
          availableKeys: Array.from(client.stateMap.keys()),
          state,
          ...(includeEvents ? { recentEvents: client.recentEvents.slice(-eventLimit) } : {}),
        };
      });
    },
  },
  {
    name: "get_python_debug_logs",
    description:
      "Read lines from AnimeAnBackend's window.pythonDebug ring buffer (up to 2000 entries), with optional regex/substring filtering and error-only mode.",
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
    inputSchema: {
      type: "object",
      properties: {
        limit: {
          type: "number",
          description: "Maximum trailing entries to return after filtering (default: 80).",
        },
        pattern: {
          type: "string",
          description: "Optional case-insensitive substring or regex pattern to filter log entries.",
        },
        errorsOnly: {
          type: "boolean",
          description: "Return only entries mentioning Traceback, Error, Exception, or [mcp_probe] err (default: false).",
        },
      },
    },
    async handler(args, context) {
      return withBackendClient(context, {}, async (client) => {
        const win = client.stateMap.get("window") || {};
        const allLines = Array.isArray(win.pythonDebug) ? win.pythonDebug : [];
        const errorsOnly = Boolean(args.errorsOnly);
        const limit = Math.max(1, Math.min(2000, Number(args.limit || 80)));

        let regex = null;
        if (args.pattern) {
          try {
            regex = new RegExp(String(args.pattern), "i");
          } catch {
            regex = null;
          }
        }

        const errorPattern = /traceback|error|exception|failed|\[mcp_probe\]\s*err/i;
        const filtered = [];
        for (let i = 0; i < allLines.length; i += 1) {
          const line = String(allLines[i]);
          if (errorsOnly && !errorPattern.test(line)) continue;
          if (args.pattern) {
            const matched = regex
              ? regex.test(line)
              : line.toLowerCase().includes(String(args.pattern).toLowerCase());
            if (!matched) continue;
          }
          filtered.push({ index: i, text: line });
        }

        const entries = filtered.slice(-limit);
        return {
          totalEntries: allLines.length,
          matchedCount: filtered.length,
          returnedCount: entries.length,
          entries,
        };
      });
    },
  },
  {
    name: "get_board_drawlist_summary",
    description:
      "Inspect the rendered draw-list sent by BoardSurface/RecordingDevice for 'main' or 'child' board (dimensions, frame id, op histogram T/P/B/G/S/F, active deformation field scopes, and optional op slice).",
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
    inputSchema: {
      type: "object",
      properties: {
        view: {
          type: "string",
          enum: ["main", "child"],
          description: "Target board view (default: 'main').",
        },
        offset: {
          type: "number",
          description: "Start index into reconstructed ops array when requesting raw ops (default: 0).",
        },
        limit: {
          type: "number",
          description: "Number of raw ops to return (0 returns histogram & field scopes only; max 500; default: 0).",
        },
      },
    },
    async handler(args, context) {
      return withBackendClient(context, {}, async (client) => {
        const view = args.view === "child" ? "child" : "main";
        return client.summarizeBoardDrawList(view, {
          offset: args.offset,
          limit: args.limit,
        });
      });
    },
  },
];
