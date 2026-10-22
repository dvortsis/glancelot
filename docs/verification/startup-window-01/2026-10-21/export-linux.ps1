$ErrorActionPreference='Stop'
& C:/src/Github/Glancelot/scripts/gate-lock.ps1 -Branch codex/startup-window-01 -Run {
  docker build --progress=plain --target build -t glancelot-startup-evidence:db62544 . *> .cache/startup-window/linux-build-export.txt
  $v=$LASTEXITCODE; "exit=$v" | Set-Content .cache/startup-window/linux-build-export-status.txt
  if($v -ne 0){throw 'Build stage export failed'}
}
$cid=docker create glancelot-startup-evidence:db62544 /bin/true
if($LASTEXITCODE -ne 0){throw 'Evidence container create failed'}
try {
 New-Item -ItemType Directory -Force .cache/startup-window/linux-raw | Out-Null
 foreach($name in @('Testing','recovery-process','recovery-results.txt','recovery-startup-results.txt','recovery-controller-results.txt')) {
  docker cp "${cid}:/build/$name" .cache/startup-window/linux-raw/ *> ".cache/startup-window/linux-copy-$name.txt"
  if($LASTEXITCODE -ne 0){throw "Evidence copy failed $name"}
 }
} finally { docker rm $cid *> .cache/startup-window/linux-evidence-container-remove.txt }
