param(
    [ValidateSet('v3','v4')][string]$Board = 'v3',
    [string]$IdfPath = 'C:\esp\v5.5.5\esp-idf',
    [string]$ExtraDefaults,
    [string]$BuildDirectory,
    [switch]$Flash,
    [string]$Port
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$installedProfile = 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1'
if ($IdfPath -eq 'C:\esp\v5.5.5\esp-idf' -and (Test-Path -LiteralPath $installedProfile)) {
    . $installedProfile
} else {
    . (Join-Path $IdfPath 'export.ps1')
}
# Call the selected installation directly, avoiding aliases from another SDK.
$idfPython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'
$idfScript = Join-Path $IdfPath 'tools/idf.py'
$versionText = & $idfPython $idfScript --version
if ($LASTEXITCODE -ne 0 -or $versionText -notmatch '^ESP-IDF v5\.5(?:\.|$)') {
    throw "This project requires ESP-IDF 5.5.x; selected SDK reports: $versionText"
}
Push-Location $projectRoot
try {
    if (-not $BuildDirectory) { $BuildDirectory = "build/idf55-$Board" }
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
    $defaults = "sdkconfig.defaults;sdkconfig.$Board"
    if ($ExtraDefaults) {
        if (-not (Test-Path -LiteralPath $ExtraDefaults)) { throw "Missing defaults: $ExtraDefaults" }
        $defaults += ";$ExtraDefaults"
    }
    $idfArgs = @('-B', $buildRoot, '-D', "SDKCONFIG=$buildRoot/sdkconfig", '-D', "SDKCONFIG_DEFAULTS=$defaults", 'build')
    & $idfPython $idfScript @idfArgs
    if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF build failed' }
    & $idfPython (Join-Path $PSScriptRoot 'verify-build.py') $buildRoot $Board
    if ($LASTEXITCODE -ne 0) { throw 'Build verification failed' }
    if ($Flash) {
        $flashArgs = @('-B', $buildRoot)
        if ($Port) { $flashArgs += @('-p', $Port) }
        & $idfPython $idfScript @flashArgs flash
        if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF flash failed' }
    }
} finally { Pop-Location }
