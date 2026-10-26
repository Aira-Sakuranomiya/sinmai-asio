# SPDX-License-Identifier: GPL-3.0-only
param([string]$GameRoot='K:\SDEZ170\Package',[ValidateSet('capture','clock')][string]$Mode='capture')
$ErrorActionPreference='Stop'
if(Get-Process Sinmai -ErrorAction SilentlyContinue){throw 'Close Sinmai before launching a probe'}
$root=(Resolve-Path -LiteralPath $GameRoot).Path
if(-not(Test-Path -LiteralPath (Join-Path $root 'launch.bat'))){throw 'Expected the existing launch.bat; no alternate launcher is guessed'}
if(-not(Test-Path -LiteralPath (Join-Path $root 'Mods\SinmaiAsio.dll'))){throw 'Install the plugin first'}
$info=New-Object Diagnostics.ProcessStartInfo
$info.FileName=$env:ComSpec
$info.Arguments='/d /c launch.bat'
$info.WorkingDirectory=$root
$info.UseShellExecute=$false
$info.CreateNoWindow=$true
$info.EnvironmentVariables['SINMAI_ASIO_EXPERIMENT']=$Mode
$process=[Diagnostics.Process]::Start($info)
Write-Output ('Probe launcher PID='+$process.Id+' mode='+$Mode)
Write-Output ('Stats: '+(Join-Path $root 'UserData\SinmaiAsio.stats.json'))
Write-Output 'Both probe modes are intentionally silent. Exit the game normally before switching modes.'
