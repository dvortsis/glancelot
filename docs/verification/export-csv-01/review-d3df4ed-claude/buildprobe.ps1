param([string]$Repo, [string]$Build, [string]$Src, [string]$Out)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
$envv = & cmd.exe /d /c "call `"$devCmd`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($e in $envv) { if ($e -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') } }
& cl /nologo /std:c++20 /EHsc /MD /O2 /utf-8 "/I$Repo\include" $Src "/Fe:$Out" "/Fo:$Out.obj" "$Build\glancelot_core.lib" 2>&1 | Select-Object -Last 8
