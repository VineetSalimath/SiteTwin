$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $projectRoot
$coreInclude = Join-Path $repositoryRoot 'components\sitetwin_core\include'
$sensorRuntimeInclude = Join-Path $repositoryRoot 'components\sitetwin_sensor_runtime\include'
$sensorsInclude = Join-Path $repositoryRoot 'components\sitetwin_sensors\include'
$fakeInclude = Join-Path $projectRoot 'components\sitetwin_fake_hal\include'
$outputPath = Join-Path $env:TEMP 'sitetwin-host-tests.exe'
$sources = @(
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\contracts.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\gateway_frame.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\gateway_json.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\gateway_processor.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\gateway_registry.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\gateway_runtime.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\pod_runtime.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\reporting_policy.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\sensor_registry.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\telemetry_queue.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\zigbee_payload.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensor_runtime\src\logical_channel_adapter.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensor_runtime\src\module_instance.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensors\src\sht41.c'),
    (Join-Path $projectRoot 'components\sitetwin_fake_hal\src\fake_sensor.c'),
    (Join-Path $PSScriptRoot 'test_sensor_foundation.c'),
    (Join-Path $PSScriptRoot 'test_runner.c')
)

& gcc -std=c11 -Wall -Wextra -Werror -I $coreInclude -I $sensorRuntimeInclude `
    -I $sensorsInclude -I $fakeInclude $sources -o $outputPath
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $outputPath
exit $LASTEXITCODE
