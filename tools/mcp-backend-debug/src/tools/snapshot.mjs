import fs from "node:fs";
import path from "node:path";
import { encodeRgbaToPng } from "../png.mjs";
import { buildStatusSummary, withBackendClient } from "./helpers.mjs";

export const snapshotTools = [
  {
    name: "render_frame_png",
    description:
      "Render a backend frame via timeline.thumbnails (offscreen RasterDevice of real scene model), encode it to a PNG file in .scratch/mcp-snapshots/, and optionally return it inline as an MCP image.",
    annotations: { readOnlyHint: true, destructiveHint: false, idempotentHint: true },
    inputSchema: {
      type: "object",
      properties: {
        view: {
          type: "string",
          enum: ["main", "child"],
          description: "Board view to render (default: 'main').",
        },
        frame: {
          type: "number",
          description: "0-based frame index (defaults to current timeline frame or 0).",
        },
        width: {
          type: "number",
          description: "Output PNG width in pixels (default: 640).",
        },
        height: {
          type: "number",
          description: "Output PNG height in pixels (default: 360).",
        },
        outputPath: {
          type: "string",
          description: "Optional explicit .png file path; defaults to .scratch/mcp-snapshots/.",
        },
        inlineImage: {
          type: "boolean",
          description: "Also attach base64 PNG image content block for multimodal models (default: false).",
        },
      },
    },
    async handler(args, context) {
      return withBackendClient(context, {}, async (client) => {
        const view = args.view === "child" ? "child" : "main";
        const timeline = client.stateMap.get("timeline") || {};
        const frame =
          args.frame !== undefined && args.frame !== null
            ? Number(args.frame)
            : Number(timeline.currentFrame ?? 0);
        const width = Math.max(16, Math.min(2048, Math.round(Number(args.width || 640))));
        const height = Math.max(16, Math.min(2048, Math.round(Number(args.height || 360))));

        const rpcRes = await client.call("timeline.thumbnails", {
          view,
          frames: [frame],
          width,
          height,
        });
        const key = rpcRes?.thumbnails?.[String(frame)];
        if (!key) {
          throw new Error(
            `Backend did not return a thumbnail key for view='${view}', frame=${frame} (${width}x${height}).`
          );
        }

        const imgEntry = await client.waitForImage(key, 10000);
        const pngBuf = encodeRgbaToPng(imgEntry.data, imgEntry.w, imgEntry.h);

        const targetPath = args.outputPath
          ? path.resolve(args.outputPath)
          : path.join(
              client.paths.snapshotsDir,
              `frame_${view}_f${frame}_${imgEntry.w}x${imgEntry.h}_${Date.now()}.png`
            );
        fs.mkdirSync(path.dirname(targetPath), { recursive: true });
        fs.writeFileSync(targetPath, pngBuf);

        const payload = {
          filePath: targetPath,
          view,
          frame,
          key,
          width: imgEntry.w,
          height: imgEntry.h,
          bytes: pngBuf.length,
        };
        if (args.inlineImage) {
          payload.__mcpImage = {
            base64: pngBuf.toString("base64"),
            mimeType: "image/png",
          };
        }
        return payload;
      });
    },
  },
  {
    name: "export_repro_bundle",
    description:
      "Save a complete self-contained JSON reproduction bundle (main & child scenes, non-empty cells, scriptData, registered hooks, hook trace buffer, live backend state, board draw-list summaries, and pythonDebug) for offline analysis or regression test creation.",
    annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: false },
    inputSchema: {
      type: "object",
      properties: {
        label: {
          type: "string",
          description: "Short label embedded in the bundle filename (e.g. 'curve_gap' or 'am_fold').",
        },
        outputPath: {
          type: "string",
          description: "Optional explicit output .json path; defaults to .scratch/mcp-snapshots/repro_<ts>.json.",
        },
        includeCommands: {
          type: "boolean",
          description: "Include full stroke Bezier commands in exported cells (default: true).",
        },
        // No .anproj option: the only save call, project.saveAs, makes the
        // saved file the project's own path, so the user's next Save would go
        // to the bundle instead of their file.
      },
    },
    async handler(args, context) {
      return withBackendClient(context, {}, async (client) => {
        const probeDump = await client.callProbe("export_repro_data", {
          includeCommands: args.includeCommands !== false,
          toPoly: false,
        });

        const stateSnapshot = {};
        for (const [k, v] of client.stateMap.entries()) {
          stateSnapshot[k] = v;
        }

        const bundle = {
          schemaVersion: 1,
          label: args.label || null,
          summary: buildStatusSummary(client),
          ...probeDump,
          boardDrawLists: {
            main: client.summarizeBoardDrawList("main", { limit: 0 }),
            child: client.summarizeBoardDrawList("child", { limit: 0 }),
          },
          recentEvents: client.recentEvents.slice(),
          state: stateSnapshot,
        };

        const safeLabel = String(args.label || "repro")
          .replace(/[^a-zA-Z0-9_-]+/g, "_")
          .replace(/^_+|_+$/g, "");
        const stamp = new Date().toISOString().replace(/[:.]/g, "-");
        const targetPath = args.outputPath
          ? path.resolve(args.outputPath)
          : path.join(client.paths.snapshotsDir, `${safeLabel || "repro"}_${stamp}.json`);
        fs.mkdirSync(path.dirname(targetPath), { recursive: true });
        const jsonText = JSON.stringify(bundle, null, 2);
        fs.writeFileSync(targetPath, jsonText, "utf-8");

        return {
          filePath: targetPath,
          bytes: Buffer.byteLength(jsonText, "utf-8"),
          views: Object.keys(probeDump.views || {}),
          mainNonEmptyCells: probeDump.views?.main?.nonEmptyCells?.length ?? 0,
          childNonEmptyCells: probeDump.views?.child?.nonEmptyCells?.length ?? 0,
          hookTraceEvents: probeDump.hookTrace?.events?.length ?? 0,
        };
      });
    },
  },
];
