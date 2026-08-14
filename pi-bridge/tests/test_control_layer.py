import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from bridge_core import RpcCommandBridge, control_projection


class FakeIo:
    def __init__(self):
        self.published = []
        self.replies = []

    def publish(self, topic, payload):
        self.published.append((topic, json.loads(payload)))
        return True

    def reply(self, pod_id, request_id, response):
        self.replies.append((pod_id, request_id, response))


def base_event(kind, transition="active", instance_id=0):
    return {
        "schema_version": 1,
        "event_kind": kind,
        "transition": transition,
        "reason": "none",
        "pod_id": "POD_1",
        "sensor_id": "control_state",
        "instance_id": instance_id,
        "timestamp_ms": 100,
        "active": transition in {"active", "recovered"},
        "acknowledged": False,
        "silenced": False,
        "shared_alarm_indicator_verified": False,
        "capability": "co2_ppm",
        "secondary_capability": "unknown",
        "rule_kind": "numeric_high_threshold",
        "threshold": 1000.0,
        "observed": 1200.0,
        "secondary_observed": 0.0,
        "trend": 0.0,
        "evidence_count": 1,
        "quality_flags": 1,
        "config_revision": 2,
        "ruleset_revision": 3,
        "capability_mask": 15,
        "boot_id": 1,
        "sequence": 1,
    }


class ControlLayerTests(unittest.TestCase):
    def test_capability_and_configuration_projection(self):
        capability = base_event("capabilities", "snapshot")
        pod, attributes, telemetry = control_projection(capability)
        self.assertEqual(pod, "POD_1")
        self.assertIn("co2_ppm", attributes["capabilities"])
        self.assertFalse(attributes["shared_alarm_indicator"])
        self.assertEqual(telemetry["ruleset_revision"], 3)

        configuration = base_event("configuration", "updated")
        pod, attributes, telemetry = control_projection(configuration)
        key = "rule_co2_ppm_numeric_high_threshold"
        self.assertEqual(attributes[f"{key}_value"], 1000.0)
        self.assertEqual(telemetry[f"{key}_revision"], 2)
        self.assertTrue(telemetry[f"{key}_changed"])

    def test_alarm_ack_silence_and_reboot_projection_are_independent(self):
        condition = base_event("alarm_condition", "active", 9001)
        pod, attributes, telemetry = control_projection(condition)
        self.assertEqual(pod, "POD_1")
        self.assertEqual(attributes, {})
        prefix = "alarm_co2_ppm_numeric_high_threshold"
        self.assertEqual(telemetry[f"{prefix}_instance_id"], 9001)
        self.assertTrue(telemetry[f"{prefix}_active"])
        self.assertFalse(telemetry[f"{prefix}_acknowledged"])

        retrigger = base_event("alarm_condition", "active", 9002)
        _, retrigger_attributes, retrigger_telemetry = control_projection(retrigger)
        self.assertEqual(retrigger_attributes, {})
        self.assertEqual(set(retrigger_telemetry), set(telemetry))
        self.assertEqual(retrigger_telemetry[f"{prefix}_instance_id"], 9002)

        acknowledgement = base_event("alarm_acknowledgement", "acknowledged", 9001)
        acknowledgement["active"] = True
        acknowledgement["acknowledged"] = True
        _, _, telemetry = control_projection(acknowledgement)
        self.assertTrue(telemetry[f"{prefix}_active"])
        self.assertTrue(telemetry[f"{prefix}_acknowledged"])

        reboot = base_event("alarm_silence", "reboot_reset")
        reboot["reason"] = "reboot_reset"
        _, _, telemetry = control_projection(reboot)
        self.assertFalse(telemetry["alarm_silenced"])
        self.assertEqual(telemetry["alarm_silence_reason"], "reboot_reset")

    def test_gateway_incident_routes_to_gateway_device(self):
        incident = base_event("gateway_incident", "active", 7001)
        incident["sensor_id"] = "gateway_multi_sensor"
        incident["reason"] = "multi_sensor"
        incident["evidence_count"] = 2
        incident["secondary_observed"] = 0.08
        incident["trend"] = 0.01
        device, attributes, telemetry = control_projection(incident)
        prefix = "incident_POD_1_gateway_multi_sensor"
        self.assertEqual(device, "GATEWAY_1")
        self.assertEqual(attributes[f"{prefix}_source_pod"], "POD_1")
        self.assertEqual(telemetry[f"{prefix}_instance_id"], 7001)
        self.assertTrue(telemetry[f"{prefix}_active"])
        self.assertEqual(telemetry[f"{prefix}_evidence_count"], 2)

    def test_control_rpcs_and_capability_result_correlation(self):
        io = FakeIo()
        bridge = RpcCommandBridge(io.publish, io.reply)
        get_caps = {"device": "POD_1", "data": {"id": 60,
                    "method": "get_capabilities", "params": {}}}
        self.assertEqual(bridge.handle_rpc(get_caps, 1000), "pending")
        command = io.published[-1][1]
        self.assertEqual(command["schema_version"], 3)
        self.assertEqual(command["target"], "capabilities")
        result = {"schema_version": 3, "pod_id": "POD_1",
                  "command_id": command["command_id"], "status": "executed",
                  "reason": "none", "timestamp_ms": 5,
                  "capability_mask": (1 << 0) | (1 << 2),
                  "ruleset_revision": 4}
        self.assertEqual(bridge.handle_command_result(result, 1010), "completed")
        response = io.replies[-1][2]
        self.assertEqual(response["capabilities"], ["temperature_c", "co2_ppm"])
        self.assertFalse(response["shared_alarm_indicator"])

        ack = {"device": "POD_1", "data": {"id": 61, "method": "ack_alarm",
               "params": {"alarm_instance_id": 9001}}}
        self.assertEqual(bridge.handle_rpc(ack, 1100), "pending")
        ack_command = io.published[-1][1]
        self.assertEqual(ack_command["alarm_instance_id"], 9001)
        ack_result = {"schema_version": 3, "pod_id": "POD_1",
                      "command_id": ack_command["command_id"],
                      "status": "executed", "reason": "none",
                      "timestamp_ms": 6, "alarm_instance_id": 9001}
        self.assertEqual(bridge.handle_command_result(ack_result, 1110), "completed")
        self.assertEqual(io.replies[-1][2]["alarm_instance_id"], 9001)

    def test_silence_requires_instance_and_unsupported_result_is_exactly_once(self):
        io = FakeIo()
        bridge = RpcCommandBridge(io.publish, io.reply)
        invalid = {"device": "POD_1", "data": {"id": 70,
                   "method": "silence_alarm", "params": {"duration_ms": 1000}}}
        self.assertEqual(bridge.handle_rpc(invalid, 2000), "rejected")
        self.assertEqual(io.published, [])

        request = {"device": "POD_1", "data": {"id": 71,
                   "method": "silence_alarm",
                   "params": {"alarm_instance_id": 9001,
                              "duration_ms": 1000}}}
        self.assertEqual(bridge.handle_rpc(request, 2100), "pending")
        command = io.published[-1][1]
        result = {"schema_version": 3, "pod_id": "POD_1",
                  "command_id": command["command_id"], "status": "rejected",
                  "reason": "unsupported", "timestamp_ms": 7,
                  "alarm_instance_id": 9001}
        self.assertEqual(bridge.handle_command_result(result, 2110), "completed")
        self.assertEqual(bridge.handle_command_result(result, 2111), "late")
        self.assertEqual(len(io.replies), 2)  # local invalid + downstream unsupported
        self.assertEqual(io.replies[-1][2]["reason"], "unsupported")


if __name__ == "__main__":
    unittest.main()
