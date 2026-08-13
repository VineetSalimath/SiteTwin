import json
import logging
import os
import ssl
import threading
import time

import paho.mqtt.client as mqtt
from tb_gateway_mqtt import TBGatewayMqttClient

import config

LOG_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_FILE = os.path.join(LOG_DIR, "bridge.log")
COMMAND_ACKS_TOPIC = "sitetwin/pods/+/command_acks"
TERMINAL_STATUSES = {"executed", "rejected", "expired", "duplicate", "failed"}
SUPPORTED_COMMANDS = {
    "set_threshold": "co2_threshold_ppm",
    "silence_alarm": "alarm",
    "test_output": "alarm",
    "get_config": "config",
}
SUPPORTED_TARGETS = {
    "set_threshold": {"co2_threshold_ppm"},
    "silence_alarm": {"alarm"},
    "test_output": {"led", "buzzer", "alarm"},
    "get_config": {"config"},
}
ENVIRONMENT_POD_ID = "POD_1"
MAX_JSON_INTEGER = (1 << 53) - 1

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
    handlers=[logging.FileHandler(LOG_FILE), logging.StreamHandler()],
)
log = logging.getLogger("sitetwin-bridge")


def connect_tb_gateway():
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
hivemq_client = None
connected_devices = set()
pending_rpcs = {}
pending_lock = threading.Lock()


def send_rpc_reply(pod_id, command_id, payload):
    try:
        tb_gateway.gw_send_rpc_reply(pod_id, command_id, payload)
        log.info("Closed RPC: device=%s command_id=%s status=%s", pod_id, command_id,
                 payload.get("status"))
    except Exception as exc:
        log.error("Failed to send RPC reply for command_id=%s: %s", command_id, exc)


def expire_pending_rpc(key):
    with pending_lock:
        pending = pending_rpcs.pop(key, None)
    if pending is None:
        return
    pod_id, command_id = key
    send_rpc_reply(
        pod_id,
        command_id,
        {
            "command_id": command_id,
            "pod_id": pod_id,
            "status": "failed",
            "reason": "timeout",
            "applied_config_revision": 0,
            "timestamp_ms": int(time.time() * 1000),
        },
    )


def bounded_int(value, minimum, maximum):
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError("integer required")
    if value < minimum or value > maximum:
        raise ValueError("integer outside bounds")
    return value


def normalize_rpc_params(params):
    if params is None or params == "":
        return {}
    if isinstance(params, str):
        try:
            params = json.loads(params)
        except json.JSONDecodeError as exc:
            raise ValueError("params must contain valid JSON") from exc
    if not isinstance(params, dict):
        raise ValueError("params must be a JSON object")
    return params


def normalize_command_id(command_id):
    if isinstance(command_id, str) and command_id.isdecimal():
        command_id = int(command_id)
    if (isinstance(command_id, bool) or not isinstance(command_id, int) or
            command_id <= 0 or command_id > MAX_JSON_INTEGER):
        raise ValueError("command id must be a positive JSON-safe integer")
    return command_id


def on_server_side_rpc(gateway, content):
    del gateway
    global hivemq_client
    device_name = content.get("device")
    rpc_data = content.get("data", {})
    command_id = rpc_data.get("id")
    method = rpc_data.get("method")
    params = rpc_data.get("params")
    log.info(
        "Received RPC: device=%r command_id=%r method=%r params_type=%s params=%r",
        device_name, command_id, method, type(params).__name__, params,
    )
    try:
        command_id = normalize_command_id(command_id)
    except ValueError:
        if device_name and command_id is not None:
            send_rpc_reply(device_name, command_id, {"status": "rejected", "reason": "invalid_syntax"})
        return
    if not device_name or method not in SUPPORTED_COMMANDS:
        if device_name:
            send_rpc_reply(device_name, command_id, {"status": "rejected", "reason": "invalid_syntax"})
        return
    if device_name != ENVIRONMENT_POD_ID:
        send_rpc_reply(device_name, command_id, {"status": "rejected", "reason": "unsupported"})
        return
    try:
        params = normalize_rpc_params(params)
    except ValueError:
        send_rpc_reply(device_name, command_id, {"status": "rejected", "reason": "invalid_syntax"})
        return

    try:
        valid_for_ms = bounded_int(params.get("valid_for_ms", 10000), 1, 60000)
        config_revision = bounded_int(params.get("config_revision", 0), 0, (1 << 32) - 1)
        duration_ms = bounded_int(params.get("duration_ms", 0), 0, (1 << 32) - 1)
    except ValueError:
        send_rpc_reply(device_name, command_id, {"status": "rejected", "reason": "out_of_bounds"})
        return
    value = params.get("value", 0)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        send_rpc_reply(device_name, command_id, {"status": "rejected", "reason": "invalid_syntax"})
        return
    issued_at_ms = int(time.time() * 1000)
    target = params.get("target", SUPPORTED_COMMANDS[method])
    if target not in SUPPORTED_TARGETS[method]:
        send_rpc_reply(device_name, command_id, {"status": "rejected", "reason": "unsupported"})
        return
    if method == "set_threshold" and not 400 <= value <= 5000:
        send_rpc_reply(device_name, command_id, {"status": "rejected", "reason": "out_of_bounds"})
        return
    if method == "silence_alarm" and not 1 <= duration_ms <= 300000:
        send_rpc_reply(device_name, command_id, {"status": "rejected", "reason": "out_of_bounds"})
        return
    if method == "test_output" and not 1 <= duration_ms <= 5000:
        send_rpc_reply(device_name, command_id, {"status": "rejected", "reason": "out_of_bounds"})
        return
    command_payload = {
        "schema_version": 1,
        "command_id": command_id,
        "target_pod_id": device_name,
        "command_type": method,
        "target": target,
        "parameters": {
            "value": value,
            "duration_ms": duration_ms,
        },
        "issued_at_ms": issued_at_ms,
        "expires_at_ms": issued_at_ms + valid_for_ms,
        "valid_for_ms": valid_for_ms,
        "config_revision": config_revision,
        "source": "thingsboard",
    }
    key = (device_name, command_id)
    with pending_lock:
        if key in pending_rpcs:
            send_rpc_reply(device_name, command_id, {"status": "duplicate", "reason": "none"})
            return
        timer = threading.Timer(valid_for_ms / 1000.0, expire_pending_rpc, args=(key,))
        timer.daemon = True
        pending_rpcs[key] = {"timer": timer}
        timer.start()

    topic = f"sitetwin/pods/{device_name}/commands"
    try:
        if hivemq_client is None:
            raise RuntimeError("HiveMQ client is not initialized")
        result = hivemq_client.publish(topic, json.dumps(command_payload), qos=1)
        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(f"MQTT publish rc={result.rc}")
        log.info("Forwarded RPC command to %s: %s", topic, command_payload)
    except Exception as exc:
        with pending_lock:
            pending = pending_rpcs.pop(key, None)
        if pending is not None:
            pending["timer"].cancel()
        log.error("Failed to publish downstream command: %s", exc)
        send_rpc_reply(device_name, command_id, {"status": "failed", "reason": "transport_failed"})


tb_gateway.gw_set_server_side_rpc_request_handler(on_server_side_rpc)


def on_hivemq_connect(client, userdata, flags, reason_code, properties=None):
    del userdata, flags, properties
    if reason_code == 0:
        log.info("Connected to HiveMQ; subscribing to telemetry and command acknowledgements")
        client.subscribe(config.HIVEMQ_TOPIC, qos=1)
        client.subscribe(COMMAND_ACKS_TOPIC, qos=1)
    else:
        log.error("Failed to connect to HiveMQ, reason_code=%s", reason_code)


def on_hivemq_disconnect(client, userdata, flags, reason_code, properties=None):
    del client, userdata, flags, properties
    log.warning("Disconnected from HiveMQ, reason_code=%s. Auto-reconnect is enabled.", reason_code)


def on_command_ack(payload):
    pod_id = payload.get("pod_id")
    command_id = payload.get("command_id")
    status = payload.get("status")
    if pod_id is None or command_id is None or status is None:
        log.warning("Ignoring malformed command acknowledgement: %s", payload)
        return
    if status not in TERMINAL_STATUSES:
        log.info("Command %s for %s is %s; RPC remains open", command_id, pod_id, status)
        return
    key = (pod_id, command_id)
    with pending_lock:
        pending = pending_rpcs.pop(key, None)
    if pending is None:
        log.warning("Terminal acknowledgement has no pending RPC: %s", payload)
        return
    pending["timer"].cancel()
    send_rpc_reply(pod_id, command_id, payload)


def on_hivemq_message(client, userdata, msg):
    del client, userdata
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
        except Exception as exc:
            log.error("Failed to register gateway device %s: %s", pod_id, exc)
            return

    if record_class == "health":
        telemetry = {f"{sensor_id}_heartbeat": True}
        for field in ("sequence", "boot_id", "uptime_ms"):
            if field in payload:
                telemetry[f"{sensor_id}_heartbeat_{field}"] = payload[field]
    else:
        telemetry = {sensor_id: value}
        for field in ("sequence", "uptime_ms", "quality_flags"):
            if field in payload:
                telemetry[f"{sensor_id}_{field}"] = payload[field]
    attributes = {}
    for field in ("sensor_kind", "unit", "boot_id", "record_class"):
        if field in payload:
            attributes[f"{sensor_id}_{field}"] = payload[field]
    try:
        if attributes:
            tb_gateway.gw_send_attributes(pod_id, attributes)
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
    hivemq_client.reconnect_delay_set(min_delay=1, max_delay=30)
    log.info("Connecting to HiveMQ at %s:%s", config.HIVEMQ_HOST, config.HIVEMQ_PORT)
    hivemq_client.connect(config.HIVEMQ_HOST, config.HIVEMQ_PORT, keepalive=60)
    hivemq_client.loop_forever(retry_first_connection=True)


if __name__ == "__main__":
    main()
