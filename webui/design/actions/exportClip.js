// 导出当前动画镜头片段（支持同步或异步返回 Promise）。
export default async function exportClip(input) {
  const title = input?.title ?? "未命名镜头";
  const count = input?.layerCount ?? 0;
  return {
    outputPath: `exports/${title}.mp4`,
    summary: `已导出 ${title}（${count} 个图层）`,
  };
}
