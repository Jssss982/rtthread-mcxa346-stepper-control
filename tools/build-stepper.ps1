param(
    [string]$Python = 'python',
    [string]$ArmBin = $env:RTT_EXEC_PATH
)
$ErrorActionPreference = 'Stop'
if (-not $ArmBin) {
    $ArmCommand = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
    if (-not $ArmCommand) { throw 'Set -ArmBin to the Arm GNU toolchain bin directory.' }
    $ArmBin = Split-Path -Parent $ArmCommand.Source
}
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$RttSource = Join-Path $PSScriptRoot 'rt-thread'
$BspPath = Join-Path $RttSource 'bsp\nxp\mcx\mcxa\frdm-mcxa346'
$AppPath = Join-Path $ProjectRoot 'frdm-mcxa346-stepper-rtthread'
$OutputPath = Join-Path $ProjectRoot 'output\firmware'
New-Item -ItemType Directory -Force -Path $OutputPath | Out-Null
# Copy individual maintained files without deleting unrelated user files.
Copy-Item -Path "$AppPath\src\*.c" -Destination "$BspPath\applications\stepper\src" -Force
Copy-Item -Path "$AppPath\inc\*.h" -Destination "$BspPath\applications\stepper\inc" -Force
Copy-Item -LiteralPath "$AppPath\SConscript" -Destination "$BspPath\applications\stepper\SConscript" -Force
& $Python (Join-Path $PSScriptRoot "prepare-bsp.py") $BspPath
if ($LASTEXITCODE -ne 0) { throw "BSP preparation failed" }
$env:RTT_EXEC_PATH = $ArmBin
$env:RTT_ROOT = $RttSource
$env:RTT_CC = 'gcc'
$env:PYTHONUTF8 = '1'
Push-Location -LiteralPath $BspPath
try {
    & $Python -m SCons -j4 *> (Join-Path $OutputPath 'build.log')
    if ($LASTEXITCODE -ne 0) { throw 'Build failed. See output/firmware/build.log.' }
    Copy-Item -LiteralPath 'rtthread.elf','rtthread.bin','rtthread.map' -Destination $OutputPath -Force
} finally { Pop-Location }
Write-Host "Built firmware: $OutputPath"
