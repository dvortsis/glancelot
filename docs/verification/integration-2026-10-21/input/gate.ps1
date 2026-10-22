$ErrorActionPreference='Stop'
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/input-02-r3 -Run {
  $win=1
  try { & ./scripts/build.ps1 -BuildDir build-codex-input-02-r3 -Test *> .cache/integration-input/windows.txt; $win=0 } catch { $_ | Out-String | Add-Content .cache/integration-input/windows.txt }
  "exit=$win" | Set-Content .cache/integration-input/windows-status.txt
  docker build --progress=plain -t glancelot-browser:main . *> .cache/integration-input/docker.txt
  $linux=$LASTEXITCODE
  "exit=$linux" | Set-Content .cache/integration-input/docker-status.txt
  if($win -ne 0 -or $linux -ne 0){throw "Integration gates failed Windows=$win Docker=$linux"}
}
