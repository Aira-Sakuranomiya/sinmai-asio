# SPDX-License-Identifier: GPL-3.0-only
param([string]$GameRoot='K:\SDEZ170\Package',[switch]$FetchDependencies,[switch]$ManagedOnly)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$out=Join-Path $root 'build'
New-Item -ItemType Directory -Path $out -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $out 'fixtures') -Force | Out-Null
$sdk=Join-Path $root 'third_party\asio-sdk\ASIOSDK'
$url='https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip'
$hash='D5EBF0C20DD2C5F43771FD0C1418F4B361BF52434EE670097CFA6B3A335E2ECA'
$minhook=Join-Path $root 'third_party\minhook-1.3.4'
$mhZip=Join-Path $root 'third_party\minhook-v1.3.4.zip'
$mhHash='172708123DAA0C98D20D3A980B16A50BE14AF243DC95DEE6F79C24193AD010E4'
$zip=Join-Path $root 'third_party\ASIO-SDK_2.3.4_2025-10-15.zip'
if($FetchDependencies){
    New-Item -ItemType Directory -Path (Split-Path $zip) -Force | Out-Null
    if(-not(Test-Path $zip)){Invoke-WebRequest -Uri $url -OutFile $zip}
    if((Get-FileHash $zip -Algorithm SHA256).Hash -ne $hash){throw 'ASIO SDK archive checksum mismatch'}
    Expand-Archive -LiteralPath $zip -DestinationPath (Split-Path $sdk) -Force
    if(-not(Test-Path $mhZip)){Invoke-WebRequest -Uri 'https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.zip' -OutFile $mhZip}
}
if(-not $ManagedOnly){
if(-not(Test-Path (Join-Path $sdk 'common\iasiodrv.h'))){throw 'Missing ASIO SDK. Run build.ps1 -FetchDependencies'}
if(-not(Test-Path $zip) -or (Get-FileHash $zip -Algorithm SHA256).Hash -ne $hash){throw 'Missing or modified pinned SDK archive'}
if(-not(Test-Path $mhZip) -or (Get-FileHash $mhZip -Algorithm SHA256).Hash -ne $mhHash){throw 'Missing or modified pinned MinHook archive; use -FetchDependencies'}
Expand-Archive -LiteralPath $zip -DestinationPath (Split-Path $sdk) -Force
Expand-Archive -LiteralPath $mhZip -DestinationPath (Split-Path $minhook) -Force
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'Visual Studio C++ x64 build tools are required'}
$vars=Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$nativeBuild=Join-Path $out 'native-build.cmd'
# Use one command shell for compiler setup/build. No path enumeration or destructive operations.
$batch=@"
@echo off
call "$vars" >nul
if errorlevel 1 exit /b 1
cd /d "$out"
cl /nologo /W4 /O2 /MT /I"$minhook\include" /c "$minhook\src\buffer.c" "$minhook\src\hook.c" "$minhook\src\trampoline.c" "$minhook\src\hde\hde64.c"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /I"$sdk\common" /I"$minhook\include" /I"$root\native" /LD "$root\native\engine.cpp" buffer.obj hook.obj trampoline.obj hde64.obj /Fe:"$out\SinmaiAsio.Native.dll" /link /IMPLIB:"$out\SinmaiAsio.Native.lib"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /I"$root\native" "$root\native\probe.cpp" "$out\SinmaiAsio.Native.lib" /Fe:"$out\asio-probe.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /I"$root\native" "$root\tests\audio_tests.cpp" /Fe:"$out\audio-tests.exe"
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W4 /O2 /MT /LD "$root\tests\profile_stub.cpp" /Fe:"$out\fixtures\cri_ware_unity.dll"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /I"$root\native" "$root\tests\profile_tests.cpp" "$out\SinmaiAsio.Native.lib" /Fe:"$out\profile-tests.exe"
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($nativeBuild,$batch,[Text.Encoding]::Default)
& $env:ComSpec /d /c $nativeBuild
if($LASTEXITCODE -ne 0){throw 'Native build failed'}
& (Join-Path $out 'audio-tests.exe')
if($LASTEXITCODE -ne 0){throw 'Native audio tests failed'}
& (Join-Path $out 'profile-tests.exe') --known (Join-Path $GameRoot 'Sinmai_Data\Plugins\cri_ware_unity.dll')
if($LASTEXITCODE -ne 0){throw 'Known CRI profile / hook restoration test failed'}
& (Join-Path $out 'profile-tests.exe') --unknown (Join-Path $out 'fixtures\cri_ware_unity.dll')
if($LASTEXITCODE -ne 0){throw 'Unknown CRI profile rejection test failed'}
}
$refs=Join-Path $GameRoot 'MelonLoader\net35'
$game=Join-Path $GameRoot 'Sinmai_Data\Managed'
$csc=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$framework=Join-Path ${env:ProgramFiles(x86)} 'Reference Assemblies\Microsoft\Framework\.NETFramework\v4.7.2'
if(-not(Test-Path (Join-Path $framework 'mscorlib.dll'))){throw '.NET Framework 4.7.2 targeting pack is required'}
$args=@('/nologo','/noconfig','/target:library','/platform:x64','/optimize+','/nostdlib+',('/out:'+(Join-Path $out 'SinmaiAsio.dll')))
foreach($dll in @('mscorlib.dll','System.dll','System.Core.dll')){$args+=('/reference:'+(Join-Path $framework $dll))}
foreach($dll in @('MelonLoader.dll','0Harmony.dll','Tomlet.dll')){$args+=('/reference:'+(Join-Path $refs $dll))}
foreach($dll in @('UnityEngine.dll','UnityEngine.CoreModule.dll','UnityEngine.IMGUIModule.dll')){$args+=('/reference:'+(Join-Path $game $dll))}
$args+=@(Get-ChildItem (Join-Path $root 'managed') -Filter '*.cs' -File | ForEach-Object {$_.FullName})
& $csc @args
if($LASTEXITCODE -ne 0){throw 'Managed build failed'}
if($ManagedOnly){Write-Host "Managed build passed: $out"}else{Write-Host "Build and native tests passed: $out"}
