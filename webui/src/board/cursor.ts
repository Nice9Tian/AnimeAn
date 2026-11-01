export interface BoardCursor {
  key?: string;
  css?: string;
  hx?: number;
  hy?: number;
  scale?: number;
}

// The bitmap has the screen's pixels and the hotspot is in CSS pixels. A bare
// url() counts each bitmap pixel as one CSS pixel, so on a scaled screen the
// pen ring came out too large with the hotspot off its centre.
export function cursorCss(value: BoardCursor | undefined, url: string | null | undefined): string {
  const fallback = value?.css ?? "default";
  if (!url) return fallback;
  return `image-set(url(${url}) ${value?.scale ?? 1}x) ${value?.hx ?? 0} ${value?.hy ?? 0}, ${fallback}`;
}
