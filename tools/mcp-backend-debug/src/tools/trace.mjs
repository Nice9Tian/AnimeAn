import { withBackendClient } from "./helpers.mjs";

export const traceTools = [
  {
    name: "trace_hooks",
    description:
      "Instrument or read live python_hooks.dispatch trace ring-buffer inside AnimeAnBackend (captures event name, view, tool, property, durationMs, handled/cancel_history flags, matched hook count, exceptions, with move coalescing so pen drags never evict press/release/linefinish).",
    annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: true },
    inputSchema: {
      type: "object",
      properties: {
        action: {
          type: "string",
          enum: ["status", "start", "read", "clear", "stop"],
          description: "Trace lifecycle action (default: 'read').",
        },
        maxEvents: {
          type: "number",
          description: "Ring buffer capacity when starting (10..2000, default: 200).",
        },
        eventsFilter: {
          type: "array",
          items: { type: "string" },
          description: "Optional whitelist of hook event names (e.g. ['linefinish', 'handle', 'fillrequest']).",
        },
        coalesceMoves: {
          type: "boolean",
          description: "Coalesce consecutive handle/pad move events on the same tool (default: true).",
        },
        sinceSeq: {
          type: "number",
          description: "For 'read': only return events with seq > sinceSeq (default: 0).",
        },
        limit: {
          type: "number",
          description: "For 'read': maximum events to return (default: 100).",
        },
      },
      required: ["action"],
    },
    async handler(args, context) {
      return withBackendClient(context, {}, async (client) => {
        return client.callProbe("trace_hooks", {
          action: args.action || "read",
          maxEvents: args.maxEvents ?? 200,
          eventsFilter: Array.isArray(args.eventsFilter) ? args.eventsFilter : null,
          coalesceMoves: args.coalesceMoves !== false,
          sinceSeq: args.sinceSeq ?? 0,
          limit: args.limit ?? 100,
        });
      });
    },
  },
];
