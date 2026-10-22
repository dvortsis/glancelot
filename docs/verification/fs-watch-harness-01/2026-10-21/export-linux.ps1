$ErrorActionPreference='Stop'
& C:/src/Github/Glancelot/scripts/gate-lock.ps1 -Branch codex/fs-watch-harness-01 -Run {
 docker build --progress=plain --target build -t glancelot-watcher-harness-evidence:7e66052 . *> .cache/watcher-harness/linux-export-build.txt
 $v=$LASTEXITCODE; "exit=$v" | Set-Content .cache/watcher-harness/linux-export-build-status.txt
 if($v -ne 0){throw 'Linux evidence stage export failed'}
}
$cid=docker create glancelot-watcher-harness-evidence:7e66052 /bin/true
if($LASTEXITCODE -ne 0){throw 'Evidence container create failed'}
try {
 New-Item -ItemType Directory -Force .cache/watcher-harness/linux-raw | Out-Null
 foreach($name in @('fs-watch-budget-results.txt','Testing')) {
  docker cp "${cid}:/build/$name" .cache/watcher-harness/linux-raw/ *> ".cache/watcher-harness/linux-copy-$name.txt"
  if($LASTEXITCODE -ne 0){throw "Evidence copy failed $name"}
 }
} finally { docker rm $cid *> .cache/watcher-harness/linux-export-container-remove.txt }
