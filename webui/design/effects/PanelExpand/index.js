// 面板展开和收起的过渡。
// 显示：高度从 0 变到原本的高度，同时从透明变到不透明。隐藏：反过来。
// 时长 180 毫秒，先快后慢（三次方缓出）。过渡期间超出当前高度的内容被裁掉。
const DURATION_MS = 180;
const EASE_OUT_CUBIC = "cubic-bezier(0.33, 1, 0.68, 1)";

export default function play(el, { showing }) {
  // 上一次过渡还没放完就又来了：停掉旧的，从完整的样子重新开始
  for (const running of el.getAnimations()) running.cancel();

  const fullHeight = el.offsetHeight;
  const closed = { height: "0px", opacity: 0, overflow: "hidden" };
  const open = { height: `${fullHeight}px`, opacity: 1, overflow: "hidden" };
  const animation = el.animate(showing ? [closed, open] : [open, closed], {
    duration: DURATION_MS,
    easing: EASE_OUT_CUBIC,
  });
  return animation.finished;
}
