# Copy this file to config.py and fill in real values.
# config.py is gitignored and must never be committed.

# ---------- HiveMQ (upstream) ----------
HIVEMQ_HOST = "your-cluster-id.s1.eu.hivemq.cloud"
HIVEMQ_PORT = 8883
HIVEMQ_USERNAME = "your-username"
HIVEMQ_PASSWORD = "your-password"
HIVEMQ_TOPIC = "sitetwin/pods/+/telemetry"

# ---------- ThingsBoard (downstream, local) ----------
TB_HOST = "localhost"
TB_PORT = 1883
TB_GATEWAY_TOKEN = "your-gateway-access-token"