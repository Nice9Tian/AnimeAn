// 调色盘自绘效果（工具端 Canvas 2D 实现）。
// 导出到 Qt 时在 ui_effects/ColorPickerDrawEffect.h/.cpp 中用 QPainter + QLinearGradient 翻译。
export default function paint(el, { props, width, height }) {
  const w = Math.max(40, Math.round(width || el.clientWidth || 228));
  const h = Math.max(40, Math.round(height || el.clientHeight || 112));

  let canvas = el.querySelector("canvas[data-vf-color-picker]");
  if (!canvas) {
    canvas = document.createElement("canvas");
    canvas.dataset.vfColorPicker = "1";
    canvas.style.cssText = "display:block;width:100%;height:100%;border-radius:6px;";
    el.replaceChildren(canvas);
  }
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
  }

  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  ctx.clearRect(0, 0, w, h);

  const hue = Math.max(0, Math.min(360, Number(props?.hue ?? 210)));
  const sat = Math.max(0, Math.min(100, Number(props?.saturation ?? 78)));
  const val = Math.max(0, Math.min(100, Number(props?.brightness ?? 92)));
  const hexColor = typeof props?.hexColor === "string" && props.hexColor ? props.hexColor : "#3394eb";

  const pad = 6;
  const barH = 22;
  const hueBarW = 18;
  const svX = pad;
  const svY = pad;
  const svW = Math.max(20, w - pad * 3 - hueBarW);
  const svH = Math.max(20, h - pad * 3 - barH);

  // 背景底板
  ctx.fillStyle = "#0f172a";
  ctx.fillRect(0, 0, w, h);

  // 1. 饱和度-明度 (SV) 方块
  ctx.fillStyle = `hsl(${hue}, 100%, 50%)`;
  ctx.fillRect(svX, svY, svW, svH);

  const whiteGrad = ctx.createLinearGradient(svX, svY, svX + svW, svY);
  whiteGrad.addColorStop(0, "rgba(255,255,255,1)");
  whiteGrad.addColorStop(1, "rgba(255,255,255,0)");
  ctx.fillStyle = whiteGrad;
  ctx.fillRect(svX, svY, svW, svH);

  const blackGrad = ctx.createLinearGradient(svX, svY, svX, svY + svH);
  blackGrad.addColorStop(0, "rgba(0,0,0,0)");
  blackGrad.addColorStop(1, "rgba(0,0,0,1)");
  ctx.fillStyle = blackGrad;
  ctx.fillRect(svX, svY, svW, svH);

  ctx.strokeStyle = "#334155";
  ctx.lineWidth = 1;
  ctx.strokeRect(svX + 0.5, svY + 0.5, svW - 1, svH - 1);

  // SV 十字准星
  const cx = svX + (sat / 100) * svW;
  const cy = svY + (1 - val / 100) * svH;
  ctx.beginPath();
  ctx.arc(cx, cy, 5, 0, Math.PI * 2);
  ctx.strokeStyle = "#0f172a";
  ctx.lineWidth = 2.5;
  ctx.stroke();
  ctx.beginPath();
  ctx.arc(cx, cy, 5, 0, Math.PI * 2);
  ctx.strokeStyle = "#ffffff";
  ctx.lineWidth = 1.5;
  ctx.stroke();

  // 2. 右侧色相条 (Hue Bar)
  const hx = svX + svW + pad;
  const hy = svY;
  const hueGrad = ctx.createLinearGradient(hx, hy, hx, hy + svH);
  hueGrad.addColorStop(0, "#ff0000");
  hueGrad.addColorStop(1 / 6, "#ffff00");
  hueGrad.addColorStop(2 / 6, "#00ff00");
  hueGrad.addColorStop(3 / 6, "#00ffff");
  hueGrad.addColorStop(4 / 6, "#0000ff");
  hueGrad.addColorStop(5 / 6, "#ff00ff");
  hueGrad.addColorStop(1, "#ff0000");
  ctx.fillStyle = hueGrad;
  ctx.fillRect(hx, hy, hueBarW, svH);
  ctx.strokeStyle = "#334155";
  ctx.lineWidth = 1;
  ctx.strokeRect(hx + 0.5, hy + 0.5, hueBarW - 1, svH - 1);

  const hueY = hy + (hue / 360) * svH;
  ctx.fillStyle = "#ffffff";
  ctx.fillRect(hx - 1, hueY - 2, hueBarW + 2, 4);
  ctx.strokeStyle = "#0f172a";
  ctx.strokeRect(hx - 1, hueY - 2, hueBarW + 2, 4);

  // 3. 底部当前颜色预览条
  const by = svY + svH + pad;
  const bw = w - pad * 2;
  ctx.fillStyle = hexColor;
  ctx.fillRect(svX, by, 44, barH);
  ctx.strokeStyle = "#475569";
  ctx.strokeRect(svX + 0.5, by + 0.5, 43, barH - 1);

  ctx.fillStyle = "#cbd5e1";
  ctx.font = "600 11px sans-serif";
  ctx.textBaseline = "middle";
  ctx.fillText(`HSV(${Math.round(hue)}, ${Math.round(sat)}%, ${Math.round(val)}%)  ${hexColor}`, svX + 52, by + barH / 2);
}
