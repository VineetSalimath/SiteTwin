import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from bridge_core import RpcCommandBridge


class DeterministicCommandPath:
    """ThingsBoard shape -> bridge -> Wi-Fi schema -> Zigbee result abstraction."""

    def __init__(self):
        self.outbound = []
        self.responses = []
        self.connected = True
        self.bridge = RpcCommandBridge(self.publish, self.reply)

    def publish(self, topic, payload):
        if not self.connected:
            return False
        command = json.loads(payload)
        required = {"schema_version", "command_id", "pod_id", "command_type",
                    "target", "issued_at_ms", "valid_for_ms"}
        if command["schema_version"] != 3 or not required.issubset(command):
            return False
        self.outbound.append((topic, command))
        return True

    def reply(self, pod_id, request_id, response):
        self.responses.append((pod_id, request_id, response))

    def deliver(self, index=0, status="executed", reason="none", now_ms=100):
        command = self.outbound[index][1]
        result = {"schema_version": 3, "pod_id": command["pod_id"],
                  "command_id": command["command_id"], "status": status,
                  "reason": reason, "timestamp_ms": now_ms,
                  "applied_config_revision": command.get("config_revision", 0),
                  "config_value": command.get("value", 0.0)}
        return self.bridge.handle_command_result(result, now_ms)


class FullCommandFlowTests(unittest.TestCase):
    def test_valid_object_and_json_string_requests(self):
        path = DeterministicCommandPath()
        first = {"device": "POD_1", "data": {"id": 10, "method": "set_threshold",
                 "params": {"value": 1200, "config_revision": 2}}}
        second = {"device": "POD_2", "data": {"id": 11, "method": "get_rule",
                  "params": "{\"capability\":\"contact\","
                            "\"rule_kind\":\"event_debounce_ms\"}"}}
        self.assertEqual(path.bridge.handle_rpc(first, 1), "pending")
        self.assertEqual(path.bridge.handle_rpc(second, 2), "pending")
        self.assertEqual(path.deliver(0), "completed")
        self.assertEqual(path.deliver(1), "completed")
        self.assertEqual(len(path.responses), 2)

    def test_invalid_duplicate_timeout_late_reconnect_and_exactly_once(self):
        path = DeterministicCommandPath()
        invalid = {"device": "POD_1", "data": {"id": 20, "method": "set_rule",
                   "params": {"capability": "co2_ppm"}}}
        self.assertEqual(path.bridge.handle_rpc(invalid, 0), "rejected")
        self.assertEqual(len(path.responses), 1)

        valid = {"device": "POD_1", "data": {"id": 21, "method": "get_config",
                 "params": {"valid_for_ms": 100}}}
        self.assertEqual(path.bridge.handle_rpc(valid, 10), "pending")
        self.assertEqual(path.bridge.handle_rpc(valid, 11), "duplicate")
        path.connected = False
        path.connected = True
        path.bridge.tick(110)
        self.assertEqual(path.responses[-1][2]["reason"], "timeout")
        self.assertEqual(path.deliver(0, now_ms=120), "late")
        self.assertEqual(len(path.responses), 2)

        exactly_once = {"device": "POD_3", "data": {"id": 22,
                        "method": "get_rule",
                        "params": {"capability": "current_ma",
                                   "rule_kind": "report_deadband"}}}
        self.assertEqual(path.bridge.handle_rpc(exactly_once, 200), "pending")
        self.assertEqual(path.deliver(1, now_ms=210), "completed")
        self.assertEqual(path.deliver(1, now_ms=211), "late")
        self.assertEqual(len(path.responses), 3)


if __name__ == "__main__":
    unittest.main()
