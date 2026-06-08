# Agent Notes

* 若需要验证编译，不要直接执行 `deploy_AnimeAn` 作为默认检查目标；该目标会触发 Qt 部署流程，可能导致 Agent 无法正确判断任务结束。

* 默认应使用 wrapper 脚本验证主程序目标是否能正常构建并退出：

  ```powershell
  powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File C:\Users\admin\Documents\AnimeAn\scripts\agent_build.ps1
  ```

* `agent_build.ps1` 会构建 `AnimeAn` 目标，并在结束时打印：

  ```text
  ===== AGENT BUILD DONE EXIT_CODE=... =====
  ```

* 若 `EXIT_CODE=0`，说明主程序编译正常。

* 若 Agent 显示任务长时间 Running，应先检查是否存在属于本项目的残留 `cmake` / `ninja` / `windeployqt` 等进程。只有在确认这些进程属于当前项目且已经卡住时，才可以结束它们。

* 发布或打包时才手动执行 `deploy_AnimeAn` 目标，不建议让 Agent 默认执行 deploy 目标。
