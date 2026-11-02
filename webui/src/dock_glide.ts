// Panels glide into their new places when the dock layout changes shape: a
// panel docked, floated, tabbed, split, shown or hidden, and the others (the
// board included) make room over a short ease instead of jumping. Resizing
// the window or dragging a splitter only changes sizes, and stays immediate:
// there the hand already moves the edge.
//
// A gliding panel's content is laid out at its final size from the first
// frame; the panel's own box is what moves, clipping its content. Canvases in
// it keep their final pixel size meanwhile, so a board resizes once (one
// resize to the backend), not once per frame, and never shows stretched.

import type { DockLayoutSpec } from "../vf/src/dock.ts";

export const DOCK_GLIDE_MS = 240;
export const DOCK_GLIDE_EASING = "cubic-bezier(0.2, 0.7, 0.2, 1)";

export interface GlideRect {
  x: number;
  y: number;
  width: number;
  height: number;
}

// What the layout looks like, without its sizes: which visible panels sit in
// which area and group, and which float. Equal keys mean nothing was docked,
// floated, shown or hidden, so nothing should glide.
export function dockShapeKey(layout: DockLayoutSpec | undefined, isVisible: (id: string) => boolean): string {
  if (!layout) return "";
  const areas = Object.keys(layout.areas ?? {})
    .sort()
    .map((area) => {
      const groups = layout.areas[area as keyof DockLayoutSpec["areas"]]?.groups ?? [];
      return `${area}:${groups.map((g) => g.tabs.filter(isVisible).join(",")).filter(Boolean).join("|")}`;
    });
  const floating = (layout.floating ?? []).map((f) => f.dockId).filter(isVisible);
  return `${areas.join(";")}/${floating.join(",")}`;
}

export function sameRect(a: GlideRect, b: GlideRect, eps = 0.5): boolean {
  return Math.abs(a.x - b.x) < eps && Math.abs(a.y - b.y) < eps
    && Math.abs(a.width - b.width) < eps && Math.abs(a.height - b.height) < eps;
}

// The box a wrapper shows right now, mid-glide included (computed style
// follows a running animation).
export function shownRect(el: HTMLElement): GlideRect | null {
  const view = el.ownerDocument?.defaultView;
  const style = view?.getComputedStyle ? view.getComputedStyle(el) : el.style;
  const x = parseFloat(style.left);
  const y = parseFloat(style.top);
  const width = parseFloat(style.width);
  const height = parseFloat(style.height);
  if (![x, y, width, height].every(Number.isFinite)) return null;
  return { x, y, width, height };
}

const px = (r: GlideRect) => ({ left: `${r.x}px`, top: `${r.y}px`, width: `${r.width}px`, height: `${r.height}px` });

// Glides one wrapper from `from` to its current (final) inline box. Returns
// the animation, or null when the page cannot animate.
export function glideBox(el: HTMLElement, from: GlideRect, to: GlideRect): Animation | null {
  if (typeof el.animate !== "function") return null;
  const canvases = [...el.querySelectorAll("canvas")].map((canvas) => {
    const saved = { width: canvas.style.width, height: canvas.style.height };
    // Measured before the glide starts: the final layout is already applied.
    const w = canvas.clientWidth;
    const h = canvas.clientHeight;
    if (w > 0 && h > 0) {
      canvas.style.width = `${w}px`;
      canvas.style.height = `${h}px`;
    }
    return { canvas, saved };
  });
  const overflow = el.style.overflow;
  el.style.overflow = "hidden";
  const animation = el.animate([px(from), px(to)], { duration: DOCK_GLIDE_MS, easing: DOCK_GLIDE_EASING });
  const restore = () => {
    el.style.overflow = overflow;
    for (const { canvas, saved } of canvases) {
      canvas.style.width = saved.width;
      canvas.style.height = saved.height;
    }
  };
  animation.addEventListener("finish", restore);
  animation.addEventListener("cancel", restore);
  return animation;
}
