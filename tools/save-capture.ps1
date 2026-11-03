# SPDX-License-Identifier: GPL-3.0-only
param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference='Stop'
$game=(Resolve-Path -LiteralPath $GameRoot).Path
if(-not(Test-Path -LiteralPath (Join-Path $game 'Sinmai.exe'))){throw 'Not a Sinmai game directory'}
$config=Join-Path $game 'SinmaiAsio.toml'
if(-not(Test-Path -LiteralPath $config) -or (Get-Content -LiteralPath $config -Raw) -notmatch '(?m)^\s*CaptureOutput\s*=\s*true\s*(?:#.*)?$'){throw 'Set CaptureOutput = true and restart the game with the new plugin first'}
$directory=Join-Path $game 'UserData'
New-Item -ItemType Directory -Path $directory -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $directory 'SinmaiAsio.capture'),'save')
Write-Output ('Capture requested. With the game running, the latest up to 30 seconds will be saved within a few seconds to '+$directory+'\SinmaiAsio-Capture-*.wav')