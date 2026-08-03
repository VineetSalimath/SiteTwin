# End-to-End Integration Validation

## Verified physical path

The deployed bring-up path has been demonstrated with the current health-record source:

`pod -> Zigbee coordinator -> UART1 -> Wi-Fi ESP -> JSON -> MQTTS/HiveMQ`

- The pod joins the coordinator and transmits one 30-byte health payload every 15 seconds.
- The coordinator accepts the payload on custom cluster `0xFC00`, command `0x01`.
- The coordinator sends a 48-byte gateway frame on UART1 GPIO4 (TX).
- The Wi-Fi ESP receives the frame on UART1 GPIO5 (RX), with shared ground and 115200 baud,
  8N1 signalling.
- The Wi-Fi ESP CRC-validates and decodes the payload, produces JSON, publishes it to HiveMQ
  over TLS, and receives an MQTT publish acknowledgement.
- The Pi bridge consumes the HiveMQ telemetry and forwards it to ThingsBoard; that bridge
  integration is verified separately, though it was not re-exercised during this UART run.

The source is a health/bring-up record, not a real sensor driver. Sensor acquisition,
resilience/soak testing, power optimisation, OTA, and security hardening remain future work.

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
