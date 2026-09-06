param([string]$Compiler = 'C:\cygwin64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$outputDir = Join-Path $projectRoot 'build/host-tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
Push-Location $projectRoot
$previousPath = $env:PATH
$env:PATH = (Split-Path $Compiler -Parent) + ';' + $env:PATH
try {
    & $Compiler -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wpedantic -g `
        -Itests/stubs -Icomponents/div_protocol/include -Icomponents/div_services `
        -Icomponents/div_storage/include -Icomponents/minmea `
        tests/test_protocol.c components/div_protocol/protocol.c components/div_services/pulse_profile.c `
        components/minmea/minmea.c -lm -o build/host-tests/test_protocol.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed' }
    & (Join-Path $outputDir 'test_protocol.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
} finally { $env:PATH = $previousPath; Pop-Location }
