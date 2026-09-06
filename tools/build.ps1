param(
    [ValidateSet('v3','v4')][string]$Board = 'v3',
    [string]$IdfPath = 'C:\esp\v6.0.2\esp-idf',
    [string]$ExtraDefaults,
    [string]$BuildDirectory,
    [switch]$Flash,
    [string]$Port
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $IdfPath 'export.ps1')
Push-Location $projectRoot
try {
    if (-not $BuildDirectory) { $BuildDirectory = "build/c-$Board" }
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
    $defaults = "sdkconfig.defaults;sdkconfig.$Board"
    if ($ExtraDefaults) {
        if (-not (Test-Path -LiteralPath $ExtraDefaults)) { throw "Missing defaults: $ExtraDefaults" }
        $defaults += ";$ExtraDefaults"
    }
    $idfArgs = @('-B', $buildRoot, '-D', "SDKCONFIG=$buildRoot/sdkconfig", '-D', "SDKCONFIG_DEFAULTS=$defaults", 'build')
    & idf.py @idfArgs
    if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF build failed' }
    & python (Join-Path $PSScriptRoot 'verify-build.py') $buildRoot $Board
    if ($LASTEXITCODE -ne 0) { throw 'Build verification failed' }
    if ($Flash) {
        $flashArgs = @('-B', $buildRoot)
        if ($Port) { $flashArgs += @('-p', $Port) }
        & idf.py @flashArgs flash
        if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF flash failed' }
    }
} finally { Pop-Location }
