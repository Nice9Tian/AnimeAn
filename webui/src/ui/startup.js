// Whether the page opens the New Project dialog for this first `window` state.
// A fresh backend has no file; a backend that already holds a project (opened
// before the page connected, or a page reload) keeps it instead of being
// offered a blank canvas over it.
export function asksForCanvasOnStartup(windowState) {
  if (!windowState?.canvasWidth) return false;
  return !windowState.currentFilePath;
}
