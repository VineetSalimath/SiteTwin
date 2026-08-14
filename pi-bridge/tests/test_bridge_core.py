import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from bridge_core import RpcCommandBridge, telemetry_projection


class FakeIo:
    def __init__(self):
        self.published = []
        self.replies = []
        self.fail_replies = False

    def publish(self, topic, payload):
        self.published.append((topic, json.loads(payload)))
        return True

    def reply(self, pod_id, request_id, payload):
        if self.fail_replies:
            raise RuntimeError("offline")
        self.replies.append((pod_id, request_id, payload))


class BridgeCoreTests(unittest.TestCase):
    def setUp(self):
        self.io = FakeIo()
        self.bridge = RpcCommandBridge(self.io.publish, self.io.reply)

    def test_object_and_string_params(self):
        object_rpc = {"device": "POD_1", "data": {"id": 1, "method": "set_threshold",
                      "params": {"value": 1200, "config_revision": 2,
                                 "valid_for_ms": 10000}}}
        string_rpc = {"device": "POD_2", "data": {"id": 2, "method": "set_rule",
                      "params": json.dumps({"capability": "contact",
                                            "rule_kind": "event_debounce_ms",
                                            "value": 50, "config_revision": 1})}}
        self.assertEqual(self.bridge.handle_rpc(object_rpc, 1000), "pending")
        self.assertEqual(self.bridge.handle_rpc(string_rpc, 1001), "pending")
        self.assertEqual(self.io.published[0][1]["target"], "co2_threshold_ppm")
        self.assertEqual(self.io.published[1][1]["capability"], "contact")

    def test_invalid_params_are_rejected_without_downstream_publish(self):
        rpc = {"device": "POD_1", "data": {"id": 3, "method": "set_threshold",
               "params": {"value": 1200}}}
        self.assertEqual(self.bridge.handle_rpc(rpc, 2000), "rejected")
        self.assertEqual(self.io.published, [])
        self.assertEqual(len(self.io.replies), 1)
        self.assertEqual(self.io.replies[0][2]["reason"], "invalid_syntax")

    def test_duplicate_result_and_duplicate_rpc_reply_exactly_once(self):
        rpc = {"device": "POD_1", "data": {"id": 4, "method": "get_config",
               "params": {"valid_for_ms": 10000}}}
        self.assertEqual(self.bridge.handle_rpc(rpc, 3000), "pending")
        self.assertEqual(self.bridge.handle_rpc(rpc, 3001), "duplicate")
        command_id = self.io.published[0][1]["command_id"]
        result = {"schema_version": 2, "pod_id": "POD_1", "command_id": command_id,
                  "status": "executed", "reason": "none", "timestamp_ms": 10,
                  "applied_config_revision": 1, "config_value": 1000.0}
        self.assertEqual(self.bridge.handle_command_result(result, 3010), "completed")
        self.assertEqual(self.bridge.handle_command_result(result, 3011), "late")
        self.assertEqual(len(self.io.published), 1)
        self.assertEqual(len(self.io.replies), 1)

    def test_same_rpc_id_on_different_pods_has_distinct_downstream_ids(self):
        first = {"device": "POD_1", "data": {"id": 44, "method": "get_config",
                 "params": {}}}
        second = {"device": "POD_2", "data": {"id": 44, "method": "get_rule",
                  "params": {"capability": "contact",
                             "rule_kind": "event_debounce_ms"}}}
        self.assertEqual(self.bridge.handle_rpc(first, 3500), "pending")
        self.assertEqual(self.bridge.handle_rpc(second, 3500), "pending")
        first_id = self.io.published[0][1]["command_id"]
        second_id = self.io.published[1][1]["command_id"]
        self.assertNotEqual(first_id, second_id)

    def test_timeout_late_result_and_reply_retry_after_reconnect(self):
        rpc = {"device": "POD_3", "data": {"id": 5, "method": "get_rule",
               "params": {"capability": "vibration_rms_g",
                          "rule_kind": "report_deadband", "valid_for_ms": 100}}}
        self.io.fail_replies = True
        self.assertEqual(self.bridge.handle_rpc(rpc, 4000), "pending")
        self.bridge.tick(4100)
        self.assertEqual(self.io.replies, [])
        self.io.fail_replies = False
        self.bridge.tick(5100)
        self.assertEqual(len(self.io.replies), 1)
        self.assertEqual(self.io.replies[0][2]["reason"], "timeout")
        command_id = self.io.published[0][1]["command_id"]
        result = {"schema_version": 2, "pod_id": "POD_3", "command_id": command_id,
                  "status": "executed", "reason": "none", "timestamp_ms": 20}
        self.assertEqual(self.bridge.handle_command_result(result, 5200), "late")
        self.assertEqual(len(self.io.replies), 1)

    def test_attribute_telemetry_separation(self):
        payload = {"pod_id": "POD_1", "sensor_id": "scd41_co2",
                   "sensor_kind": "co2_ppm", "record_class": "state",
                   "sequence": 7, "boot_id": 2, "uptime_ms": 9000,
                   "value": 750.0, "unit": "ppm", "quality_flags": 1}
        pod_id, attributes, telemetry = telemetry_projection(payload)
        self.assertEqual(pod_id, "POD_1")
        self.assertEqual(attributes["scd41_co2_sensor_kind"], "co2_ppm")
        self.assertEqual(attributes["scd41_co2_boot_id"], 2)
        self.assertNotIn("scd41_co2_boot_id", telemetry)
        self.assertEqual(telemetry["scd41_co2"], 750.0)


if __name__ == "__main__":
    unittest.main()
