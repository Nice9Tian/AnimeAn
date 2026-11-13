import { resolveBackendEndpoint } from "../discovery.mjs";

/**
 * Run a tool callback against a connected BackendDebugClient and automatically
 * release pre-browser observer connections so Chrome keeps presenter ownership.
 * @param {{ client: import("../backend_client.mjs").BackendDebugClient }} context
 * @param {{ wsUrl?: string, forceReconnect?: boolean }} [options]
 * @param {(client: import("../backend_client.mjs").BackendDebugClient, connInfo: any) => Promise<any>} fn
 */
export async function withBackendClient(context, options, fn) {
  const { client } = context;
  const connInfo = await client.ensureConnected(options || {});
  try {
    return await fn(client, connInfo);
  } finally {
    client.releaseIfNoBrowserPresenter();
  }
}

/**
 * Summarize the live backend state from cached WebSocket state & frame maps.
 * @param {import("../backend_client.mjs").BackendDebugClient} client
 */
export function buildStatusSummary(client) {
  const endpoint = resolveBackendEndpoint({
    explicitWsUrl: client.explicitWsUrl || undefined,
    repoRoot: client.paths.repoRoot,
  });

  const win = client.stateMap.get("window") || {};
  const tools = client.stateMap.get("tools") || {};
  const timeline = client.stateMap.get("timeline") || {};
  const history = client.stateMap.get("history") || {};
  const dialogs = Array.isArray(client.stateMap.get("dialogs")) ? client.stateMap.get("dialogs") : [];
  const mainLayers = client.stateMap.get("layers/main") || {};
  const childLayers = client.stateMap.get("layers/child") || {};
  const drawColor = client.stateMap.get("drawColor") || null;
  const drawSettings = client.stateMap.get("drawSettings") || null;

  const mainBoard = client.boardFrames.get("main");
  const childBoard = client.boardFrames.get("child");
  const pythonDebug = Array.isArray(win.pythonDebug) ? win.pythonDebug : [];
  const labels = Array.isArray(history.labels) ? history.labels : [];
  const curHistoryIdx = typeof history.currentIndex === "number" ? history.currentIndex : -1;

  return {
    connected: client.isConnected(),
    wsUrl: client.wsUrl || endpoint.wsUrl,
    endpointSource: client.endpointSource || endpoint.source,
    connectedAt: client.connectedAt,
    sessionMeta: client.sessionMeta || endpoint.sessionMeta,
    browserSized: client.isBrowserSized(),
    traceActive: client.traceActive,
    window: {
      title: win.title ?? null,
      status: win.status ?? null,
      activeView: win.activeView ?? null,
      assetView: win.assetView ?? null,
      canvasWidth: win.canvasWidth ?? null,
      canvasHeight: win.canvasHeight ?? null,
      currentFilePath: win.currentFilePath ?? null,
      childFilePath: win.childFilePath ?? null,
    },
    tool: {
      tool: tools.tool ?? null,
      toolEnum: tools.toolEnum ?? null,
      activeExtraTool: tools.activeExtraTool || null,
      activeExtraToolProperty: tools.activeExtraToolProperty || null,
      page: tools.page ?? null,
      lockedTools: tools.lockedTools ?? [],
    },
    drawColor,
    drawSettings,
    selection: {
      frameCount: timeline.frameCount ?? null,
      currentFrame: timeline.currentFrame ?? null,
      playbackActive: timeline.playbackActive ?? false,
      onionEnabled: timeline.onionEnabled ?? false,
      mainLayerSelected: mainLayers.selected ?? null,
      childLayerSelected: childLayers.selected ?? null,
    },
    dialogs: dialogs.map((d) => ({
      id: d.id,
      kind: d.kind,
      title: d.title,
      modal: Boolean(d.modal),
    })),
    history: {
      view: history.view ?? null,
      count: labels.length,
      currentIndex: curHistoryIdx,
      latestLabel: curHistoryIdx >= 0 && curHistoryIdx < labels.length ? labels[curHistoryIdx] : null,
      canUndo: Boolean(history.canUndo),
      canRedo: Boolean(history.canRedo),
    },
    boards: {
      main: mainBoard
        ? { frame: mainBoard.frame, width: mainBoard.w, height: mainBoard.h, totalOps: mainBoard.totalOps }
        : null,
      child: childBoard
        ? { frame: childBoard.frame, width: childBoard.w, height: childBoard.h, totalOps: childBoard.totalOps }
        : null,
    },
    pythonDebugLineCount: pythonDebug.length,
    lastPythonDebugLines: pythonDebug.slice(-5),
    recentUiEventCount: client.recentEvents.length,
    stateKeysLoaded: Array.from(client.stateMap.keys()),
    lastError: client.lastError,
  };
}
