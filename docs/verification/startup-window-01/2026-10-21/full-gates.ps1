$ErrorActionPreference='Stop'
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/startup-window-01 -Run {
  $win=1
  try { & ./scripts/build.ps1 -BuildDir build-codex-startup-window-01 -Test *> .cache/startup-window/windows-full.txt; $win=0 } catch { $_ | Out-String | Add-Content .cache/startup-window/windows-full.txt }
  "exit=$win" | Set-Content .cache/startup-window/windows-full-status.txt
  docker build --progress=plain -t glancelot-browser:main . *> .cache/startup-window/docker-full.txt
  $linux=$LASTEXITCODE
  "exit=$linux" | Set-Content .cache/startup-window/docker-full-status.txt
  if($win -ne 0 -or $linux -ne 0){throw "Full gates failed Windows=$win Docker=$linux"}
}
