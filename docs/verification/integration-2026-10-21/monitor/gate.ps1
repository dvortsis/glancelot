$ErrorActionPreference='Stop'
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/monitor-output-r2 -Run {
  $win=1
  try { & ./scripts/build.ps1 -BuildDir build-codex-monitor-output-r2 -Test *> .cache/integration-monitor/windows.txt; $win=0 } catch { $_ | Out-String | Add-Content .cache/integration-monitor/windows.txt }
  "exit=$win" | Set-Content .cache/integration-monitor/windows-status.txt
  docker build --progress=plain -t glancelot-browser:main . *> .cache/integration-monitor/docker.txt
  $linux=$LASTEXITCODE
  "exit=$linux" | Set-Content .cache/integration-monitor/docker-status.txt
  if($win -ne 0 -or $linux -ne 0){throw "Integration gates failed Windows=$win Docker=$linux"}
}
