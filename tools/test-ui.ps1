param([string]$Compiler = 'C:\cygwin64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$outputDir = Join-Path $projectRoot 'build/ui-preview'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$previousPath = $env:PATH
$env:PATH = (Split-Path $Compiler -Parent) + ';' + $env:PATH
Push-Location $projectRoot
try {
    $compileArgs = @('-std=c11', '-O0', '-DLV_CONF_SKIP', '-DLV_MEM_CUSTOM=0', '-DLV_MEM_SIZE=98304', '-DLV_COLOR_16_SWAP=1',
        '-Imanaged_components/lvgl__lvgl', '-Icomponents/div_ui/include', '-Icomponents/div_spectrum/include',
        'tests/test_ui.c', 'components/div_ui/menu.c', 'components/div_ui/ui_assets.c', 'components/div_ui/touch.c',
        'components/div_ui/waterfall.c', 'components/div_spectrum/spectrum.c')
    $sources = rg --files managed_components/lvgl__lvgl/src -g '*.c'
    $compileArgs += $sources | ForEach-Object { $_.Replace('\','/') }
    $compileArgs += @('-lm', '-o', 'build/ui-preview/test_ui.exe')
    [IO.File]::WriteAllLines((Join-Path $outputDir 'compile.rsp'), $compileArgs)
    & $Compiler '@build/ui-preview/compile.rsp'
    if ($LASTEXITCODE -ne 0) { throw 'UI host compilation failed' }
    & (Join-Path $outputDir 'test_ui.exe')
    if ($LASTEXITCODE -ne 0) { throw 'UI regression check failed' }
} finally { Pop-Location; $env:PATH = $previousPath }
