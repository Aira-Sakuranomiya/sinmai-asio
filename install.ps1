# SPDX-License-Identifier: GPL-3.0-only
param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference='Stop'
if(Get-Process Sinmai -ErrorAction SilentlyContinue){throw 'Close Sinmai before installing'}
$root=(Resolve-Path -LiteralPath $GameRoot).Path
if(-not(Test-Path -LiteralPath (Join-Path $root 'Sinmai.exe'))){throw 'Not a Sinmai game directory'}
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$backup=Join-Path $root ('UserData\SinmaiAsio-backup-'+$stamp)
$files=@{ 'Mods\SinmaiAsio.dll'='build\SinmaiAsio.dll';'UserLibs\SinmaiAsio\SinmaiAsio.Native.dll'='build\SinmaiAsio.Native.dll' }
foreach($relative in $files.Keys){
    $source=Join-Path $PSScriptRoot $files[$relative]
    if(-not(Test-Path -LiteralPath $source)){throw ('Build first: '+$source)}
}
foreach($relative in $files.Keys){
    $destination=Join-Path $root $relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    if(Test-Path -LiteralPath $destination){New-Item -ItemType Directory -Path (Join-Path $backup (Split-Path $relative)) -Force | Out-Null;Copy-Item -LiteralPath $destination -Destination (Join-Path $backup $relative)}
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $files[$relative]) -Destination $destination -Force
}
$config=Join-Path $root 'SinmaiAsio.toml'
if(-not(Test-Path -LiteralPath $config)){Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'SinmaiAsio.example.toml') -Destination $config}
Write-Host 'Installed. Existing config preserved; new config defaults to disabled.'
