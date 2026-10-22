$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path $PSScriptRoot -Parent)
$deadline=(Get-Date).AddMinutes(30)
do{
    $busy=@(Get-CimInstance Win32_Process | Where-Object {
        ($_.Name -eq 'ctest.exe' -or $_.Name -like '*_tests.exe') -and
        -not ($_.ProcessId -eq 20064 -and $_.ExecutablePath -eq 'C:\src\Github\Glancelot\.worktrees\filter\build-filter\file_operation_service_tests.exe')
    })
    if(!$busy.Count){break}
    if((Get-Date) -gt $deadline){throw 'Native slot unavailable for 30 minutes'}
    Start-Sleep -Seconds 2
}while($true)
if((git rev-parse HEAD) -ne '4a8652883d6d5a68b41340ce6639c201ee15ce32'){throw 'Candidate changed'}
git diff --quiet HEAD -- src include tests app ui cmake CMakeLists.txt
if($LASTEXITCODE -ne 0){throw 'Candidate source has uncommitted changes'}
$marker='C:\src\Github\Glancelot\.cache\native-suite-running'
$stream=[IO.File]::Open($marker,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try{
    $bytes=[Text.Encoding]::UTF8.GetBytes((@{owner='codex/fs-watch-01-fast-path';pid=$PID;source=(git rev-parse HEAD);state='diagnostics'}|ConvertTo-Json))
    $stream.Write($bytes,0,$bytes.Length)
}finally{$stream.Dispose()}
try{
    $env:PATH='C:\src\Github\Glancelot\.tools\Qt\6.8.3\msvc2022_64\bin;'+$env:PATH
    $env:QT_QPA_PLATFORM='offscreen'
    $env:GLANCELOT_CONFIG_DIR=Join-Path $PWD '.cache/fast-path-config'
    $build=Join-Path $PWD 'build-codex-fs-watch-01'
    $outcomes=@();$failed=$false
    foreach($case in @(
        @{name='metadata-regression';exe='fs_watch_tests.exe';seconds=30;selection=@('metadataOnlyRefreshPreservesPersistentRowsAndSelection')},
        @{name='cache';exe='cache_tests.exe';seconds=30;selection=@()},
        @{name='grid';exe='grid_ui_tests.exe';seconds=300;selection=@()},
        @{name='budget';exe='fs_watch_budget_tests.exe';seconds=180;selection=@()}
    )){
        $executable=Join-Path $build $case.exe
        $output=Join-Path $PWD ('.cache/windows-fast-path-'+$case.name+'.txt')
        $arguments=@($case.selection)+@('-o',"$output,txt")
        $probe=Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $build -WindowStyle Hidden -PassThru
        $probeStart=$probe.StartTime;$deadline=(Get-Date).AddSeconds($case.seconds)
        while(!$probe.HasExited -and (Get-Date) -lt $deadline){
            Wait-Process -Id $probe.Id -Timeout 2 -ErrorAction SilentlyContinue
            $probe.Refresh()
        }
        $timedOut=!$probe.HasExited
        if($timedOut){
            $current=Get-Process -Id $probe.Id -ErrorAction Stop
            if($current.StartTime -ne $probeStart -or $current.Path -ne $executable){throw 'Diagnostic process identity changed'}
            Stop-Process -Id $probe.Id -ErrorAction Stop
            $probe.WaitForExit();$code=124
        }else{$probe.WaitForExit();$code=$probe.ExitCode}
        $outcomes+=@{name=$case.name;exit=$code;timedOut=$timedOut;limitSeconds=$case.seconds}
        if($code -ne 0){$failed=$true}
        $outcomes|ConvertTo-Json|Set-Content -LiteralPath .cache/windows-fast-path-outcomes.json
        Write-Output ($case.name+' exit '+$code+' timeout '+$timedOut)
        if(Test-Path -LiteralPath $output){Get-Content -LiteralPath $output -Tail 15}
    }
    if($failed){exit 1}
}finally{
    $current=Get-Content -LiteralPath $marker -Raw|ConvertFrom-Json
    if($current.pid -eq $PID){Remove-Item -LiteralPath $marker}
}
