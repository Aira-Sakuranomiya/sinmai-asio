# SPDX-License-Identifier: GPL-3.0-only
param([string]$Version='0.2.0',[string]$TestedGameRoot)
$ErrorActionPreference='Stop'
if($Version -notmatch '^[A-Za-z0-9._-]+$'){throw 'Invalid version label'}
$root=$PSScriptRoot
foreach($file in @('build\Mods\SinmaiAsio.dll','build\UserLibs\SinmaiAsio\SinmaiAsio.Native.dll','LICENSE')){if(-not(Test-Path -LiteralPath (Join-Path $root $file))){throw ('Missing package input: '+$file)}}
$artifacts=Join-Path $root 'artifacts'
New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
$folder=Join-Path $artifacts ('sinmai-asio-'+$Version+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $folder | Out-Null
# Include source, binaries and licenses. No game, driver, logs or local settings are included.
foreach($directory in @('native','managed','tests','tools','docs')){Copy-Item -LiteralPath (Join-Path $root $directory) -Destination (Join-Path $folder $directory) -Recurse}
foreach($file in @('README.md','LICENSE','dependencies.json','build.ps1','SinmaiAsio.example.toml')){Copy-Item -LiteralPath (Join-Path $root $file) -Destination (Join-Path $folder $file)}
New-Item -ItemType Directory -Path (Join-Path $folder 'build'),(Join-Path $folder 'licenses') | Out-Null
$binaries=@{'Mods\SinmaiAsio.dll'=(Join-Path $root 'build\Mods\SinmaiAsio.dll');'UserLibs\SinmaiAsio\SinmaiAsio.Native.dll'=(Join-Path $root 'build\UserLibs\SinmaiAsio\SinmaiAsio.Native.dll')}
if($TestedGameRoot){
    $binaries['Mods\SinmaiAsio.dll']=Join-Path $TestedGameRoot 'Mods\SinmaiAsio.dll'
    $binaries['UserLibs\SinmaiAsio\SinmaiAsio.Native.dll']=Join-Path $TestedGameRoot 'UserLibs\SinmaiAsio\SinmaiAsio.Native.dll'
}
$manifest=@{nativeAbi=6;provenance=$(if($TestedGameRoot){'Installed DLL pair; playback and build validation scope is documented in docs/validation.md'}else{'Local build output'});files=@{}}
foreach($name in $binaries.Keys){
    $destination=Join-Path $folder ('build\'+$name)
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath $binaries[$name] -Destination $destination
    $manifest.files[$name]=(Get-FileHash -LiteralPath $binaries[$name] -Algorithm SHA256).Hash
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $folder 'binary-manifest.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $root 'third_party\asio-sdk\ASIOSDK\LICENSE.txt') -Destination (Join-Path $folder 'licenses\ASIO-SDK.txt')
Copy-Item -LiteralPath (Join-Path $root 'third_party\minhook-1.3.4\LICENSE.txt') -Destination (Join-Path $folder 'licenses\MinHook.txt')
$archive=$folder+'.zip'
Compress-Archive -LiteralPath $folder -DestinationPath $archive
Get-FileHash -LiteralPath $archive -Algorithm SHA256 | Format-List Path,Hash
