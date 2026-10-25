param(
    [Parameter(Mandatory = $true)][string]$ZipPath,
    [Parameter(Mandatory = $true)][string]$ReleaseDirectory
)
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$zip = (Resolve-Path -LiteralPath $ZipPath).Path
$release = (Resolve-Path -LiteralPath $ReleaseDirectory).Path
$scratchParent = Join-Path $root 'test-data'
$scratch = Join-Path $scratchParent ('release-smoke-' + [guid]::NewGuid().ToString('N'))
$app = $null

try {
    New-Item -ItemType Directory -Path $scratch | Out-Null
    $unpacked = Join-Path $scratch 'unpacked'
    Expand-Archive -LiteralPath $zip -DestinationPath $unpacked
    $portable = Join-Path $unpacked (Split-Path -Leaf $release)
    if (-not (Test-Path -LiteralPath $portable -PathType Container)) {
        throw 'The ZIP does not contain the expected release folder.'
    }

    $sourceFiles = @(Get-ChildItem -LiteralPath $release -File | Sort-Object Name)
    $packageFiles = @(Get-ChildItem -LiteralPath $portable -File | Sort-Object Name)
    if ($sourceFiles.Count -ne $packageFiles.Count) {
        throw 'The ZIP file count differs from the release folder.'
    }
    for ($i = 0; $i -lt $sourceFiles.Count; ++$i) {
        if ($sourceFiles[$i].Name -ne $packageFiles[$i].Name -or
            (Get-FileHash -LiteralPath $sourceFiles[$i].FullName -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $packageFiles[$i].FullName -Algorithm SHA256).Hash) {
            throw "The ZIP entry differs from the release folder: $($sourceFiles[$i].Name)."
        }
    }

    $profile = Join-Path $scratch 'profile'
    New-Item -ItemType Directory -Path $profile | Out-Null
    $executable = Join-Path $portable 'QuickStation.exe'
    $app = Start-Process -FilePath $executable -ArgumentList ('--data-dir "' + $profile + '"') `
        -WorkingDirectory $portable -WindowStyle Minimized -PassThru
    $deadline = [datetime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 200
        $app.Refresh()
    } while (($app.MainWindowHandle -eq 0 -or
              -not (Test-Path -LiteralPath (Join-Path $profile 'stations.json'))) -and
             -not $app.HasExited -and [datetime]::UtcNow -lt $deadline)

    if ($app.HasExited -or $app.MainWindowHandle -eq 0 -or
        -not (Test-Path -LiteralPath (Join-Path $profile 'stations.json'))) {
        throw 'The ZIP-extracted app did not start and create its isolated station file.'
    }
    if (-not $app.CloseMainWindow() -or -not $app.WaitForExit(15000)) {
        throw 'The ZIP-extracted app did not close normally.'
    }
    if ($app.ExitCode -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $profile 'window.json'))) {
        throw 'The ZIP-extracted app failed to save its window state on exit.'
    }
    Write-Output "PASS: $($sourceFiles.Count) ZIP entries match; extracted QuickStation started, saved data, and closed cleanly."
}
finally {
    if ($app) {
        $app.Refresh()
        if (-not $app.HasExited) {
            Stop-Process -Id $app.Id -Force -ErrorAction SilentlyContinue
            $null = $app.WaitForExit(5000)
        }
        $app.Dispose()
    }
    $allowed = [IO.Path]::GetFullPath($scratchParent).TrimEnd('\') + '\'
    $target = [IO.Path]::GetFullPath($scratch).TrimEnd('\') + '\'
    if (-not $target.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to remove a smoke-test folder outside test-data.'
    }
    if (Test-Path -LiteralPath $scratch) {
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}
