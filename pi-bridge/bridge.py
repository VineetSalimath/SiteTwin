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


def on_server_side_rpc(gateway, content):
    """
    Fired when ThingsBoard sends a server-side RPC request for one of our
    gateway-proxied devices (e.g. from the RPC debug terminal, a dashboard
    widget, or the Rule Engine).

    NOTE: tb-mqtt-client actually invokes this as
    devices_server_side_rpc_request_handler(self, content) -- two positional
    args, not three -- confirmed by reading the library source
    (tb_gateway_mqtt.py, GATEWAY_RPC_TOPIC branch). `content` is the decoded
    JSON payload from the v1/gateway/rpc topic, shaped like:
        {"device": "<name>", "data": {"id": <int>, "method": "<str>", "params": <any>}}

    This is currently a minimal probe: it just logs what arrived, so we can
    confirm the receive path works before building out the downstream
    forwarding (HiveMQ commands topic) and the ack/reply path.
    """
    device_name = content.get("device")
    rpc_data = content.get("data", {})
    request_id = rpc_data.get("id")
    method = rpc_data.get("method")
    params = rpc_data.get("params")
    log.info(
        "Received RPC: device=%s request_id=%s method=%s params=%s",
        device_name, request_id, method, params
    )

    # Translate ThingsBoard's native RPC shape (method/params) into our own
    # generic downstream command schema, mirroring the upstream sensor_id/
    # sensor_kind design: `method` is repurposed as the actuator slot id
    # (e.g. "ACT_0"), and we stamp our own send-time timestamp since RPC
    # requests don't carry one. `params` is passed through unchanged --
    # its shape (command_type, value, unit, etc.) is a contract between
    # whoever triggers the RPC and the pod firmware, not something bridge.py
    # needs to understand.
    #
    # NOTE: we are NOT calling gw_send_rpc_reply here. This request stays
    # unanswered until the matching command_ack comes back on
    # sitetwin/pods/{pod_id}/command_acks (handled by on_command_ack below),
    # at which point THAT is when gw_send_rpc_reply is called with the real
    # (simulated or, eventually, real) result. Replying immediately here
    # would be a lie -- claiming something completed before anything
    # downstream has actually responded.
    command_topic = f"sitetwin/pods/{device_name}/commands"
    command_payload = {
        "schema_version": 1,
        "pod_id": device_name,
        "command_id": request_id,
        "actuator_id": method,
        "params": params,
        "issued_at_ms": int(time.time() * 1000),
    }
    try:
        hivemq_client.publish(command_topic, json.dumps(command_payload), qos=1)
        log.info("Forwarded command to HiveMQ: topic=%s payload=%s", command_topic, command_payload)
    except Exception as exc:
        log.error("Failed to forward command to HiveMQ: %s", exc)


tb_gateway = connect_tb_gateway()
tb_gateway.gw_set_server_side_rpc_request_handler(on_server_side_rpc)

# Devices we've explicitly declared as "connected" to the gateway via
# gw_connect_device. This is required before ThingsBoard will route shared
# attribute updates or RPC commands for that device through this gateway --
# just sending telemetry/attributes for a pod_id is not enough on its own.
# In-memory only: resets on restart, but the first message for any pod_id
# after a restart will re-trigger the declaration naturally.
connected_devices = set()

# Set once main() creates the HiveMQ client, so on_server_side_rpc (registered
# above, before the client exists) can publish downstream commands on it.
# The RPC handler is only ever invoked after connect() succeeds, so by the
# time it fires, this will already be populated.
hivemq_client = None


COMMAND_ACKS_TOPIC = "sitetwin/pods/+/command_acks"


def on_hivemq_connect(client, userdata, flags, reason_code, properties=None):
    if reason_code == 0:
        log.info("Connected to HiveMQ, subscribing to %s and %s", config.HIVEMQ_TOPIC, COMMAND_ACKS_TOPIC)
        client.subscribe(config.HIVEMQ_TOPIC, qos=1)
        client.subscribe(COMMAND_ACKS_TOPIC, qos=1)
    else:
        log.error("Failed to connect to HiveMQ, reason_code=%s", reason_code)


def on_hivemq_disconnect(client, userdata, flags, reason_code, properties=None):
    log.warning("Disconnected from HiveMQ, reason_code=%s. paho-mqtt will auto-reconnect.", reason_code)


def on_command_ack(payload):
    """
    Fired for messages on sitetwin/pods/{pod_id}/command_acks -- the
    simulated (or, eventually, real) execution result C6 publishes after
    processing a downstream command. This is where we finally call
    gw_send_rpc_reply, closing the loop that on_server_side_rpc started:
    the ThingsBoard RPC debug terminal (or whatever triggered the RPC) has
    been waiting/timing out since the original request, and this is the
    first point where we have something honest to tell it.
    """
    pod_id = payload.get("pod_id")
    command_id = payload.get("command_id")
    status = payload.get("status")

    if pod_id is None or command_id is None:
        log.warning("command_ack missing pod_id/command_id, skipping: %s", payload)
        return

    try:
        tb_gateway.gw_send_rpc_reply(pod_id, command_id, payload)
        log.info(
            "Closed RPC loop: device=%s command_id=%s status=%s",
            pod_id, command_id, status
        )
    except Exception as exc:
        log.error("Failed to send RPC reply for command_id=%s: %s", command_id, exc)


def on_hivemq_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        log.error("Failed to parse message on %s: %s", msg.topic, exc)
        return

    if msg.topic.endswith("/command_acks"):
        on_command_ack(payload)
        return

    pod_id = payload.get("pod_id")
    sensor_id = payload.get("sensor_id")
    value = payload.get("value")
    record_class = payload.get("record_class")

    if pod_id is None or sensor_id is None or value is None:
        log.warning("Message missing pod_id/sensor_id/value, skipping: %s", payload)
        return

    if pod_id not in connected_devices:
        try:
            tb_gateway.gw_connect_device(pod_id)
            connected_devices.add(pod_id)
            log.info("Declared device connected to gateway: %s", pod_id)
        except Exception as exc:
            log.error("Failed to declare device connected: %s", exc)

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
    global hivemq_client
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