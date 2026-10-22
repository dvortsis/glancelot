$ErrorActionPreference='Stop'
Set-Location 'C:\src\Github\Glancelot\.worktrees\codex-integrate-export-csv-01'
$proof=Join-Path $PWD '.cache/csv-integration'
$pin=git rev-parse HEAD
try {
 & C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/integrate-export-csv-01 -Run {
  $pin | Set-Content "$proof/source-pin.txt"
  'Windows build.ps1 -Test' | Set-Content "$proof/phase.txt"
  & ./scripts/build.ps1 -BuildDir build-codex-integrate-export-csv-01 -Test *> "$proof/windows-gate.txt"
  Copy-Item build-codex-integrate-export-csv-01/Testing/Temporary/LastTest.log "$proof/windows-LastTest.log"
  'Docker build' | Set-Content "$proof/phase.txt"
  docker build --progress=plain -t glancelot-browser:main . *> "$proof/docker-gate.txt"
  if($LASTEXITCODE -ne 0){throw 'Docker gate failed'}
  docker image inspect glancelot-browser:main --format '{{.Id}}' | Set-Content "$proof/docker-image.txt"
 }
 @{exit=0;source=$pin;finished=(Get-Date -Format o)} | ConvertTo-Json | Set-Content "$proof/status.json"
} catch {
 @{exit=1;source=$pin;error="$_";finished=(Get-Date -Format o)} | ConvertTo-Json | Set-Content "$proof/status.json"
 throw
}
