// 处理工具选项面板（DataForm）字段变化，更新参数字典并同步状态栏摘要。
export default function applyToolOption(state, args) {
  const fieldName = String(args?.name ?? "");
  const fieldVal = args?.value ?? null;
  const hook = String(args?.hook ?? fieldName);
  const prevValues = state.toolOptions?.values ?? {};
  const nextValues = Object.assign({}, prevValues);
  if (fieldName) {
    nextValues[fieldName] = fieldVal;
  }
  return {
    "toolOptions.values": nextValues,
    "toolOptions.lastHook": hook,
    "project.statusMessage": "工具参数更新：" + fieldName + " = " + String(fieldVal),
  };
}
