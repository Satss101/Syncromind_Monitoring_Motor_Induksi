#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

//Definisi Pin
#define LED_SYSTEM 15
#define DHTPIN 17
#define DHTTYPE DHT22

// Tegangan antar fasa (simulasi pot sebagai ZMPT)
const uint8_t PIN_V_RS = 36;
const uint8_t PIN_V_ST = 39;
const uint8_t PIN_V_TR = 34;

// Arus tiap fasa (simulasi pot sebagai ACS)
const uint8_t PIN_I_R  = 33;
const uint8_t PIN_I_S  = 32;
const uint8_t PIN_I_T  = 35;

// MPU6050 I2C
const uint8_t I2C_SDA_PIN = 21;
const uint8_t I2C_SCL_PIN = 22;

// WIFI & HTTP
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";
const char* SERVER_URL    = "http://GANTI_SERVER_URL/api/motor";

// Simulation
const float ADC_MAX            = 4095.0f;
const float SIM_MAX_VOLTAGE_LL = 450.0f; // line-to-line voltage max
const float SIM_MAX_CURRENT    = 10.0f;  // phase current max

// Interval Kirim Data
const unsigned long READ_INTERVAL_MS = 200;
const unsigned long SEND_INTERVAL_MS = 300;
const uint8_t ANALOG_SAMPLES = 5;

// Sensor Object
DHT dht(DHTPIN, DHTTYPE);
Adafruit_MPU6050 mpu;
bool mpuReady = false;

//Data Struct
struct LineData {
  uint16_t rawVoltage;
  uint16_t rawCurrent;

  float voltageLL;
  float current;
};

LineData lineRS, lineST, lineTR;

float temperatureC = NAN;
float humidityPct  = NAN;

float accX = 0.0f, accY = 0.0f, accZ = 0.0f;
float gyroX = 0.0f, gyroY = 0.0f, gyroZ = 0.0f;
float vibrationValue = 0.0f;

unsigned long lastReadTime = 0;
unsigned long lastSendTime = 0;

// Helper
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float clampFloat(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

String jsonNumberOrNull(float value, unsigned int decimals = 2) {
  if (isnan(value)) return "null";
  return String(value, decimals);
}

uint16_t readAnalogAverage(uint8_t pin, uint8_t samples = ANALOG_SAMPLES) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delayMicroseconds(100);
  }
  return (uint16_t)(sum / samples);
}

LineData readLineData(uint8_t pinVoltage, uint8_t pinCurrent) {
  LineData d;

  d.rawVoltage = readAnalogAverage(pinVoltage);
  d.rawCurrent = readAnalogAverage(pinCurrent);

  d.voltageLL = mapFloat((float)d.rawVoltage, 0.0f, ADC_MAX, 0.0f, SIM_MAX_VOLTAGE_LL);
  d.current   = mapFloat((float)d.rawCurrent, 0.0f, ADC_MAX, 0.0f, SIM_MAX_CURRENT);

  d.voltageLL = clampFloat(d.voltageLL, 0.0f, SIM_MAX_VOLTAGE_LL);
  d.current   = clampFloat(d.current, 0.0f, SIM_MAX_CURRENT);

  return d;
}

// WIFI
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting WiFi");
  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 20000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection failed");
  }
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("WiFi disconnected, reconnecting...");
  WiFi.disconnect(true, true);
  delay(1000);
  connectWiFi();
}

// MPU6050
void initMPU6050() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  if (!mpu.begin()) {
    Serial.println("MPU6050 tidak terdeteksi");
    mpuReady = false;
    return;
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  mpuReady = true;
  Serial.println("MPU6050 siap");
}

// Baca Sensor
void readAllSensors() {
  
  //Fasa
  lineRS = readLineData(PIN_V_RS, PIN_I_R);
  lineST = readLineData(PIN_V_ST, PIN_I_S);
  lineTR = readLineData(PIN_V_TR, PIN_I_T);
  
  // DHT22
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperatureC = t;
  if (!isnan(h)) humidityPct = h;

  // MPU6050
  if (mpuReady) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    accX = a.acceleration.x;
    accY = a.acceleration.y;
    accZ = a.acceleration.z;

    gyroX = g.gyro.x;
    gyroY = g.gyro.y;
    gyroZ = g.gyro.z;

    float accMagnitude = sqrt((accX * accX) + (accY * accY) + (accZ * accZ));
    vibrationValue = fabs(accMagnitude - 9.81f);
  } else {
    accX = accY = accZ = 0.0f;
    gyroX = gyroY = gyroZ = 0.0f;
    vibrationValue = 0.0f;
  }
}

// Serial
void printToSerial() {
  Serial.println("\n================ REALTIME MOTOR DATA ================");
  Serial.printf("V_RS : %.2f V | I_R : %.2f A | rawV:%u rawI:%u\n", lineRS.voltageLL, lineRS.current, lineRS.rawVoltage, lineRS.rawCurrent);
  Serial.printf("V_ST : %.2f V | I_S : %.2f A | rawV:%u rawI:%u\n", lineST.voltageLL, lineST.current, lineST.rawVoltage, lineST.rawCurrent);
  Serial.printf("V_TR : %.2f V | I_T : %.2f A | rawV:%u rawI:%u\n", lineTR.voltageLL, lineTR.current, lineTR.rawVoltage, lineTR.rawCurrent);

  Serial.printf("Temperature : %s C\n", jsonNumberOrNull(temperatureC, 2).c_str());
  Serial.printf("Humidity    : %s %%\n", jsonNumberOrNull(humidityPct, 2).c_str());

  Serial.printf("Accel       : X=%.2f Y=%.2f Z=%.2f m/s^2\n", accX, accY, accZ);
  Serial.printf("Gyro        : X=%.2f Y=%.2f Z=%.2f rad/s\n", gyroX, gyroY, gyroZ);
  Serial.printf("Vibration   : %.2f m/s^2\n", vibrationValue);
  Serial.println("====================================================");
}

// Build JSON
String buildJsonPayload() {
  String payload;
  payload.reserve(1200);

  payload += "{";
  payload += "\"device_id\":\"esp32-motor-monitor\",";
  payload += "\"timestamp_ms\":" + String(millis()) + ",";

  payload += "\"temperature_c\":" + jsonNumberOrNull(temperatureC, 2) + ",";
  payload += "\"humidity_pct\":" + jsonNumberOrNull(humidityPct, 2) + ",";
  payload += "\"vibration_ms2\":" + String(vibrationValue, 2) + ",";

  payload += "\"line_rs\":{";
  payload += "\"raw_voltage\":" + String(lineRS.rawVoltage) + ",";
  payload += "\"raw_current\":" + String(lineRS.rawCurrent) + ",";
  payload += "\"voltage_rs_v\":" + String(lineRS.voltageLL, 2) + ",";
  payload += "\"current_r_a\":" + String(lineRS.current, 2);
  payload += "},";

  payload += "\"line_st\":{";
  payload += "\"raw_voltage\":" + String(lineST.rawVoltage) + ",";
  payload += "\"raw_current\":" + String(lineST.rawCurrent) + ",";
  payload += "\"voltage_st_v\":" + String(lineST.voltageLL, 2) + ",";
  payload += "\"current_s_a\":" + String(lineST.current, 2);
  payload += "},";

  payload += "\"line_tr\":{";
  payload += "\"raw_voltage\":" + String(lineTR.rawVoltage) + ",";
  payload += "\"raw_current\":" + String(lineTR.rawCurrent) + ",";
  payload += "\"voltage_tr_v\":" + String(lineTR.voltageLL, 2) + ",";
  payload += "\"current_t_a\":" + String(lineTR.current, 2);
  payload += "},";

  payload += "\"mpu6050\":{";
  payload += "\"acc_x\":" + String(accX, 2) + ",";
  payload += "\"acc_y\":" + String(accY, 2) + ",";
  payload += "\"acc_z\":" + String(accZ, 2) + ",";
  payload += "\"gyro_x\":" + String(gyroX, 2) + ",";
  payload += "\"gyro_y\":" + String(gyroY, 2) + ",";
  payload += "\"gyro_z\":" + String(gyroZ, 2);
  payload += "}";

  payload += "}";

  return payload;
}

// Kirim HTTP
void sendDataToServer() {
  ensureWiFi();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Skip send, WiFi belum connect");
    return;
  }

  String payload = buildJsonPayload();

  HTTPClient http;
  http.setTimeout(2000);
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(payload);

  Serial.printf("[HTTP] POST code: %d\n", httpCode);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("[HTTP] Response:");
    Serial.println(response);
  } else {
    Serial.print("[HTTP] Error: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// Setup & Loop
void setup_sensor() {
  Serial.begin(115200);
  delay(500);

  analogReadResolution(12);

  pinMode(LED_SYSTEM, OUTPUT);
  digitalWrite(LED_SYSTEM, HIGH); // indikator sistem hidup

  pinMode(PIN_V_RS, INPUT);
  pinMode(PIN_I_R, INPUT);
  pinMode(PIN_V_ST, INPUT);
  pinMode(PIN_I_S, INPUT);
  pinMode(PIN_V_TR, INPUT);
  pinMode(PIN_I_T, INPUT);

  dht.begin();
  initMPU6050();
  connectWiFi();

  Serial.println("System ready...");
}

void loop_data() {
  unsigned long now = millis();

  if (now - lastReadTime >= READ_INTERVAL_MS) {
    lastReadTime = now;
    readAllSensors();
    printToSerial();
  }

  if (now - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = now;
    sendDataToServer();
  }

  delay(5);
}