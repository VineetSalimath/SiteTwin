#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_INA219.h>
#include <Adafruit_ADXL345_U.h>
#include <Adafruit_Sensor.h>
#include <math.h>

// =====================================================
// Pod 3 pins
// =====================================================
#define DS18B20_PIN 0

#define LED_PIN 4
#define BUZZER_PIN 5

#define I2C_SDA 6
#define I2C_SCL 7

// =====================================================
// Settings
// =====================================================
#define TEMPERATURE_THRESHOLD_C 31.0
#define MOTION_THRESHOLD_MS2 5.0

#define BUZZER_FREQUENCY 2000
#define ALARM_INTERVAL_MS 1000
#define DISPLAY_INTERVAL_MS 1000

// =====================================================
// Sensors
// =====================================================
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);

Adafruit_INA219 ina219;
Adafruit_ADXL345_Unified adxl345(12345);

// =====================================================
// Sensor status
// =====================================================
bool ds18b20Connected = false;
bool ina219Connected = false;
bool adxl345Connected = false;

// =====================================================
// Latest measurements
// =====================================================
float temperatureC = 0.0;

float busVoltageV = 0.0;
float currentmA = 0.0;
float powermW = 0.0;

float accelerationX = 0.0;
float accelerationY = 0.0;
float accelerationZ = 0.0;

float totalAcceleration = 0.0;
float motionAcceleration = 0.0;

// =====================================================
// Alarm and timing
// =====================================================
bool temperatureAlarm = false;
bool motionAlarm = false;
bool alarmOutputOn = false;

uint32_t lastAlarmToggleTime = 0;
uint32_t lastDisplayTime = 0;

// =====================================================
// Any active alarm
// =====================================================
bool alarmActive() {
  return temperatureAlarm || motionAlarm;
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
// Update alarm output
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
// Read DS18B20
// =====================================================
void readTemperature() {
  if (!ds18b20Connected) {
    temperatureAlarm = false;
    return;
  }

  ds18b20.requestTemperatures();

  float newTemperature =
      ds18b20.getTempCByIndex(0);

  if (newTemperature == DEVICE_DISCONNECTED_C) {
    ds18b20Connected = false;
    temperatureAlarm = false;
    return;
  }

  temperatureC = newTemperature;
  temperatureAlarm =
      temperatureC > TEMPERATURE_THRESHOLD_C;
}

// =====================================================
// Read INA219
// =====================================================
void readINA219() {
  if (!ina219Connected) {
    return;
  }

  busVoltageV = ina219.getBusVoltage_V();
  currentmA = ina219.getCurrent_mA();
  powermW = ina219.getPower_mW();
}

// =====================================================
// Read ADXL345
// =====================================================
void readADXL345() {
  if (!adxl345Connected) {
    motionAlarm = false;
    return;
  }

  sensors_event_t event;
  adxl345.getEvent(&event);

  accelerationX = event.acceleration.x;
  accelerationY = event.acceleration.y;
  accelerationZ = event.acceleration.z;

  totalAcceleration = sqrt(
      accelerationX * accelerationX +
      accelerationY * accelerationY +
      accelerationZ * accelerationZ);

  motionAcceleration =
      fabs(totalAcceleration - 9.81);

  motionAlarm =
      motionAcceleration > MOTION_THRESHOLD_MS2;
}

// =====================================================
// Setup
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  ledcAttach(BUZZER_PIN, BUZZER_FREQUENCY, 8);
  setAlarmOutput(false);

  if (!Wire.begin(I2C_SDA, I2C_SCL)) {
    Serial.println("I2C ERROR");

    while (true) {
      setAlarmOutput(false);
      delay(1000);
    }
  }

  Wire.setClock(100000);
  Wire.setTimeOut(1000);

  ds18b20.begin();
  ds18b20Connected =
      ds18b20.getDeviceCount() > 0;

  if (ds18b20Connected) {
    ds18b20.setResolution(12);
  }

  ina219Connected = ina219.begin(&Wire);

  adxl345Connected = adxl345.begin(0x53);

  if (adxl345Connected) {
    adxl345.setRange(ADXL345_RANGE_16_G);
  }

  lastAlarmToggleTime = millis();
  lastDisplayTime = millis();

  Serial.println("SiteTwin Pod 3 started");
}

// =====================================================
// Loop
// =====================================================
void loop() {
  uint32_t now = millis();

  updateAlarm(now);

  if (now - lastDisplayTime < DISPLAY_INTERVAL_MS) {
    return;
  }

  lastDisplayTime = now;

  bool wasAlarmActive = alarmActive();

  readTemperature();
  readINA219();
  readADXL345();

  if (!wasAlarmActive && alarmActive()) {
    setAlarmOutput(false);
    lastAlarmToggleTime = now;
  }

  updateAlarm(now);

  Serial.println();
  Serial.println("------------- POD 3 DATA -------------");

  // DS18B20
  Serial.print("DS18B20 | ");

  if (!ds18b20Connected) {
    Serial.println("OFFLINE");
  } else {
    Serial.print("Temperature: ");
    Serial.print(temperatureC, 2);
    Serial.println(" C");
  }

  // INA219
  Serial.print("INA219   | ");

  if (!ina219Connected) {
    Serial.println("OFFLINE");
  } else {
    Serial.print("Voltage: ");
    Serial.print(busVoltageV, 3);
    Serial.print(" V | Current: ");
    Serial.print(currentmA, 2);
    Serial.print(" mA | Power: ");
    Serial.print(powermW, 2);
    Serial.println(" mW");
  }

  // ADXL345
  Serial.print("ADXL345  | ");

  if (!adxl345Connected) {
    Serial.println("OFFLINE");
  } else {
    Serial.print("X: ");
    Serial.print(accelerationX, 2);

    Serial.print(" | Y: ");
    Serial.print(accelerationY, 2);

    Serial.print(" | Z: ");
    Serial.print(accelerationZ, 2);

    Serial.print(" m/s2 | Motion: ");
    Serial.print(motionAcceleration, 2);
    Serial.println(" m/s2");
  }

  // Alarm
  Serial.print("Alarm    | ");
  Serial.println(alarmActive() ? "ON" : "OFF");

  Serial.println("--------------------------------------");
}