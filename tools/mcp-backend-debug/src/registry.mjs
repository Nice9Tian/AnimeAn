/**
 * Declarative registry for MCP tool definitions.
 *
 * Adding a tool only requires registering an object of shape:
 * `{ name, description, inputSchema, annotations?, handler }`.
 */
export class ToolRegistry {
  constructor() {
    this.tools = new Map();
  }

  /**
   * Register one tool definition (overwrites if same name already registered).
   * @param {{
   *   name: string,
   *   description: string,
   *   inputSchema: Record<string, any>,
   *   annotations?: Record<string, any>,
   *   handler: (args: Record<string, any>, context: any) => Promise<any> | any
   * }} toolDef
   */
  register(toolDef) {
    if (!toolDef || !toolDef.name || typeof toolDef.handler !== "function") {
      throw new Error("Invalid tool definition: requires 'name' and callable 'handler'");
    }
    this.tools.set(toolDef.name, toolDef);
    return this;
  }

  /**
   * Register an array of tool definitions.
   * @param {Array<any>} toolDefs
   */
  registerAll(toolDefs) {
    for (const def of toolDefs) {
      this.register(def);
    }
    return this;
  }

  /**
   * Export tool descriptors in MCP `tools/list` schema format.
   */
  listTools() {
    return Array.from(this.tools.values()).map((tool) => ({
      name: tool.name,
      description: tool.description,
      inputSchema: tool.inputSchema || { type: "object", properties: {} },
      ...(tool.annotations ? { annotations: tool.annotations } : {}),
    }));
  }

  /**
   * Invoke a registered tool and format MCP `tools/call` result blocks.
   * @param {string} name
   * @param {Record<string, any>} args
   * @param {any} context
   */
  async callTool(name, args = {}, context = {}) {
    const tool = this.tools.get(name);
    if (!tool) {
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify({ error: `Unknown tool: ${name}` }, null, 2),
          },
        ],
        isError: true,
      };
    }

    try {
      const result = await tool.handler(args || {}, context);
      const content = [];

      if (result && typeof result === "object" && result.__mcpImage) {
        const { __mcpImage, ...rest } = result;
        content.push({
          type: "text",
          text: JSON.stringify(rest, null, 2),
        });
        content.push({
          type: "image",
          data: __mcpImage.base64,
          mimeType: __mcpImage.mimeType || "image/png",
        });
      } else {
        content.push({
          type: "text",
          text: typeof result === "string" ? result : JSON.stringify(result, null, 2),
        });
      }

      return {
        content,
        isError: false,
      };
    } catch (err) {
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                error: err instanceof Error ? err.message : String(err),
              },
              null,
              2
            ),
          },
        ],
        isError: true,
      };
    }
  }
}
