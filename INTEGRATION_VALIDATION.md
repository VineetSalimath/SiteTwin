# End-to-End Integration Validation

## Verified physical path

The deployed path has been demonstrated with real Pod 1, Pod 2, and Pod 3
sensor records:

`pod -> Zigbee coordinator -> UART1 -> Wi-Fi ESP -> JSON -> MQTTS/HiveMQ`

- Pods join the coordinator and transmit fixed 30-byte telemetry payloads.
- The coordinator accepts the payload on custom cluster `0xFC00`, command `0x01`.
- The coordinator sends a 48-byte gateway frame on UART1 GPIO4 (TX).
- The Wi-Fi ESP receives the frame on UART1 GPIO5 (RX), with shared ground and 115200 baud,
  8N1 signalling.
- The Wi-Fi ESP CRC-validates and decodes the payload, produces JSON, publishes it to HiveMQ
  over TLS, and receives an MQTT publish acknowledgement.
- The Pi bridge consumes HiveMQ telemetry and forwards it to ThingsBoard. The
  downstream RPC path has also been verified with `get_config`, `test_output`,
  and `set_threshold`, including a physical Pod 1 LED response.

Physical telemetry evidence includes Pod 2 illuminance/contact and all Pod 3
paths: INA219, ADXL345, and DS18B20. On 2026-08-13 the powered waterproof
DS18B20 probe was confirmed working on GPIO0, producing valid canonical
temperature telemetry through Zigbee, UART, MQTT, and the server path.
Resilience/soak testing, power optimisation, OTA, and security hardening remain
future work.

## Canonical contract

Both ESP-IDF projects consume `components/sitetwin_core`; there is no copied core.

| Contract item | Value |
| --- | --- |
| `ST_CONTRACT_VERSION` | `1` |
| Zigbee telemetry payload | `30` bytes |
| Gateway frame version | `1` |
| Gateway frame start marker | `0xA55A` (encoded little-endian) |
| Gateway frame header | `16` bytes |
| Frame CRC | CRC16-CCITT, `2` bytes |
| Message types | telemetry `1`, health `2`, command `3` |
| Source address | Zigbee short address, stored as little-endian `uint16_t` |
| Boot ID and sequence | little-endian `uint32_t` values in both payload and frame header |
| Sensor slot | payload byte offset `5` |
| Quality flags | little-endian `uint32_t` at payload byte offset `6` |

## Repeatable checks

```powershell
# Portable core and stress harness
powershell -ExecutionPolicy Bypass -File .\firmware\host_tests\run-tests.ps1

# Zigbee coordinator and pod images
cd .\firmware
powershell -ExecutionPolicy Bypass -File .\tools\gateway.ps1 build
powershell -ExecutionPolicy Bypass -File .\tools\pod.ps1 build

# Wi-Fi gateway image
cd ..\gateway-wifi
idf.py build
```
