// 根据新建项目对话框里选择的画幅预设，返回对应的画布宽高和帧率。
export default function syncPresetDimensions(state) {
  const preset = state.newProject?.preset ?? "1080p";
  if (preset === "720p") {
    return {
      "newProject.width": 1280,
      "newProject.height": 720,
      "newProject.fps": 24,
    };
  }
  if (preset === "Square") {
    return {
      "newProject.width": 1080,
      "newProject.height": 1080,
      "newProject.fps": 30,
    };
  }
  return {
    "newProject.width": 1920,
    "newProject.height": 1080,
    "newProject.fps": 24,
  };
}
