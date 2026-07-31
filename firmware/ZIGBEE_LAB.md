# SiteTwin Zigbee Lab

This is a temporary two-board radio test. It proves that the ESP32-C6 gateway can form a Zigbee network and the
ESP32-C6 pod can join it. It does not replace the SiteTwin firmware and does not send JSON.

## Boards

| Board | USB port | Lab role |
| --- | --- | --- |
| Gateway | COM10 | Zigbee coordinator |
| Pod | COM3 | Zigbee end device |

## Lab project

The official Espressif console example is stored at:

```text
SiteTwin/external/esp-zigbee-sdk/examples/all_device_types_app
```

Build the project for `esp32c6`, then flash the same lab image to both boards. Use a separate ESP-IDF terminal
for each board when monitoring them.

## Gateway console sequence

At the `esp>` prompt on COM10:

```text
role zc
zha add 1 on_off_light
dm register
bdb_comm start form
network open -t 300
```

This makes COM10 the coordinator, creates the temporary test endpoint, forms a Zigbee network, and opens joining
for five minutes.

## Pod console sequence

At the `esp>` prompt on COM3 while the gateway network is open:

```text
role zed
zha add 2 on_off_switch
dm register
bdb_comm start steer
```

The pod should report that it joined the gateway network.

## What success means

The gateway forms a network, reports short address `0x0000`, and opens joining. The pod reports a successful join
and receives a non-zero short address. After this passes, the next increment is a SiteTwin custom ZCL cluster that
carries the existing fixed 30-byte telemetry payload from the pod to the gateway.
