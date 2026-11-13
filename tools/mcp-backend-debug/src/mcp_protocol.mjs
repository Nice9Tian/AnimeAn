const SUPPORTED_VERSIONS = new Set(["2024-11-05", "2025-03-26", "2025-06-18"]);

/**
 * Create a transport-agnostic MCP JSON-RPC 2.0 dispatcher.
 * @param {{
 *   registry: import("./registry.mjs").ToolRegistry,
 *   context: any,
 *   serverInfo?: { name: string, version: string },
 *   instructions?: string
 * }} options
 */
export function createMcpHandler({
  registry,
  context,
  serverInfo = { name: "animean-backend-debug", version: "1.0.0" },
  instructions =
    "Inspect live AnimeAnBackend state, vector stroke geometry, Auto Mapping scriptData, draw-lists, and hook traces while the user draws in Chrome.",
}) {
  /**
   * Process one parsed JSON-RPC 2.0 request or notification.
   * Returns `null` when no response should be sent (notifications).
   * @param {any} msg
   * @returns {Promise<Record<string, any> | null>}
   */
  async function handleMessage(msg) {
    if (!msg || typeof msg !== "object" || Array.isArray(msg)) {
      return {
        jsonrpc: "2.0",
        id: null,
        error: { code: -32600, message: "Invalid Request" },
      };
    }

    const hasId = Object.prototype.hasOwnProperty.call(msg, "id") && msg.id !== undefined && msg.id !== null;
    const id = hasId ? msg.id : null;
    const method = msg.method;
    const params = msg.params || {};

    if (!method || typeof method !== "string") {
      return hasId
        ? { jsonrpc: "2.0", id, error: { code: -32600, message: "Invalid Request: missing method" } }
        : null;
    }

    // Notifications carry no request id and must not receive a JSON-RPC reply.
    if (!hasId) {
      return null;
    }

    try {
      if (method === "initialize") {
        const requestedVersion = typeof params.protocolVersion === "string" ? params.protocolVersion : "";
        const protocolVersion = SUPPORTED_VERSIONS.has(requestedVersion) ? requestedVersion : "2024-11-05";
        return {
          jsonrpc: "2.0",
          id,
          result: {
            protocolVersion,
            capabilities: {
              tools: {
                listChanged: false,
              },
            },
            serverInfo,
            instructions,
          },
        };
      }

      if (method === "ping") {
        return { jsonrpc: "2.0", id, result: {} };
      }

      if (method === "tools/list") {
        return {
          jsonrpc: "2.0",
          id,
          result: {
            tools: registry.listTools(),
          },
        };
      }

      if (method === "tools/call") {
        const toolName = params.name;
        const toolArgs = params.arguments || {};
        const result = await registry.callTool(toolName, toolArgs, context);
        return {
          jsonrpc: "2.0",
          id,
          result,
        };
      }

      if (method === "resources/list") {
        return { jsonrpc: "2.0", id, result: { resources: [] } };
      }

      if (method === "prompts/list") {
        return { jsonrpc: "2.0", id, result: { prompts: [] } };
      }

      return {
        jsonrpc: "2.0",
        id,
        error: {
          code: -32601,
          message: `Method not found: ${method}`,
        },
      };
    } catch (err) {
      return {
        jsonrpc: "2.0",
        id,
        error: {
          code: -32603,
          message: err instanceof Error ? err.message : String(err),
        },
      };
    }
  }

  /**
   * Handle either a single JSON-RPC message or a batch array.
   * @param {any} payload
   */
  async function handlePayload(payload) {
    if (Array.isArray(payload)) {
      const replies = [];
      for (const item of payload) {
        const reply = await handleMessage(item);
        if (reply !== null) {
          replies.push(reply);
        }
      }
      return replies.length > 0 ? replies : null;
    }
    return handleMessage(payload);
  }

  return { handleMessage, handlePayload };
}
