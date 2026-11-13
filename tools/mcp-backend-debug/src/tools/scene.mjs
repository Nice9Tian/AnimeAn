import { withBackendClient } from "./helpers.mjs";

export const sceneTools = [
  {
    name: "inspect_scene",
    description:
      "Inspect live C++ AnimeSceneModel structure across main and/or child boards: canvas size, frames, layers, layer tree, assets, non-empty cells summary, scriptData key sizes, overlay owners, registered hooks, and fieldDisplaySupported.",
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
    inputSchema: {
      type: "object",
      properties: {
        view: {
          type: "string",
          enum: ["main", "child", "both"],
          description: "Which scene view(s) to inspect (default: 'both').",
        },
        includeEmptyCells: {
          type: "boolean",
          description: "Include empty cells in the cell list (default: false).",
        },
      },
    },
    async handler(args, context) {
      return withBackendClient(context, {}, async (client) => {
        const views = args.view === "main" || args.view === "child" ? [args.view] : ["main", "child"];
        return client.callProbe("inspect_scene", {
          views,
          includeEmptyCells: Boolean(args.includeEmptyCells),
        });
      });
    },
  },
  {
    name: "inspect_cell_geometry",
    description:
      "Extract exact vector strokes (Bezier commands or sampled polylines, width, color, property, bounds, endpoints, closedness), fill regions (seed, color, bounds, source layer), and raster metadata for a cell.",
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
    inputSchema: {
      type: "object",
      properties: {
        view: {
          type: "string",
          enum: ["main", "child"],
          description: "Target board view (default: 'main').",
        },
        frame: {
          type: "number",
          description: "0-based frame index (defaults to current_frame).",
        },
        layer: {
          type: "number",
          description: "0-based layer index (defaults to current_layer).",
        },
        includeCommands: {
          type: "boolean",
          description: "Include full Bezier commands or polylines for each stroke (default: true).",
        },
        toPoly: {
          type: "boolean",
          description: "Return sampled polylines instead of cubic Bezier commands (default: false).",
        },
        polyStep: {
          type: "number",
          description: "Sampling step when toPoly=true (default: 4.0).",
        },
        includeRawPoints: {
          type: "boolean",
          description: "Include raw input stroke points alongside fitted path commands (default: false).",
        },
        includeFillCommands: {
          type: "boolean",
          description: "Include full boundary path commands for fill regions (default: false).",
        },
        propertyFilter: {
          type: "string",
          description: "Only include strokes matching this stroke property string.",
        },
        strokeIndices: {
          type: "array",
          items: { type: "number" },
          description: "Optional list of 0-based stroke indices to inspect.",
        },
        maxStrokes: {
          type: "number",
          description: "Maximum strokes to return (default: 100).",
        },
      },
    },
    async handler(args, context) {
      return withBackendClient(context, {}, async (client) => {
        return client.callProbe("inspect_cell_geometry", {
          view: args.view === "child" ? "child" : "main",
          frame: args.frame ?? null,
          layer: args.layer ?? null,
          includeCommands: args.includeCommands !== false,
          toPoly: Boolean(args.toPoly),
          polyStep: args.polyStep ?? 4.0,
          includeRawPoints: Boolean(args.includeRawPoints),
          includeFillCommands: Boolean(args.includeFillCommands),
          propertyFilter: args.propertyFilter ?? null,
          strokeIndices: Array.isArray(args.strokeIndices) ? args.strokeIndices : null,
          maxStrokes: args.maxStrokes ?? 100,
        });
      });
    },
  },
  {
    name: "inspect_automapping",
    description:
      "Inspect Auto Mapping state across main & child scenes: scriptData (mapping units, assets, ground lines, curve/round-corner store), guide/property-tagged strokes, overlay stack items, and field_display_supported.",
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
    inputSchema: {
      type: "object",
      properties: {
        view: {
          type: "string",
          enum: ["main", "child", "both"],
          description: "Which scene view(s) to inspect (default: 'both').",
        },
        frame: {
          type: "number",
          description: "0-based frame index for guide stroke lookup (defaults to current_frame).",
        },
        keys: {
          type: "array",
          items: { type: "string" },
          description: "Optional scriptData top-level keys to filter.",
        },
        fullArrays: {
          type: "boolean",
          description: "Return long numeric arrays verbatim instead of summaries (default: false).",
        },
        maxDepth: {
          type: "number",
          description: "Maximum object nesting depth before summarizing (default: 6).",
        },
      },
    },
    async handler(args, context) {
      return withBackendClient(context, {}, async (client) => {
        const views = args.view === "main" || args.view === "child" ? [args.view] : ["main", "child"];
        return client.callProbe("inspect_automapping", {
          views,
          frame: args.frame ?? null,
          keys: Array.isArray(args.keys) ? args.keys : null,
          fullArrays: Boolean(args.fullArrays),
          maxDepth: args.maxDepth ?? 6,
        });
      });
    },
  },
];
