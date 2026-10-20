$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path $PSScriptRoot -Parent)
$expected='1bd44616d801f09b61f1191724d918f7decd8992'
if((git rev-parse HEAD) -ne $expected){throw 'Integrated source changed'}
git diff --quiet HEAD -- src include app ui tests cmake CMakeLists.txt scripts Dockerfile
if($LASTEXITCODE -ne 0){throw 'Uncommitted implementation cannot be qualified'}
$hold=Get-Content -LiteralPath 'C:\src\Github\Glancelot\.cache\native-fixture-security-hold.json' -Raw|ConvertFrom-Json
if($hold.state -ne 'replacement_qualified'){throw 'Replacement fixture is not qualified'}
git merge-base --is-ancestor $hold.qualified_commit HEAD
if($LASTEXITCODE -ne 0){throw 'Integrated source lacks qualified fixture'}
git diff --quiet $hold.qualified_commit HEAD -- cmake/PlatformTestFixtures.cmake tests/platform_fixture_capture.hpp tests/platform_argv_capture.cpp tests/platform_integration_test_adapter.cpp tests/platform_integration_tests.cpp
if($LASTEXITCODE -ne 0){throw 'Fixture differs from qualified source'}
foreach($retired in @('tests/platform_launcher_stub.cpp','tests/native_platform_fixture.hpp')){
    if(Test-Path -LiteralPath $retired){throw 'Retired fixture source is present'}
}
$busy=@(Get-CimInstance Win32_Process | Where-Object {
    ($_.Name -eq 'ctest.exe' -or $_.Name -like '*_tests.exe') -and
    -not ($_.ProcessId -eq 20064 -and $_.ExecutablePath -eq 'C:\src\Github\Glancelot\.worktrees\filter\build-filter\file_operation_service_tests.exe')
})
if($busy.Count){throw 'Another native suite is active'}
$marker='C:\src\Github\Glancelot\.cache\native-suite-running'
$reservation=@{owner='codex/verify-fs-watch-integrated';pid=$PID;source=$expected;state='windows';started=[DateTime]::UtcNow.ToString('o')}
$stream=[IO.File]::Open($marker,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try{$bytes=[Text.Encoding]::UTF8.GetBytes(($reservation|ConvertTo-Json));$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
try{
    $env:GLANCELOT_CONFIG_DIR=Join-Path $PWD '.cache/integrated-config'
    & .\scripts\build.ps1 -BuildDir build-codex-fs-watch-integrated -Test *> .cache/windows-integrated-full.txt
    if($LASTEXITCODE -ne 0){throw "Integrated Windows gate exit $LASTEXITCODE"}
    if(!(Select-String -LiteralPath .cache/windows-integrated-full.txt -Pattern '100% tests passed(?:, 0 tests failed)? out of 32' -Quiet)){throw 'Integrated Windows full32 pass missing'}
    Get-Content .cache/windows-integrated-full.txt -Tail 12
    $reservation.state='linux';Set-Content -LiteralPath $marker -Value ($reservation|ConvertTo-Json)
    docker build -t glancelot-browser:main . *> .cache/linux-integrated-full.txt
    if($LASTEXITCODE -ne 0){throw "Integrated Docker gate exit $LASTEXITCODE"}
    if(!(Select-String -LiteralPath .cache/linux-integrated-full.txt -Pattern '100% tests passed(?:, 0 tests failed)? out of 32' -Quiet)){throw 'Integrated Linux full32 pass missing'}
    Get-Content .cache/linux-integrated-full.txt -Tail 15
}finally{
    $current=Get-Content -LiteralPath $marker -Raw|ConvertFrom-Json
    if($current.pid -eq $PID){Remove-Item -LiteralPath $marker}
}
