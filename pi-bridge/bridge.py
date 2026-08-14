import json
import logging
import os
import ssl
import time

import paho.mqtt.client as mqtt
from tb_gateway_mqtt import TBGatewayMqttClient

import config
from bridge_core import COMMAND_RESULTS_TOPIC, RpcCommandBridge, telemetry_projection


LOG_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_FILE = os.path.join(LOG_DIR, "bridge.log")
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
    handlers=[logging.FileHandler(LOG_FILE), logging.StreamHandler()],
)
log = logging.getLogger("sitetwin-bridge")


def connect_tb_gateway():
    while True:
        try:
            client = TBGatewayMqttClient(config.TB_HOST, config.TB_PORT,
                                         config.TB_GATEWAY_TOKEN)
            client.connect()
            log.info("Connected to ThingsBoard gateway at %s:%s",
                     config.TB_HOST, config.TB_PORT)
            return client
        except Exception as exc:
            log.error("ThingsBoard connection failed: %s; retrying in 5s", exc)
            time.sleep(5)


class LiveBridge:
    def __init__(self, tb_gateway, hivemq_client):
        self.tb_gateway = tb_gateway
        self.hivemq_client = hivemq_client
        self.connected_devices = set()
        self.command_bridge = RpcCommandBridge(self._publish_command,
                                               self._send_rpc_reply)

    def _publish_command(self, topic, payload):
        result = self.hivemq_client.publish(topic, payload, qos=1)
        return result.rc == mqtt.MQTT_ERR_SUCCESS

    def _send_rpc_reply(self, pod_id, request_id, response):
        self.tb_gateway.gw_send_rpc_reply(pod_id, request_id, response)
        log.info("RPC completed: pod=%s request=%s status=%s reason=%s",
                 pod_id, request_id, response.get("status"), response.get("reason"))

    def on_server_side_rpc(self, gateway, content):
        del gateway
        outcome = self.command_bridge.handle_rpc(content)
        data = content.get("data") if isinstance(content, dict) else None
        data = data if isinstance(data, dict) else {}
        log.info("RPC ingress outcome=%s device=%s request=%s", outcome,
                 content.get("device") if isinstance(content, dict) else None,
                 data.get("id"))

    def on_hivemq_connect(self, client, userdata, flags, reason_code, properties=None):
        del userdata, flags, properties
        if reason_code == 0:
            client.subscribe(config.HIVEMQ_TOPIC, qos=1)
            client.subscribe(COMMAND_RESULTS_TOPIC, qos=1)
            log.info("Connected to HiveMQ; telemetry and command-result subscriptions restored")
            self.command_bridge.tick()
        else:
            log.error("HiveMQ connection failed, reason_code=%s", reason_code)

    def on_hivemq_disconnect(self, client, userdata, flags, reason_code, properties=None):
        del client, userdata, flags, properties
        log.warning("Disconnected from HiveMQ, reason_code=%s; automatic reconnect enabled",
                    reason_code)

    def _ensure_device_connected(self, pod_id):
        if pod_id in self.connected_devices:
            return True
        try:
            self.tb_gateway.gw_connect_device(pod_id)
        except Exception as exc:
            log.error("Failed to declare %s connected: %s", pod_id, exc)
            return False
        self.connected_devices.add(pod_id)
        return True

    def on_hivemq_message(self, client, userdata, message):
        del client, userdata
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            log.error("Invalid JSON on %s: %s", message.topic, exc)
            return
        if message.topic.endswith("/command_results"):
            outcome = self.command_bridge.handle_command_result(payload)
            log.info("Command result outcome=%s pod=%s command=%s", outcome,
                     payload.get("pod_id"), payload.get("command_id"))
            return
        try:
            pod_id, attributes, telemetry = telemetry_projection(payload)
        except ValueError as exc:
            log.warning("Rejected telemetry on %s: %s", message.topic, exc)
            return
        if not self._ensure_device_connected(pod_id):
            return
        try:
            if attributes:
                self.tb_gateway.gw_send_attributes(pod_id, attributes)
            self.tb_gateway.gw_send_telemetry(pod_id, telemetry)
        except Exception as exc:
            log.error("ThingsBoard forwarding failed for %s: %s", pod_id, exc)


def main():
    tb_gateway = connect_tb_gateway()
    hivemq_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
                                client_id="sitetwin-bridge")
    hivemq_client.username_pw_set(config.HIVEMQ_USERNAME, config.HIVEMQ_PASSWORD)
    hivemq_client.tls_set(cert_reqs=ssl.CERT_REQUIRED)
    runtime = LiveBridge(tb_gateway, hivemq_client)
    tb_gateway.gw_set_server_side_rpc_request_handler(runtime.on_server_side_rpc)
    hivemq_client.on_connect = runtime.on_hivemq_connect
    hivemq_client.on_disconnect = runtime.on_hivemq_disconnect
    hivemq_client.on_message = runtime.on_hivemq_message
    hivemq_client.reconnect_delay_set(min_delay=1, max_delay=30)
    hivemq_client.connect(config.HIVEMQ_HOST, config.HIVEMQ_PORT, keepalive=60)
    hivemq_client.loop_start()
    try:
        while True:
            runtime.command_bridge.tick()
            time.sleep(0.1)
    except KeyboardInterrupt:
        log.info("Bridge stopped by user")
    finally:
        hivemq_client.loop_stop()
        hivemq_client.disconnect()


if __name__ == "__main__":
    main()
