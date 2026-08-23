$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $projectRoot
$coreInclude = Join-Path $repositoryRoot 'components\sitetwin_core\include'
$sensorRuntimeInclude = Join-Path $repositoryRoot 'components\sitetwin_sensor_runtime\include'
$sensorsInclude = Join-Path $repositoryRoot 'components\sitetwin_sensors\include'
$fakeInclude = Join-Path $projectRoot 'components\sitetwin_fake_hal\include'
$outputPath = Join-Path $env:TEMP 'sitetwin-host-tests.exe'
$sources = @(
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\alarm.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\capability_config.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\command.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\control_event.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\contracts.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\gateway_frame.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\gateway_identity.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\gateway_json.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\gateway_processor.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\gateway_registry.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\gateway_runtime.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\gateway_state.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\pod_runtime.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\reporting_policy.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\sensor_registry.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\telemetry_queue.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_core\src\zigbee_payload.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensor_runtime\src\logical_channel_adapter.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensor_runtime\src\final_pcb_board.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensor_runtime\src\board_port_manager.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensor_runtime\src\hotswap_module_binding.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensor_runtime\src\hotswap_zigbee_slot.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensor_runtime\src\module_instance.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensors\src\scd41.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensors\src\sgp40.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensors\src\sht41.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensors\src\bh1750.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensors\src\reed.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensors\src\ina219.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensors\src\pir.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensors\src\adxl345.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensors\src\ds18b20.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensors\src\voc_index_algorithm.c'),
    (Join-Path $repositoryRoot 'components\sitetwin_sensors\third_party\sensirion_gas_index_algorithm\sensirion_gas_index_algorithm.c'),
    (Join-Path $projectRoot 'components\sitetwin_fake_hal\src\fake_sensor.c'),
    (Join-Path $PSScriptRoot 'test_scd41.c'),
    (Join-Path $PSScriptRoot 'test_sgp40.c'),
    (Join-Path $PSScriptRoot 'test_command_actuation.c'),
    (Join-Path $PSScriptRoot 'test_command_transport.c'),
    (Join-Path $PSScriptRoot 'test_alarm_control.c'),
    (Join-Path $PSScriptRoot 'test_gateway_state.c'),
    (Join-Path $PSScriptRoot 'test_gateway_identity.c'),
    (Join-Path $PSScriptRoot 'test_final_pcb_board.c'),
    (Join-Path $PSScriptRoot 'test_board_port_manager.c'),
    (Join-Path $PSScriptRoot 'test_hotswap_module_binding.c'),
    (Join-Path $PSScriptRoot 'test_hotswap_zigbee_slot.c'),
    (Join-Path $PSScriptRoot 'test_hotswap_integration.c'),
    (Join-Path $PSScriptRoot 'test_sensor_foundation.c'),
    (Join-Path $PSScriptRoot 'test_bh1750.c'),
    (Join-Path $PSScriptRoot 'test_reed.c'),
    (Join-Path $PSScriptRoot 'test_ina219.c'),
    (Join-Path $PSScriptRoot 'test_pir.c'),
    (Join-Path $PSScriptRoot 'test_adxl345.c'),
    (Join-Path $PSScriptRoot 'test_ds18b20.c'),
    (Join-Path $PSScriptRoot 'test_runner.c')
)

$gasAlgorithmInclude = Join-Path $repositoryRoot `
    'components\sitetwin_sensors\third_party\sensirion_gas_index_algorithm'

& gcc -std=c11 -Wall -Wextra -Werror -I $coreInclude -I $sensorRuntimeInclude `
    -I $sensorsInclude -I $gasAlgorithmInclude -I $fakeInclude $sources -o $outputPath -lm
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $outputPath
exit $LASTEXITCODE
