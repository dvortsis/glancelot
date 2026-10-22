param([string]$Attempt='10')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
Set-Location $repo
$evidence=Join-Path $repo ".cache/monitor-native-ui-attempt-$Attempt"
$env:PATH='C:/src/Github/Glancelot/.tools/Qt/6.8.3/msvc2022_64/bin;'+$env:PATH
$env:QT_QPA_PLATFORM='windows'
Remove-Item Env:QT_QUICK_BACKEND -ErrorAction SilentlyContinue
$env:QSG_RHI_BACKEND='d3d11'
$env:GLANCELOT_MONITOR_UI_EVIDENCE=$evidence
$binary=Join-Path $repo 'build-codex-monitor-custom-01/monitor_customization_ui_tests.exe'
$before=(Get-FileHash $binary).Hash
$pin=git rev-parse HEAD
$dirty=git status --porcelain -- app include src ui tests CMakeLists.txt cmake
if($dirty){throw 'Product sources must be clean before native evidence'}
@{source=$pin;binarySha256Before=$before;started=(Get-Date -Format o);command="monitor_customization_ui_tests.exe -o .cache/monitor-native-ui-attempt-$Attempt.txt,txt"} | ConvertTo-Json | Set-Content "$evidence-provenance-before.json"
& $binary -o ".cache/monitor-native-ui-attempt-$Attempt.txt,txt" | Out-Null
$nativeExit=$LASTEXITCODE
$after=(Get-FileHash $binary).Hash
@{sourceBefore=$pin;sourceAfter=(git rev-parse HEAD);binarySha256Before=$before;binarySha256After=$after;binaryUnchanged=($before -eq $after);exit=$nativeExit;finished=(Get-Date -Format o)} | ConvertTo-Json | Set-Content "$evidence-provenance-after.json"
"exit=$nativeExit" | Set-Content "$evidence-status.txt"
if($nativeExit -ne 0){throw 'Native UI evidence failed'}
