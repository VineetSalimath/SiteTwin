"""Pure SiteTwin bridge logic with no broker, credential, or hardware dependency."""

from collections import OrderedDict
from dataclasses import dataclass
import json
import math
import re
import time


COMMAND_SCHEMA_VERSION = 3
CONTROL_SCHEMA_VERSION = 1
MAX_EXACT_JSON_INTEGER = (1 << 53) - 1
COMMAND_TOPIC = "sitetwin/pods/{pod_id}/commands"
COMMAND_RESULTS_TOPIC = "sitetwin/pods/+/command_results"
CONTROL_TOPIC = "sitetwin/pods/+/control"
GATEWAY_INCIDENT_TOPIC = "sitetwin/gateway/incidents"
POD_ID_PATTERN = re.compile(r"^POD_[0-9A-F]+$")
TERMINAL_STATUSES = {"executed", "rejected", "expired", "duplicate", "failed"}
CAPABILITIES = {
    "temperature_c",
    "relative_humidity_percent",
    "co2_ppm",
    "voc_index",
    "illuminance_lux",
    "motion",
    "contact",
    "current_ma",
    "voltage_v",
    "vibration_rms_g",
}
RULE_KINDS = {
    "numeric_high_threshold",
    "numeric_low_threshold",
    "report_deadband",
    "report_min_interval_ms",
    "report_max_interval_ms",
    "event_debounce_ms",
    "event_retrigger_ms",
    "state_active_value",
}
CAPABILITY_ORDER = (
    "temperature_c",
    "relative_humidity_percent",
    "co2_ppm",
    "voc_index",
    "illuminance_lux",
    "motion",
    "contact",
    "current_ma",
    "voltage_v",
    "vibration_rms_g",
)
SHARED_ALARM_INDICATOR_BIT = 1 << 31


class InvalidRpc(ValueError):
    pass


@dataclass
class PendingRequest:
    pod_id: str
    request_id: int
    command_id: int
    deadline_ms: int
    method: str
    alarm_instance_id: int = 0


@dataclass
class CompletedRequest:
    response: dict
    delivered: bool = False
    next_attempt_ms: int = 0


def _integer(value, name, minimum=0, maximum=MAX_EXACT_JSON_INTEGER):
    if isinstance(value, bool) or not isinstance(value, int):
        raise InvalidRpc(f"{name} must be an integer")
    if value < minimum or value > maximum:
        raise InvalidRpc(f"{name} out of range")
    return value


def _finite_number(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise InvalidRpc(f"{name} must be numeric")
    value = float(value)
    if not math.isfinite(value):
        raise InvalidRpc(f"{name} must be finite")
    return value


def normalise_params(params):
    if params is None or params == "":
        return {}
    if isinstance(params, str):
        try:
            params = json.loads(params)
        except json.JSONDecodeError as exc:
            raise InvalidRpc("params string must contain one JSON object") from exc
    if not isinstance(params, dict):
        raise InvalidRpc("params must be an object or JSON-encoded object")
    return dict(params)


def command_from_rpc(content, now_ms):
    if not isinstance(content, dict) or not isinstance(content.get("data"), dict):
        raise InvalidRpc("RPC envelope must contain a data object")
    pod_id = content.get("device")
    data = content["data"]
    if not isinstance(pod_id, str) or len(pod_id) >= 16 or not POD_ID_PATTERN.fullmatch(pod_id):
        raise InvalidRpc("device must be a stable POD_x identity")
    request_id = _integer(data.get("id"), "id", minimum=1)
    method = data.get("method")
    if not isinstance(method, str):
        raise InvalidRpc("method must be a string")
    params = normalise_params(data.get("params"))
    valid_for_ms = _integer(params.get("valid_for_ms", 10000), "valid_for_ms",
                            minimum=1, maximum=60000)
    command = {
        "schema_version": COMMAND_SCHEMA_VERSION,
        "pod_id": pod_id,
        "command_type": method,
        "issued_at_ms": _integer(now_ms, "issued_at_ms"),
        "valid_for_ms": valid_for_ms,
    }

    if method == "set_threshold":
        command.update(
            target="co2_threshold_ppm",
            capability="co2_ppm",
            rule_kind="numeric_high_threshold",
            value=_finite_number(params.get("value"), "value"),
            config_revision=_integer(params.get("config_revision"),
                                     "config_revision", minimum=1,
                                     maximum=(1 << 32) - 1),
        )
    elif method == "get_config":
        command["target"] = "config"
    elif method in {"set_rule", "get_rule"}:
        capability = params.get("capability")
        rule_kind = params.get("rule_kind")
        if capability not in CAPABILITIES:
            raise InvalidRpc("unsupported capability name")
        if rule_kind not in RULE_KINDS:
            raise InvalidRpc("unsupported rule_kind name")
        command.update(target="capability_rule", capability=capability,
                       rule_kind=rule_kind)
        if method == "set_rule":
            command.update(
                value=_finite_number(params.get("value"), "value"),
                config_revision=_integer(params.get("config_revision"),
                                         "config_revision", minimum=1,
                                         maximum=(1 << 32) - 1),
            )
    elif method == "get_capabilities":
        command["target"] = "capabilities"
    elif method == "ack_alarm":
        command["target"] = "alarm"
        command["alarm_instance_id"] = _integer(
            params.get("alarm_instance_id"), "alarm_instance_id", minimum=1)
    elif method == "silence_alarm":
        command["target"] = "alarm"
        command["alarm_instance_id"] = _integer(
            params.get("alarm_instance_id"), "alarm_instance_id", minimum=1)
        command["duration_ms"] = _integer(params.get("duration_ms"),
                                           "duration_ms", minimum=1,
                                           maximum=60000)
    elif method == "test_output":
        command["target"] = "alarm"
        command["duration_ms"] = _integer(params.get("duration_ms", 1000),
                                           "duration_ms", minimum=1,
                                           maximum=60000)
    else:
        raise InvalidRpc("unknown RPC method")
    return pod_id, request_id, command


def terminal_response(pod_id, command_id, status, reason, timestamp_ms, **fields):
    response = {
        "schema_version": COMMAND_SCHEMA_VERSION,
        "command_id": command_id,
        "pod_id": pod_id,
        "status": status,
        "reason": reason,
        "timestamp_ms": timestamp_ms,
    }
    response.update(fields)
    return response


def telemetry_projection(payload):
    if not isinstance(payload, dict):
        raise ValueError("telemetry payload must be an object")
    pod_id = payload.get("pod_id")
    sensor_id = payload.get("sensor_id")
    if not isinstance(pod_id, str) or not isinstance(sensor_id, str) or "value" not in payload:
        raise ValueError("telemetry requires pod_id, sensor_id, and value")

    attributes = {}
    for field in ("sensor_kind", "unit", "boot_id", "record_class"):
        if field in payload:
            attributes[f"{sensor_id}_{field}"] = payload[field]

    if payload.get("record_class") == "health":
        telemetry = {f"{sensor_id}_heartbeat": True}
        prefix = f"{sensor_id}_heartbeat"
    else:
        telemetry = {sensor_id: payload["value"]}
        prefix = sensor_id
    for field in ("sequence", "uptime_ms", "quality_flags"):
        if field in payload:
            telemetry[f"{prefix}_{field}"] = payload[field]
    return pod_id, attributes, telemetry


def _safe_token(value, name):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_-]+", value):
        raise ValueError(f"{name} must be a safe token")
    return value


def control_projection(payload):
    """Split versioned control events into TB attributes and telemetry."""
    if not isinstance(payload, dict) or payload.get("schema_version") != CONTROL_SCHEMA_VERSION:
        raise ValueError("invalid control-event schema")
    event_kind = _safe_token(payload.get("event_kind"), "event_kind")
    pod_id = _safe_token(payload.get("pod_id"), "pod_id")
    sensor_id = _safe_token(payload.get("sensor_id"), "sensor_id")
    transition = _safe_token(payload.get("transition"), "transition")
    reason = _safe_token(payload.get("reason"), "reason")
    instance_id = _integer(payload.get("instance_id", 0), "instance_id")
    timestamp_ms = _integer(payload.get("timestamp_ms", 0), "timestamp_ms")
    attributes = {}
    telemetry = {}

    if event_kind == "capabilities":
        mask = _integer(payload.get("capability_mask", 0), "capability_mask",
                        maximum=(1 << 32) - 1)
        attributes.update(
            control_contract_version=CONTROL_SCHEMA_VERSION,
            capability_mask=mask,
            capabilities=[name for bit, name in enumerate(CAPABILITY_ORDER)
                          if mask & (1 << bit)],
            shared_alarm_indicator=bool(
                payload.get("shared_alarm_indicator_verified", False)),
        )
        telemetry["ruleset_revision"] = _integer(
            payload.get("ruleset_revision", 0), "ruleset_revision",
            maximum=(1 << 32) - 1)
    elif event_kind == "configuration":
        capability = _safe_token(payload.get("capability"), "capability")
        rule_kind = _safe_token(payload.get("rule_kind"), "rule_kind")
        prefix = f"rule_{capability}_{rule_kind}"
        attributes[f"{prefix}_value"] = _finite_number(payload.get("threshold"),
                                                         "threshold")
        telemetry[f"{prefix}_revision"] = _integer(
            payload.get("config_revision", 0), "config_revision",
            maximum=(1 << 32) - 1)
        telemetry["ruleset_revision"] = _integer(
            payload.get("ruleset_revision", 0), "ruleset_revision",
            maximum=(1 << 32) - 1)
        telemetry[f"{prefix}_changed"] = transition == "updated"
    elif event_kind in {"alarm_condition", "alarm_acknowledgement"}:
        if instance_id == 0:
            raise ValueError("alarm event requires instance_id")
        capability = _safe_token(payload.get("capability"), "capability")
        rule_kind = _safe_token(payload.get("rule_kind"), "rule_kind")
        prefix = f"alarm_{capability}_{rule_kind}"
        telemetry[f"{prefix}_instance_id"] = instance_id
        telemetry[f"{prefix}_sensor_id"] = sensor_id
        telemetry[f"{prefix}_threshold"] = _finite_number(
            payload.get("threshold", 0.0), "threshold")
        telemetry[f"{prefix}_active"] = bool(payload.get("active", False))
        telemetry[f"{prefix}_acknowledged"] = bool(
            payload.get("acknowledged", False))
        telemetry[f"{prefix}_observed"] = _finite_number(
            payload.get("observed", 0.0), "observed")
        telemetry[f"{prefix}_quality_flags"] = _integer(
            payload.get("quality_flags", 0), "quality_flags",
            maximum=(1 << 32) - 1)
        telemetry[f"{prefix}_transition"] = transition
        telemetry[f"{prefix}_reason"] = reason
    elif event_kind == "alarm_silence":
        telemetry.update(
            alarm_silenced=bool(payload.get("silenced", False)),
            silenced_instance_id=instance_id,
            alarm_silence_transition=transition,
            alarm_silence_reason=reason,
        )
    elif event_kind == "gateway_incident":
        prefix = f"incident_{pod_id}_{sensor_id}"
        attributes[f"{prefix}_source_pod"] = pod_id
        attributes[f"{prefix}_kind"] = sensor_id
        telemetry[f"{prefix}_instance_id"] = instance_id
        telemetry[f"{prefix}_active"] = bool(payload.get("active", False))
        telemetry[f"{prefix}_transition"] = transition
        telemetry[f"{prefix}_reason"] = reason
        telemetry[f"{prefix}_evidence_count"] = _integer(
            payload.get("evidence_count", 0), "evidence_count", maximum=255)
        telemetry[f"{prefix}_primary"] = _finite_number(
            payload.get("observed", 0.0), "observed")
        telemetry[f"{prefix}_secondary"] = _finite_number(
            payload.get("secondary_observed", 0.0), "secondary_observed")
        telemetry[f"{prefix}_trend"] = _finite_number(
            payload.get("trend", 0.0), "trend")
        pod_id = "GATEWAY_1"
    else:
        raise ValueError("unknown control-event kind")
    telemetry["control_timestamp_ms"] = timestamp_ms
    return pod_id, attributes, telemetry


class RpcCommandBridge:
    """Correlates one ThingsBoard RPC with one terminal downstream result."""

    def __init__(self, publish_command, send_rpc_reply, clock_ms=None,
                 completed_capacity=256, retry_interval_ms=1000):
        self.publish_command = publish_command
        self.send_rpc_reply = send_rpc_reply
        self.clock_ms = clock_ms or (lambda: int(time.time() * 1000))
        self.completed_capacity = completed_capacity
        self.retry_interval_ms = retry_interval_ms
        self.pending = {}
        self.pending_by_command = {}
        self.completed = OrderedDict()
        self.completed_commands = {}
        self._command_clock_ms = 0
        self._command_sequence = 0

    def _next_command_id(self, now_ms):
        if now_ms > self._command_clock_ms:
            self._command_clock_ms = now_ms
            self._command_sequence = 1
        else:
            self._command_sequence += 1
            if self._command_sequence >= 1024:
                self._command_clock_ms += 1
                self._command_sequence = 1
        command_id = self._command_clock_ms * 1024 + self._command_sequence
        if command_id > MAX_EXACT_JSON_INTEGER:
            raise InvalidRpc("command id generator exhausted exact JSON range")
        return command_id

    def _deliver(self, key, now_ms):
        completed = self.completed[key]
        if completed.delivered or now_ms < completed.next_attempt_ms:
            return
        pod_id, request_id = key
        try:
            self.send_rpc_reply(pod_id, request_id, completed.response)
        except Exception:
            completed.next_attempt_ms = now_ms + self.retry_interval_ms
        else:
            completed.delivered = True

    def _complete(self, key, response, now_ms):
        pending = self.pending.pop(key, None)
        if pending is not None:
            self.pending_by_command.pop(pending.command_id, None)
            self.completed_commands[pending.command_id] = key
        self.completed[key] = CompletedRequest(response=response)
        self.completed.move_to_end(key)
        while len(self.completed) > self.completed_capacity:
            evicted_key, evicted = self.completed.popitem(last=False)
            for command_id, command_key in list(self.completed_commands.items()):
                if command_key == evicted_key:
                    self.completed_commands.pop(command_id, None)
        self._deliver(key, now_ms)

    def handle_rpc(self, content, now_ms=None):
        now_ms = self.clock_ms() if now_ms is None else now_ms
        correlation = None
        if isinstance(content, dict) and isinstance(content.get("data"), dict):
            pod_id = content.get("device")
            request_id = content["data"].get("id")
            if isinstance(pod_id, str) and isinstance(request_id, int) and not isinstance(request_id, bool):
                correlation = (pod_id, request_id)
                if correlation in self.pending or correlation in self.completed:
                    return "duplicate"
        try:
            pod_id, request_id, command = command_from_rpc(content, now_ms)
        except InvalidRpc:
            if correlation is not None:
                response = terminal_response(correlation[0], correlation[1], "rejected",
                                             "invalid_syntax", now_ms)
                self._complete(correlation, response, now_ms)
            return "rejected"

        key = (pod_id, request_id)
        try:
            command_id = self._next_command_id(now_ms)
        except InvalidRpc:
            response = terminal_response(pod_id, request_id, "failed",
                                         "transport_failed", now_ms)
            self._complete(key, response, now_ms)
            return "failed"
        command["command_id"] = command_id
        self.pending[key] = PendingRequest(
            pod_id, request_id, command_id,
            now_ms + command["valid_for_ms"], command["command_type"],
            command.get("alarm_instance_id", 0))
        self.pending_by_command[command_id] = key
        topic = COMMAND_TOPIC.format(pod_id=pod_id)
        try:
            published = self.publish_command(topic, json.dumps(command, separators=(",", ":")))
        except Exception:
            published = False
        if key not in self.pending:
            return "completed"
        if published is False:
            response = terminal_response(pod_id, request_id, "failed",
                                         "transport_failed", now_ms)
            self._complete(key, response, now_ms)
            return "failed"
        return "pending"

    def handle_command_result(self, payload, now_ms=None):
        now_ms = self.clock_ms() if now_ms is None else now_ms
        if not isinstance(payload, dict):
            return "invalid"
        pod_id = payload.get("pod_id")
        command_id = payload.get("command_id")
        status = payload.get("status")
        reason = payload.get("reason")
        if (payload.get("schema_version") not in {2, COMMAND_SCHEMA_VERSION} or
                not isinstance(pod_id, str) or isinstance(command_id, bool) or
                not isinstance(command_id, int) or status not in TERMINAL_STATUSES or
                not isinstance(reason, str)):
            return "invalid"
        key = self.pending_by_command.get(command_id)
        if key is None:
            return "late" if command_id in self.completed_commands else "uncorrelated"
        if key[0] != pod_id:
            return "uncorrelated"
        pending = self.pending[key]
        response = dict(payload)
        response["schema_version"] = COMMAND_SCHEMA_VERSION
        if pending.method == "get_capabilities" and status == "executed":
            mask = response.get("capability_mask", 0)
            if isinstance(mask, int) and not isinstance(mask, bool):
                response["shared_alarm_indicator"] = bool(
                    mask & SHARED_ALARM_INDICATOR_BIT)
                sensor_mask = mask & ~SHARED_ALARM_INDICATOR_BIT
                response["capability_mask"] = sensor_mask
                response["capabilities"] = [
                    name for bit, name in enumerate(CAPABILITY_ORDER)
                    if sensor_mask & (1 << bit)
                ]
        if pending.alarm_instance_id:
            response["alarm_instance_id"] = pending.alarm_instance_id
        self._complete(key, response, now_ms)
        return "completed"

    def tick(self, now_ms=None):
        now_ms = self.clock_ms() if now_ms is None else now_ms
        for key, pending in list(self.pending.items()):
            if now_ms >= pending.deadline_ms:
                response = terminal_response(pending.pod_id, pending.request_id,
                                             "failed", "timeout", now_ms)
                response["command_id"] = pending.command_id
                self._complete(key, response, now_ms)
        for key in list(self.completed):
            self._deliver(key, now_ms)
