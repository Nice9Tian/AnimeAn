{
  "agent_instructions": {
    "purpose": "Verify if the release target builds and exits successfully.",
    "execution": {
      "shell": "powershell",
      "command": "PowerShell -ExecutionPolicy Bypass -File \"C:\\Users\\admin\\Documents\\AnimeAn\\scripts\\agent_build.ps1\""
    },
    "verification": {
      "expected_termination_log": "===== AGENT BUILD DONE EXIT_CODE=... =====",
      "success_criteria": {
        "required_exit_code": 0,
        "description": "An exit code of 0 indicates that the release target was successfully compiled and deployed."
      }
    }
  }
}