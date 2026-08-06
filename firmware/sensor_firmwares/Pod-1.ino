#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_SGP40.h>
#include <SensirionI2cScd4x.h>

#define I2C_SDA 6
#define I2C_SCL 7
#define CO2_LED_PIN 4
#define BUZZER_PIN 5

#define CO2_THRESHOLD_PPM 1000
#define BUZZER_FREQUENCY 1000
#define ALARM_INTERVAL_MS 1000
#define DISPLAY_INTERVAL_MS 1000

Adafruit_SHT4x sht41;
Adafruit_SGP40 sgp40;
SensirionI2cScd4x scd41;

bool sht41Connected = false;
bool sgp40Connected = false;
bool scd41Connected = false;

uint16_t co2Ppm = 0;
bool co2DataValid = false;

bool alarmOutputOn = false;
uint32_t lastAlarmToggleTime = 0;

bool alarmActive() {
  return scd41Connected &&
         co2DataValid &&
         co2Ppm > CO2_THRESHOLD_PPM;
}

void setAlarmOutput(bool on) {
  alarmOutputOn = on;
  digitalWrite(CO2_LED_PIN, on ? HIGH : LOW);
  ledcWriteTone(BUZZER_PIN, on ? BUZZER_FREQUENCY : 0);
}

void updateAlarm() {
  if (!alarmActive()) {
    setAlarmOutput(false);
    return;
  }

  uint32_t now = millis();

  if (now - lastAlarmToggleTime >= ALARM_INTERVAL_MS) {
    lastAlarmToggleTime = now;
    setAlarmOutput(!alarmOutputOn);
  }
}

bool initialiseSCD41() {
  scd41.begin(Wire, SCD41_I2C_ADDR_62);

  delay(30);
  scd41.wakeUp();
  delay(30);
  scd41.stopPeriodicMeasurement();
  delay(500);
  scd41.reinit();
  delay(30);

  return scd41.startPeriodicMeasurement() == 0;
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(CO2_LED_PIN, OUTPUT);
  digitalWrite(CO2_LED_PIN, LOW);

  ledcAttach(BUZZER_PIN, BUZZER_FREQUENCY, 8);
  setAlarmOutput(false);

  if (!Wire.begin(I2C_SDA, I2C_SCL)) {
    Serial.println("I2C ERROR");
    while (true) {
      delay(1000);
    }
  }

  Wire.setClock(100000);

  sht41Connected = sht41.begin(&Wire);
  if (sht41Connected) {
    sht41.setPrecision(SHT4X_HIGH_PRECISION);
    sht41.setHeater(SHT4X_NO_HEATER);
  }

  sgp40Connected = sgp40.begin(&Wire);
  scd41Connected = initialiseSCD41();

  Serial.println("SiteTwin Pod 1 started");
  Serial.println();
}

void loop() {
  static uint32_t lastDisplayTime = 0;

  updateAlarm();

  if (millis() - lastDisplayTime < DISPLAY_INTERVAL_MS) {
    return;
  }
  lastDisplayTime = millis();

  // Read SHT41
  bool sht41ReadOK = false;
  float temperature = 25.0;
  float humidity = 50.0;

  if (sht41Connected) {
    sensors_event_t humidityEvent;
    sensors_event_t temperatureEvent;

    sht41ReadOK = sht41.getEvent(&humidityEvent, &temperatureEvent);

    if (sht41ReadOK) {
      temperature = temperatureEvent.temperature;
      humidity = humidityEvent.relative_humidity;
    }
  }

  // Read SCD41. Temperature and humidity are required by
  // the library, but are not stored or displayed.
  if (scd41Connected) {
    bool dataReady = false;

    if (scd41.getDataReadyStatus(dataReady) == 0 && dataReady) {
      uint16_t newCo2Ppm = 0;
      float unusedTemperature = 0.0;
      float unusedHumidity = 0.0;

      int16_t error = scd41.readMeasurement(
          newCo2Ppm,
          unusedTemperature,
          unusedHumidity);

      if (error == 0 && newCo2Ppm > 0) {
        bool wasNormal = !co2DataValid ||
                         co2Ppm <= CO2_THRESHOLD_PPM;

        co2Ppm = newCo2Ppm;
        co2DataValid = true;

        // When CO2 first exceeds the threshold,
        // begin with one second of silence.
        if (wasNormal && alarmActive()) {
          setAlarmOutput(false);
          lastAlarmToggleTime = millis();
        }
      }
    }
  }

  updateAlarm();

  // Read SGP40 using SHT41 compensation when available.
  int32_t vocIndex = -1;
  if (sgp40Connected) {
    vocIndex = sgp40.measureVocIndex(temperature, humidity);
  }

  Serial.println("------------- POD 1 DATA -------------");

  Serial.print("SHT41     | ");
  if (!sht41Connected) {
    Serial.println("OFFLINE");
  } else if (!sht41ReadOK) {
    Serial.println("READ ERROR");
  } else {
    Serial.print("Temperature: ");
    Serial.print(temperature, 2);
    Serial.print(" C | Humidity: ");
    Serial.print(humidity, 2);
    Serial.println(" %RH");
  }

  Serial.print("SCD41     | ");
  if (!scd41Connected) {
    Serial.println("OFFLINE");
  } else if (!co2DataValid) {
    Serial.println("WAITING");
  } else {
    Serial.print("CO2: ");
    Serial.print(co2Ppm);
    Serial.println(" ppm");
  }

  Serial.print("SGP40     | ");
  if (!sgp40Connected) {
    Serial.println("OFFLINE");
  } else if (vocIndex < 0) {
    Serial.println("READ ERROR");
  } else {
    Serial.print("VOC Index: ");
    Serial.println(vocIndex);
  }

  Serial.print("CO2 Alarm | ");
  Serial.println(alarmActive() ? "ON" : "OFF");

  Serial.println("--------------------------------------");
  Serial.println();
}