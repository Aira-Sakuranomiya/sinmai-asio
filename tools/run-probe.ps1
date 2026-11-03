# SPDX-License-Identifier: GPL-3.0-only
param([string]$GameRoot='K:\SDEZ170\Package',[ValidateSet('capture','clock','playback')][string]$Mode='capture',[double]$ServerFrequency=0)
$ErrorActionPreference='Stop'
if([double]::IsNaN($ServerFrequency) -or [double]::IsInfinity($ServerFrequency) -or ($ServerFrequency -ne 0 -and ($ServerFrequency -lt 16 -or $ServerFrequency -gt 1500))){throw 'ServerFrequency must be 0 (normal) or 16..1500 Hz'}
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
if($Mode -eq 'playback'){$info.EnvironmentVariables.Remove('SINMAI_ASIO_EXPERIMENT')}else{$info.EnvironmentVariables['SINMAI_ASIO_EXPERIMENT']=$Mode}
$info.EnvironmentVariables.Remove('SINMAI_ASIO_SERVER_FREQUENCY')
if($ServerFrequency -gt 0){$info.EnvironmentVariables['SINMAI_ASIO_SERVER_FREQUENCY']=$ServerFrequency.ToString('R',[Globalization.CultureInfo]::InvariantCulture)}
$process=[Diagnostics.Process]::Start($info)
Write-Output ('Probe launcher PID='+$process.Id+' mode='+$Mode)
Write-Output ('Stats: '+(Join-Path $root 'UserData\SinmaiAsio.stats.json'))
if($Mode -eq 'playback'){Write-Output ('Normal ASIO playback; diagnostic CRI server frequency='+$ServerFrequency+' Hz (0=normal). Output buffer unchanged.')}else{Write-Output 'Capture/clock probes are intentionally silent.'}
Write-Output 'Environment overrides apply only to this launcher and its children. Exit normally before switching tests.'
