import json
import logging
import os
import ssl
import time

import paho.mqtt.client as mqtt
from tb_gateway_mqtt import TBGatewayMqttClient

import config

# Log file lives next to this script, regardless of where it's deployed/run from.
LOG_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_FILE = os.path.join(LOG_DIR, "bridge.log")

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
    handlers=[
        logging.FileHandler(LOG_FILE),
        logging.StreamHandler(),
    ],
)
log = logging.getLogger("sitetwin-bridge")


def connect_tb_gateway():
    """Keep retrying until ThingsBoard gateway connection succeeds."""
    while True:
        try:
            client = TBGatewayMqttClient(config.TB_HOST, config.TB_PORT, config.TB_GATEWAY_TOKEN)
            client.connect()
            log.info("Connected to ThingsBoard gateway at %s:%s", config.TB_HOST, config.TB_PORT)
            return client
        except Exception as exc:
            log.error("Failed to connect to ThingsBoard gateway: %s. Retrying in 5s...", exc)
            time.sleep(5)


tb_gateway = connect_tb_gateway()


def on_hivemq_connect(client, userdata, flags, reason_code, properties=None):
    if reason_code == 0:
        log.info("Connected to HiveMQ, subscribing to %s", config.HIVEMQ_TOPIC)
        client.subscribe(config.HIVEMQ_TOPIC, qos=1)
    else:
        log.error("Failed to connect to HiveMQ, reason_code=%s", reason_code)


def on_hivemq_disconnect(client, userdata, flags, reason_code, properties=None):
    log.warning("Disconnected from HiveMQ, reason_code=%s. paho-mqtt will auto-reconnect.", reason_code)


def on_hivemq_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        log.error("Failed to parse message on %s: %s", msg.topic, exc)
        return

    pod_id = payload.get("pod_id")
    sensor_id = payload.get("sensor_id")
    value = payload.get("value")
    record_class = payload.get("record_class")

    if pod_id is None or sensor_id is None or value is None:
        log.warning("Message missing pod_id/sensor_id/value, skipping: %s", payload)
        return

    if record_class == "health":
        # Health/heartbeat records carry no meaningful sensor value (sensor_kind is
        # "unknown", value is a placeholder). Forward them as a liveness signal instead
        # of a regular reading, so ThingsBoard's "last updated" time on this key can be
        # used directly as a last-seen indicator.
        telemetry = {f"{sensor_id}_heartbeat": True}
        for field in ("sequence", "boot_id", "uptime_ms"):
            if field in payload:
                telemetry[f"{sensor_id}_heartbeat_{field}"] = payload[field]
    else:
        # Primary reading. sequence/uptime_ms/quality_flags change on every message
        # (or every message that carries this sensor_id), so they stay in telemetry
        # alongside the value -- they describe "this particular reading", not the
        # device itself.
        telemetry = {sensor_id: value}
        for field in ("sequence", "uptime_ms", "quality_flags"):
            if field in payload:
                telemetry[f"{sensor_id}_{field}"] = payload[field]

        # sensor_kind, unit, boot_id, and record_class describe the channel/device
        # itself rather than a single reading -- they only change when hardware
        # changes or the pod reboots, so they belong in attributes (latest-value
        # snapshot), not telemetry (time-series history).
        attributes = {}
        for field in ("sensor_kind", "unit", "boot_id", "record_class"):
            if field in payload:
                attributes[f"{sensor_id}_{field}"] = payload[field]
        if attributes:
            try:
                tb_gateway.gw_send_attributes(pod_id, attributes)
            except Exception as exc:
                log.error("Failed to forward attributes to ThingsBoard: %s", exc)

    try:
        tb_gateway.gw_send_telemetry(pod_id, telemetry)
        log.info("Forwarded to ThingsBoard: device=%s telemetry=%s", pod_id, telemetry)
    except Exception as exc:
        log.error("Failed to forward telemetry to ThingsBoard: %s", exc)


def main():
    hivemq_client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id="sitetwin-bridge",
    )
    hivemq_client.username_pw_set(config.HIVEMQ_USERNAME, config.HIVEMQ_PASSWORD)
    hivemq_client.tls_set(cert_reqs=ssl.CERT_REQUIRED)
    hivemq_client.on_connect = on_hivemq_connect
    hivemq_client.on_disconnect = on_hivemq_disconnect
    hivemq_client.on_message = on_hivemq_message

    # paho-mqtt built-in reconnect backoff: retry between 1s and 30s.
    hivemq_client.reconnect_delay_set(min_delay=1, max_delay=30)

    log.info("Connecting to HiveMQ at %s:%s", config.HIVEMQ_HOST, config.HIVEMQ_PORT)
    hivemq_client.connect(config.HIVEMQ_HOST, config.HIVEMQ_PORT, keepalive=60)

    hivemq_client.loop_forever(retry_first_connection=True)


if __name__ == "__main__":
    main()