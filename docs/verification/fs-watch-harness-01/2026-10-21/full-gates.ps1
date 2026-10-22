$ErrorActionPreference='Stop'
& C:\src\Github\Glancelot\scripts\gate-lock.ps1 -Branch codex/fs-watch-harness-01 -Run {
 $win=1
 try { & ./scripts/build.ps1 -BuildDir build-codex-fs-watch-harness-01 -Test *> .cache/watcher-harness/windows-full.txt; $win=0 } catch { $_ | Out-String | Add-Content .cache/watcher-harness/windows-full.txt }
 "exit=$win" | Set-Content .cache/watcher-harness/windows-full-status.txt
 docker build --progress=plain -t glancelot-browser:main . *> .cache/watcher-harness/docker-full.txt
 $linux=$LASTEXITCODE; "exit=$linux" | Set-Content .cache/watcher-harness/docker-full-status.txt
 if($win -ne 0 -or $linux -ne 0){throw "Watcher harness gate failure Windows=$win Docker=$linux"}
}
