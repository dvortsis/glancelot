param([ValidateSet('first','static-init-red','static-init-green')][string]$Phase='first')
$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path $PSScriptRoot -Parent)
$toolRoot='C:\src\Github\Glancelot\.tools'
$env:PATH=(Join-Path $toolRoot 'python/Scripts')+';'+(Join-Path $toolRoot 'Qt/6.8.3/msvc2022_64/bin')+';'+$env:PATH
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$env:PATH=(Split-Path $vswhere -Parent)+';'+$env:PATH
$vsPath=& $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vsPath){throw 'MSVC missing'}
$devCmd=Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
$environmentLines=& cmd.exe /d /c "call `"$devCmd`" -arch=x64 -host_arch=x64 >nul && set"
if($LASTEXITCODE -ne 0){throw 'MSVC environment failed'}
foreach($entry in $environmentLines){if($entry -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')}}
$prefix='.cache/registry-component-'+$Phase
& cmake -S .cache/registry-component -B build-codex-safe-mode-01/registry-component -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/src/Github/Glancelot/.tools/Qt/6.8.3/msvc2022_64 *> ($prefix+'-configure.txt')
if($LASTEXITCODE -ne 0){Get-Content ($prefix+'-configure.txt') -Tail 20;exit $LASTEXITCODE}
& cmake --build build-codex-safe-mode-01/registry-component --parallel 2 *> ($prefix+'-build.txt')
if($LASTEXITCODE -ne 0){Get-Content ($prefix+'-build.txt') -Tail 20;exit $LASTEXITCODE}
& ctest --test-dir build-codex-safe-mode-01/registry-component --output-on-failure *> ($prefix+'-test.txt')
$testExit=$LASTEXITCODE
Get-Content ($prefix+'-test.txt') -Tail 26
$results='build-codex-safe-mode-01/registry-component/registry-results.txt'
if(Test-Path -LiteralPath $results){Copy-Item -LiteralPath $results -Destination ($prefix+'-cases.txt');Get-Content $results -Tail 20}
exit $testExit
