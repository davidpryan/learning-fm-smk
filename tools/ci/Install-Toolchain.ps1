# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Install the JieLi 2.5.3 Windows toolchain (PI32v2 clang 4.0.1) on a CI
# runner, or reuse a pre-extracted copy. Prints the path to use as
# FM1_PI32_TOOLCHAIN. The vendor Build.ps1 then verifies the SHA-256 of all
# 22 toolchain files against sdk/manifest.json, so a tampered or wrong
# toolchain stops the build before anything is linked.
#
# Sources, in order:
#   1. $env:JIELI_TOOLCHAIN_ZIP_URL  (repository secret): an HTTPS URL to a zip
#      of a verified C:\JL\pi32 directory. Fastest and most reliable.
#   2. The official installer https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe
#      run silently. Installer flags are not documented by JieLi; this script
#      tries the common NSIS and Inno Setup switches and checks the result.
#   3. An existing C:\JL\pi32 (self-hosted runner with the toolchain installed).
[CmdletBinding()]
param([string]$Destination = 'C:\JL\pi32')
$ErrorActionPreference = 'Stop'
$clang = Join-Path $Destination 'bin\clang.exe'
function Done { Write-Host "toolchain: $Destination"; Write-Output "FM1_PI32_TOOLCHAIN=$Destination" >> $env:GITHUB_ENV; exit 0 }
if (Test-Path -LiteralPath $clang) { Done }

$cache = Join-Path $env:RUNNER_TEMP 'jieli'
New-Item -ItemType Directory -Force -Path $cache | Out-Null

if ($env:JIELI_TOOLCHAIN_ZIP_URL) {
    $zip = Join-Path $cache 'pi32.zip'
    Write-Host 'toolchain: downloading pre-extracted zip from the repository secret'
    Invoke-WebRequest -Uri $env:JIELI_TOOLCHAIN_ZIP_URL -OutFile $zip
    $extract = Join-Path $cache 'pi32-zip'
    Expand-Archive -LiteralPath $zip -DestinationPath $extract -Force
    $found = Get-ChildItem -LiteralPath $extract -Recurse -Filter clang.exe | Select-Object -First 1
    if (!$found) { throw 'JIELI_TOOLCHAIN_ZIP_URL did not contain bin/clang.exe' }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -LiteralPath (Split-Path -Parent (Split-Path -Parent $found.FullName)) -Destination $Destination -Recurse -Force
    if (Test-Path -LiteralPath $clang) { Done }
    throw 'toolchain zip extracted but clang.exe not at the expected place'
}

$installer = Join-Path $cache '2.5.3.exe'
$url = 'https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe'
Write-Host "toolchain: downloading $url"
Invoke-WebRequest -Uri $url -OutFile $installer
$sha = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "toolchain: installer SHA-256 $sha"
if ($env:JIELI_INSTALLER_SHA256 -and $env:JIELI_INSTALLER_SHA256.ToLowerInvariant() -ne $sha) {
    throw "installer SHA-256 does not match JIELI_INSTALLER_SHA256"
}
foreach ($args in @(@('/S', "/D=C:\JL"), @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/DIR=C:\JL'), @('/quiet'))) {
    Write-Host "toolchain: trying installer switches $($args -join ' ')"
    try { $p = Start-Process -FilePath $installer -ArgumentList $args -Wait -PassThru -NoNewWindow; Write-Host "exit $($p.ExitCode)" } catch { Write-Host "failed: $_" }
    if (Test-Path -LiteralPath $clang) { Done }
    $alt = Get-ChildItem -Path 'C:\JL' -Recurse -Filter clang.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($alt) { $dir = Split-Path -Parent (Split-Path -Parent $alt.FullName); Write-Host "toolchain: found at $dir"; Write-Output "FM1_PI32_TOOLCHAIN=$dir" >> $env:GITHUB_ENV; exit 0 }
}
throw @"
The JieLi toolchain could not be installed silently on this runner.
Options:
  * Install JieLi 2.5.3 once on a Windows PC (https://doc.zh-jieli.com/Tools/zh-cn/dev_tools/dev_env/jl_toolchain.html),
    zip the C:\JL\pi32 folder, host the zip privately (a private GitHub release works) and set the
    repository secret JIELI_TOOLCHAIN_ZIP_URL to its URL. Build.ps1 still verifies every file's SHA-256.
  * Or run this workflow on a self-hosted Windows runner that has C:\JL\pi32 installed.
"@
