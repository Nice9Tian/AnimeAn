{
  "agent_instructions": {
    "purpose": "Verify if the deploy target builds and exits successfully.",
    "build_policy": "Builds are slow. Do not run a build unless it is necessary, such as after substantial code changes or when explicitly requested.",
    "execution": {
      "shell": "powershell",
      "command": "PowerShell -ExecutionPolicy Bypass -File \".\\scripts\\agent_build.ps1\"",
      "requires_outside_sandbox": true,
      "sandbox_note": "Build verification must run outside the sandbox because CMake/Qt autogen launches compiler and deployment subprocesses."
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
