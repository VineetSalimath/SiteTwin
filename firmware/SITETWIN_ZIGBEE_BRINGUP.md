# SiteTwin Zigbee Bring-Up

This is the first runnable SiteTwin Zigbee application. It replaces the Espressif demos.

The same source builds two different images. The build scripts select their role in
their separate build directories, so neither role can overwrite the other:

- Gateway image: Zigbee Coordinator. It forms the network, accepts SiteTwin custom
  commands, validates the 30-byte payload, and passes it into `st_gateway_runtime_t`.
- Pod image: Zigbee End Device. It joins the gateway and sends one 30-byte SiteTwin
  health frame every 15 seconds as a bring-up signal. It is deliberately not a sensor
  driver; real sensor adapters will supply the records later.

## Wire format

The custom Zigbee cluster is `0xFC00` and its telemetry command is `0x01`. Its command
payload is exactly the existing `ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE` (30 bytes). The pod
uses `st_zigbee_telemetry_encode`; the gateway uses `st_gateway_runtime_ingest_zigbee`.
No JSON is created on this Zigbee-side gateway. The planned gateway-to-server ESP remains
responsible for the later UART frame and JSON conversion.

## Build and flash

Open an **ESP-IDF v5.5.4 PowerShell** terminal in `firmware`. The first build downloads
the declared `esp-zigbee-lib` dependency and can take several minutes.

Build and start the gateway first:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\gateway.ps1 set-target esp32c6
powershell -ExecutionPolicy Bypass -File .\tools\gateway.ps1 build
powershell -ExecutionPolicy Bypass -File .\tools\gateway.ps1 -p COM10 erase-flash
powershell -ExecutionPolicy Bypass -File .\tools\gateway.ps1 -p COM10 flash monitor
```

Wait for `Gateway formed network; opening joining for 240 seconds`. In a second ESP-IDF
terminal, build and start the pod:

```powershell
cd "B:\UoB\Dissertation\Dissertation\SiteTwin\firmware"
powershell -ExecutionPolicy Bypass -File .\tools\pod.ps1 set-target esp32c6
powershell -ExecutionPolicy Bypass -File .\tools\pod.ps1 build
powershell -ExecutionPolicy Bypass -File .\tools\pod.ps1 -p COM3 erase-flash
powershell -ExecutionPolicy Bypass -File .\tools\pod.ps1 -p COM3 flash monitor
```

Use the actual COM ports if they change. The expected gateway log is `Telemetry from
POD_.../SLOT_0: ingress result 0`. The expected pod log is `Sending SiteTwin health
sequence ...`.

## Current boundary

This proves the deployed path through the SiteTwin binary payload and gateway ingestion.
The remaining work is sensor hardware adapters, persistent pod identity/registry binding,
delivery of received frames over UART to the server ESP, and the server ESP's JSON/Wi-Fi
delivery.
