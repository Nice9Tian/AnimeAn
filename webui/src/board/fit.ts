// How a frame of the backend's board sits in this page's board element.
//
// A board has one size at the backend: that of the page being worked in (see
// "多个页面" in docs/mechanism/backend_protocol.md). On that page a frame is
// the size of the element and fills it. On every other page the same frame is
// shown whole, scaled to fit and centred, and the pointer is taken back
// through the same scale, so that it lands where it is seen.

export interface FrameSize {
  w: number;
  h: number;
}

export interface FrameFit {
  // False: the frame is this element's own size.
  mirrored: boolean;
  // Element pixels per board pixel.
  scale: number;
  // Where the frame's top left corner is in the element, in element pixels.
  offsetX: number;
  offsetY: number;
}

export const OWN_SIZE: FrameFit = Object.freeze({ mirrored: false, scale: 1, offsetX: 0, offsetY: 0 });

// The size a page reports is in whole pixels, so its own frame can be this far
// from the element's measured size.
const OWN_SIZE_SLACK = 1;

export function fitFrame(frameW: number, frameH: number, boxW: number, boxH: number): FrameFit {
  if (!(frameW > 0 && frameH > 0 && boxW > 0 && boxH > 0)) return OWN_SIZE;
  if (Math.abs(frameW - boxW) <= OWN_SIZE_SLACK && Math.abs(frameH - boxH) <= OWN_SIZE_SLACK) return OWN_SIZE;
  const scale = Math.min(boxW / frameW, boxH / frameH);
  return {
    mirrored: true,
    scale,
    offsetX: (boxW - frameW * scale) / 2,
    offsetY: (boxH - frameH * scale) / 2,
  };
}

// A point of the element, as the board's own coordinates.
export function toBoardPoint(fit: FrameFit, x: number, y: number): { x: number; y: number } {
  if (!fit.mirrored) return { x, y };
  return { x: (x - fit.offsetX) / fit.scale, y: (y - fit.offsetY) / fit.scale };
}
