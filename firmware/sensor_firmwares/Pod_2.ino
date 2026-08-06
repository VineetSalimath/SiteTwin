#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>

// =====================================================
// Pod 2 pins
// =====================================================
#define I2C_SDA 6
#define I2C_SCL 7

#define DOOR_SENSOR_PIN 0
#define PIR_SENSOR_PIN 1

#define LED_PIN 4
#define BUZZER_PIN 5

// =====================================================
// Settings
// =====================================================
#define BUZZER_FREQUENCY 2000
#define ALARM_INTERVAL_MS 1000
#define DISPLAY_INTERVAL_MS 1000
#define DOOR_DEBOUNCE_MS 30

// =====================================================
// Sensor
// =====================================================
BH1750 lightMeter;
bool bh1750Connected = false;

// =====================================================
// Sensor states
// LOW = door closed, HIGH = door open
// =====================================================
int doorState = HIGH;
int lastRawDoorState = HIGH;
int pirState = LOW;

// =====================================================
// Timing and alarm states
// =====================================================
uint32_t doorChangeTime = 0;
uint32_t lastAlarmToggleTime = 0;
uint32_t lastDisplayTime = 0;

bool alarmOutputOn = false;

// =====================================================
// Door open means alarm active
// =====================================================
bool alarmActive() {
  return doorState == HIGH;
}

// =====================================================
// Control LED and buzzer together
// =====================================================
void setAlarmOutput(bool on) {
  alarmOutputOn = on;

  digitalWrite(LED_PIN, on ? HIGH : LOW);
  ledcWriteTone(
      BUZZER_PIN,
      on ? BUZZER_FREQUENCY : 0);
}

// =====================================================
// Door sensor debounce
// =====================================================
void updateDoorState(uint32_t now) {
  int rawDoorState = digitalRead(DOOR_SENSOR_PIN);

  if (rawDoorState != lastRawDoorState) {
    lastRawDoorState = rawDoorState;
    doorChangeTime = now;
  }

  if ((now - doorChangeTime >= DOOR_DEBOUNCE_MS) &&
      (rawDoorState != doorState)) {
    doorState = rawDoorState;

    // After a state change, turn alarm off first.
    setAlarmOutput(false);
    lastAlarmToggleTime = now;
  }
}

// =====================================================
// Update door alarm
// Door open: 1 second OFF, 1 second ON
// Door closed: alarm OFF
// =====================================================
void updateAlarm(uint32_t now) {
  if (!alarmActive()) {
    setAlarmOutput(false);
    return;
  }

  if (now - lastAlarmToggleTime >= ALARM_INTERVAL_MS) {
    lastAlarmToggleTime = now;
    setAlarmOutput(!alarmOutputOn);
  }
}

// =====================================================
// Setup
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);
  pinMode(PIR_SENSOR_PIN, INPUT);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  ledcAttach(BUZZER_PIN, BUZZER_FREQUENCY, 8);
  setAlarmOutput(false);

  doorState = digitalRead(DOOR_SENSOR_PIN);
  lastRawDoorState = doorState;

  // Allow the PIR sensor to stabilise.
  delay(5000);
  pirState = digitalRead(PIR_SENSOR_PIN);

  if (!Wire.begin(I2C_SDA, I2C_SCL)) {
    Serial.println("I2C ERROR");

    while (true) {
      setAlarmOutput(false);
      delay(1000);
    }
  }

  Wire.setClock(100000);
  Wire.setTimeOut(1000);

  bh1750Connected = lightMeter.begin(
      BH1750::CONTINUOUS_HIGH_RES_MODE,
      0x23,
      &Wire);

  lastAlarmToggleTime = millis();
  lastDisplayTime = millis();

  Serial.println("SiteTwin Pod 2 started");
}

// =====================================================
// Loop
// =====================================================
void loop() {
  uint32_t now = millis();

  updateDoorState(now);
  updateAlarm(now);

  pirState = digitalRead(PIR_SENSOR_PIN);

  if (now - lastDisplayTime < DISPLAY_INTERVAL_MS) {
    return;
  }

  lastDisplayTime = now;

  Serial.println();
  Serial.println("------------- POD 2 DATA -------------");

  // BH1750
  Serial.print("BH1750      | ");

  if (!bh1750Connected) {
    Serial.println("OFFLINE");
  } else {
    float lux = lightMeter.readLightLevel();

    if (lux < 0) {
      Serial.println("READ ERROR");
    } else {
      Serial.print("Light: ");
      Serial.print(lux, 2);
      Serial.println(" lx");
    }
  }

  // Door sensor
  Serial.print("Door sensor | ");
  Serial.println(
      doorState == LOW ? "CLOSED" : "OPEN");

  // PIR sensor
  Serial.print("SR505 PIR   | ");
  Serial.println(
      pirState == HIGH ? "MOTION" : "NO MOTION");

  // Alarm state
  Serial.print("Door Alarm  | ");
  Serial.println(alarmActive() ? "ON" : "OFF");

  Serial.println("--------------------------------------");
}