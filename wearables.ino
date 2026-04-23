#include <bluefruit.h>
#include <Adafruit_SPIFlash.h>
#include <Adafruit_TinyUSB.h>
#include <Wire.h>
#include <LSM6DS3.h>
#include "HX711.h"
#include "MAX30105.h"

Adafruit_FlashTransport_QSPI flashTransport;

// Struct to hold Red and IR values
struct SensorData {
  long Red;
  long IR;
};

// IMU — changed int16_t to float to match readFloat functions
struct IMUData {
  float aX, aY, aZ;
  float gX, gY, gZ;
};

LSM6DS3 myIMU(I2C_MODE, 0x6A);

// LOADCELL
#define DATA_PIN 7
#define CLOCK_PIN 8
HX711 scale;

// SPO2
MAX30105 particleSensor;
uint16_t Red;
uint16_t IR;
unsigned long nowMicros = 0;
unsigned long lastMicros = 0;
const int MAX_SAMPLING_FREQ = 100;
unsigned long MINIMUM_SAMPLING_DELAY_uSec = (unsigned long)(1 * 1000000 / MAX_SAMPLING_FREQ);

bool deviceConnected = false;

////BLE GATT Profile////
BLEService customService = BLEService("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
BLECharacteristic imuChar = BLECharacteristic(0x27A8);
BLECharacteristic ecgChar = BLECharacteristic(0x2A37);
BLECharacteristic loadChar = BLECharacteristic(0x2A98);
BLECharacteristic spo2Char = BLECharacteristic(0x2A8D);

// Add at top
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 20; // 50Hz is plenty, was effectively ~100Hz with blocking sensors

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("IN SETUP");

  NRF_POWER->DCDCEN = 1;

  flashTransport.begin();
  flashTransport.runCommand(0xB9);
  delayMicroseconds(5);
  flashTransport.end();

  Wire.begin();

  // ECG pins
  pinMode(A0, INPUT);
  pinMode(A1, INPUT);
  pinMode(A2, INPUT);

  // Loadcell init
  scale.begin(DATA_PIN, CLOCK_PIN);
  scale.set_scale(121.16);
  scale.tare();
  Serial.println("Loadcell OK");

  // MAX30105 init FIRST (it changes I2C bus speed)
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30105 ERROR - check wiring!");
  } else {
    byte ledBrightness = 40;
    byte sampleAverage = 1;
    byte ledMode = 2;
    int sampleRate = 3200;
    int pulseWidth = 69;
    int adcRange = 4096;
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
    Serial.println("MAX30105 OK");
  }

  // IMU init AFTER MAX30105, then restore I2C clock to 400kHz
  Wire.setClock(400000);
  if (myIMU.begin() != 0) {
    Serial.println("IMU ERROR - check wiring or address");
  } else {
    Serial.println("IMU OK - aX,aY,aZ,gX,gY,gZ");
  }

  // BLE init
  Bluefruit.begin();
  Bluefruit.setName("Wearables");
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  setupNRF();
  startAdv();

  Serial.println("BLE advertising started");
  Serial.println("Setup complete!");
}

void setupNRF(void) {
  customService.begin();

  imuChar.setProperties(CHR_PROPS_NOTIFY);
  imuChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  imuChar.setFixedLen(64);
  imuChar.begin();

  ecgChar.setProperties(CHR_PROPS_NOTIFY);
  ecgChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  ecgChar.begin();

  loadChar.setProperties(CHR_PROPS_NOTIFY);
  loadChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  loadChar.begin();

  spo2Char.setProperties(CHR_PROPS_NOTIFY);
  spo2Char.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  spo2Char.begin();
}

void startAdv() {
  Serial.println("In Adv");
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 1600);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

// CHANGED: readRawAccel/Gyro → readFloatAccel/Gyro (raw reads return 0 due to I2C timing issues)
IMUData readIMUData() {
  IMUData data;
  data.aX = myIMU.readFloatAccelX();
  data.aY = myIMU.readFloatAccelY();
  data.aZ = myIMU.readFloatAccelZ();
  data.gX = myIMU.readFloatGyroX();
  data.gY = myIMU.readFloatGyroY();
  data.gZ = myIMU.readFloatGyroZ();
  return data;
}

int updateECG() {
  if (digitalRead(A1) == HIGH || digitalRead(A2) == HIGH) {
    Serial.println("Not found");
    return 0;
  } else {
    int ecgValue = analogRead(A0);
    return ecgValue;
  }
}

int updateLoadcell() {
  if (scale.is_ready()) {
    float weight = scale.get_units();
    Serial.print("Weight: ");
    Serial.println(weight);
    return weight;
  } else {
    Serial.println("HX711 not ready");
    return 0;
  }
}

SensorData readRedIR() {
  SensorData data;

  Wire.beginTransmission(0x57);
  Wire.write(0x07);
  Wire.endTransmission();
  Wire.requestFrom(0x57, 6, true);

  data.Red = (Wire.read() << 16 | Wire.read() << 8 | Wire.read()) & 0x3FFFF;
  data.IR = (Wire.read() << 16 | Wire.read() << 8 | Wire.read()) & 0x3FFFF;

  Serial.print("Red: ");
  Serial.print(data.Red);
  Serial.print(" IR: ");
  Serial.println(data.IR);
  return data;
}

void loop() {
  if (Bluefruit.connected()) {

    // IMU — CHANGED format specifiers from %d to %.3f to match float fields
    IMUData imu = readIMUData();
    char imuStr[80];
    snprintf(imuStr, sizeof(imuStr),
            "AX:%.2f AY:%.2f AZ:%.2f GX:%.1f GY:%.1f GZ:%.1f",
            imu.aX, imu.aY, imu.aZ, imu.gX, imu.gY, imu.gZ);
    imuChar.notify((uint8_t*)imuStr, strlen(imuStr));
    Serial.println(imuStr);

    // ECG
    int ecgValue = updateECG();
    char ecgStr[16];
    snprintf(ecgStr, sizeof(ecgStr), "%d", ecgValue);
    ecgChar.notify((uint8_t*)ecgStr, strlen(ecgStr));

    // Loadcell
    float weight = updateLoadcell();
    char loadStr[16];
    snprintf(loadStr, sizeof(loadStr), "%.2f", weight);
    loadChar.notify((uint8_t*)loadStr, strlen(loadStr));

    // SPO2
    SensorData data = readRedIR();
    char dataStr[40];
    snprintf(dataStr, sizeof(dataStr), "%ld,%ld", data.Red, data.IR);
    spo2Char.notify((uint8_t*)dataStr, strlen(dataStr));

    delay(10);
  }
}

void connect_callback(uint16_t conn_handle) {
  deviceConnected = true;
  Serial.println("Connected!");
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  deviceConnected = false;
  Serial.println("Disconnected!");
}
