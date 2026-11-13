import { withBackendClient } from "./helpers.mjs";

export const actionTools = [
  {
    name: "eval_python",
    description:
      "Execute a Python expression or statement inside AnimeAnBackend's live embedded Python interpreter (access to main_model, child_model, animean_python, ui, pyfile modules). CAUTION: BackendSession finishes an active pen drag if python.run is called while the user has a stylus/mouse button pressed.",
    annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: false },
    inputSchema: {
      type: "object",
      properties: {
        command: {
          type: "string",
          description: "Python expression/statement (or '<script>.py') to evaluate in __main__.",
        },
        scriptDir: {
          type: "string",
          description: "Optional extra directory prepended to sys.path for this call.",
        },
      },
      required: ["command"],
    },
    async handler(args, context) {
      if (!args.command || !String(args.command).trim()) {
        throw new Error("'command' must be a non-empty Python expression or statement");
      }
      return withBackendClient(context, {}, async (client) => {
        const res = await client.call("python.run", {
          command: String(args.command),
          ...(args.scriptDir ? { scriptDir: String(args.scriptDir) } : {}),
        });
        return {
          output: res?.output ?? "",
        };
      });
    },
  },
  {
    name: "call_backend_rpc",
    description:
      "Invoke a BackendSession JSON-RPC method directly (e.g. tool.select, history.undo, history.redo, layer.select, frame.select, menu.trigger, dialog.result). Never use to inject input/resize/capabilities.",
    annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: false },
    inputSchema: {
      type: "object",
      properties: {
        method: {
          type: "string",
          description: "Backend RPC method name (see BackendSession::dispatchCall).",
        },
        params: {
          type: "object",
          description: "JSON parameters object for the RPC method.",
        },
      },
      required: ["method"],
    },
    async handler(args, context) {
      if (!args.method || !String(args.method).trim()) {
        throw new Error("'method' is required");
      }
      return withBackendClient(context, {}, async (client) => {
        const result = await client.call(String(args.method), args.params || {});
        return {
          method: args.method,
          result,
        };
      });
    },
  },
];
