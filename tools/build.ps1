param([switch]$Debug, [switch]$SkipPackage, [string]$BuildDirectory = 'build', [string]$Tests = '')
$ErrorActionPreference = 'Stop'
if ($Tests -and -not $SkipPackage) { throw 'Filtered test runs require -SkipPackage. Releases must pass every suite.' }
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDirectory
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
if (-not (Test-Path -LiteralPath $cmake)) { throw 'Install Visual Studio 2022 with Desktop development with C++ and CMake.' }
$config = if ($Debug) { 'Debug' } else { 'Release' }
function Invoke-BuildTool([string]$Executable, [string[]]$Arguments) {
    # Some launchers supply both PATH and Path. MSBuild rejects that environment.
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.EnvironmentVariables.Clear()
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        $info.EnvironmentVariables[$entry.Key.ToUpperInvariant()] = $entry.Value
    }
    $info.Arguments = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $process = [System.Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    Write-Output $stdout.Result
    if ($stderr.Result) { Write-Output $stderr.Result }
    if ($process.ExitCode) { throw "$Executable failed with exit code $($process.ExitCode)." }
}
Invoke-BuildTool $cmake @('-S', $root, '-B', "$build", '-G', 'Visual Studio 17 2022', '-A', 'x64')
Invoke-BuildTool $cmake @('--build', "$build", '--config', $config, '--parallel', '1', '--', '/nodeReuse:false')
# A vendored DLL can change without relinking either EXE. Check its content on
# incremental builds; avoid replacing an identical DLL while the app is running.
function Sync-VendorDll([string]$RelativeSource, [string]$FileName) {
    $source = Join-Path $root $RelativeSource
    $target = Join-Path $build "$config\$FileName"
    if (-not (Test-Path -LiteralPath $target) -or
        (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
        Copy-Item -LiteralPath $source -Destination $target -Force
    }
}
Sync-VendorDll 'third_party\bass\x64\bass.dll' 'bass.dll'
Sync-VendorDll 'third_party\basshls\x64\basshls.dll' 'basshls.dll'
$testArguments = @('--test-dir', "$build", '-C', $config, '--output-on-failure')
if ($Tests) { $testArguments += @('-R', $Tests) }
Invoke-BuildTool (Join-Path (Split-Path $cmake) 'ctest.exe') $testArguments
if ($SkipPackage) { return }
$versionInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo("$build\$config\QuickStation.exe")
if ($versionInfo.ProductName -ne 'QuickStation' -or $versionInfo.ProductVersion -ne '1.6.14') {
    throw 'QuickStation executable product metadata is missing or incorrect.'
}
$destination = "$root\dist\QuickStation-$($versionInfo.ProductVersion)"
New-Item -ItemType Directory -Force -Path $destination | Out-Null
Copy-Item -LiteralPath "$build\$config\QuickStation.exe","$build\$config\QuickStationAudio.exe","$build\$config\bass.dll","$build\$config\basshls.dll" -Destination $destination
Copy-Item -LiteralPath "$root\README.md","$root\VALIDATION.md","$root\THIRD_PARTY_NOTICES.md","$root\third_party\bass\bass.txt","$root\third_party\basshls\basshls.txt","$root\third_party\json-LICENSE.txt" -Destination $destination
Compress-Archive -Path $destination -DestinationPath "$root\dist\QuickStation-windows-x64.zip" -Force
Invoke-BuildTool (Get-Process -Id $PID).Path @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
    (Join-Path $PSScriptRoot 'release-smoke.ps1'), '-ZipPath', "$root\dist\QuickStation-windows-x64.zip",
    '-ReleaseDirectory', $destination)
Write-Output "Ready: $destination\QuickStation.exe"
