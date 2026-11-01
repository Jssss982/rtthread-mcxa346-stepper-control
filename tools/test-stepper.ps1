param([string]$Gcc='gcc')
$ErrorActionPreference='Stop'
$ProjectRoot=Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $ProjectRoot
try {
    & $Gcc -std=c99 -Wall -Wextra -Werror -Ifrdm-mcxa346-stepper-rtthread/tests -Ifrdm-mcxa346-stepper-rtthread/inc frdm-mcxa346-stepper-rtthread/tests/test_control.c frdm-mcxa346-stepper-rtthread/src/d36a_stepper.c frdm-mcxa346-stepper-rtthread/src/ms42cg_encoder.c frdm-mcxa346-stepper-rtthread/src/stepper_axis.c -o tools/test_control.exe
    if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
    & .\tools\test_control.exe
    if ($LASTEXITCODE -ne 0) { throw 'Control regression failed' }
} finally { Pop-Location }
