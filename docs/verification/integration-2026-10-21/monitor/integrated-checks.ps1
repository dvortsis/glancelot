$ErrorActionPreference='Stop'
$env:PATH='C:/src/Github/Glancelot/.tools/python/Scripts;C:/src/Github/Glancelot/.tools/Qt/6.8.3/msvc2022_64/bin;'+$env:PATH
& C:/src/Github/Glancelot/scripts/gate-lock.ps1 -Branch codex/monitor-output-r2 -Run {
  ctest --test-dir build-codex-monitor-output-r2 -R '^(input_ui|viewport|monitor_output)$' --output-on-failure *> .cache/integration-monitor/integrated-ctest.txt
  $focused=$LASTEXITCODE
  "exit=$focused" | Set-Content .cache/integration-monitor/integrated-ctest-status.txt
  $env:QT_QPA_PLATFORM='windows'
  $env:GLANCELOT_MONITOR_NATIVE_COLOR_PROOF='1'
  $env:QSG_RHI_BACKEND='d3d11'
  $env:QSG_RHI_PREFER_SOFTWARE_RENDERER='1'
  Remove-Item Env:QT_QUICK_BACKEND -ErrorAction SilentlyContinue
  & ./build-codex-monitor-output-r2/monitor_output_tests.exe testNativeGpuColorPatch testNativeDisplayIdentityStateRestore -o .cache/integration-monitor/integrated-native.txt,txt | Out-Null
  $native=$LASTEXITCODE
  "exit=$native" | Set-Content .cache/integration-monitor/integrated-native-status.txt
  if($focused -ne 0){throw 'Integrated focused checks failed'}
}
git rev-parse HEAD > .cache/integration-monitor/integrated-commit.txt
git diff --exit-code b128be9 HEAD > .cache/integration-monitor/integrated-tree-equivalence.txt
"exit=$LASTEXITCODE" | Add-Content .cache/integration-monitor/integrated-tree-equivalence.txt
