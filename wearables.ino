#include <bluefruit.h>
#include <Adafruit_SPIFlash.h>
#include <Adafruit_TinyUSB.h>
#include <Wire.h>
#include <LSM6DS3.h>
#include "HX711.h"
#include <Wire.h>
#include "MAX30105.h"

Adafruit_FlashTransport_QSPI flashTransport;


// Struct to hold Red and IR values
struct SensorData {
  long Red;
  long IR;
};
// IMU
struct IMUData {
  int16_t aX, aY, aZ;
  int16_t gX, gY, gZ;
};
LSM6DS3 myIMU(I2C_MODE, 0x6A);  //I2C device address 0x6A
int16_t aX, aY, aZ, gX, gY, gZ;

// LOADCELL
#define DATA_PIN 7
#define CLOCK_PIN 8
HX711 scale;

// SPO2
MAX30105 particleSensor;
// const int N_set_gather = 5;
// const int N_gather_size = 100;
// unsigned long TimeStamp[N_set_gather * N_gather_size];
// unsigned long Red[N_set_gather * N_gather_size];
// unsigned long IR[N_set_gather * N_gather_size];
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

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(100);
  Serial.println(" IN SETUP");

  //  Enable DC-DC converter ************************************************************** NRF_POWER->DCDCEN = 1;  // Enable DC/DC converter for REG1 stage
  NRF_POWER->DCDCEN = 1;  // Enable DC/DC converter for REG1 stage

  // Flash power-down mode ***************************************************************
  flashTransport.begin();
  flashTransport.runCommand(0xB9);  // enter deep power-down mode
  delayMicroseconds(5);
  flashTransport.end();

  Serial.println("In Setup");
  // imu start
  if (myIMU.begin() != 0) {
    Serial.println("Device error");
  } else {
    Serial.println("aX,aY,aZ,gX,gY,gZ");
  }
  // ECG begin
  pinMode(A1, INPUT);
  pinMode(A2, INPUT);

  // loadcell begin
  scale.begin(DATA_PIN, CLOCK_PIN);
  scale.set_scale(121.16);  // calibration factor from esp version directly
  scale.tare();             // zero the scale
  // SPO2 begin


  Wire.begin();  // Initialize I2C
  byte ledBrightness = 40;
  byte sampleAverage = 1;
  byte ledMode = 2;
  int sampleRate = 3200;
  int pulseWidth = 69;
  int adcRange = 4096;
  // particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30105 not found. Check wiring!");
  } else {
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  }
  //  ble
  Bluefruit.begin();
  Bluefruit.setName("Wearables");
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  setupNRF();
  startAdv();

  Serial.println("BLE peripheral is now advertising...");
}

void setupNRF(void) {
  customService.begin();

  imuChar.setProperties(CHR_PROPS_NOTIFY);
  imuChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  imuChar.setFixedLen(64);
  // imuChar.setUserDescriptor("IMU Data");
  imuChar.begin();

  ecgChar.setProperties(CHR_PROPS_NOTIFY);
  ecgChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  // ecgChar.setFixedLen(4);
  // ecgChar.setUserDescriptor("ECG Value");
  ecgChar.begin();

  loadChar.setProperties(CHR_PROPS_NOTIFY);
  loadChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  // loadChar.setFixedLen(4);
  // loadChar.setUserDescriptor("Loadcell Weight");
  loadChar.begin();

  spo2Char.setProperties(CHR_PROPS_NOTIFY);
  spo2Char.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  // spo2Char.setFixedLen(4);
  // spo2Char.setUserDescriptor("Loadcell Weight");
  spo2Char.begin();
}


void startAdv() {
  Serial.println("In Adv");
  Bluefruit.Advertising.addName();
  // Bluefruit.Advertising.addService();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 1600);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

IMUData readIMUData() {
  IMUData data;
  data.aX = myIMU.readRawAccelX();
  data.aY = myIMU.readRawAccelY();
  data.aZ = myIMU.readRawAccelZ();
  data.gX = myIMU.readRawGyroX();
  data.gY = myIMU.readRawGyroY();
  data.gZ = myIMU.readRawGyroZ();
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
    float weight = scale.get_units();  // weight in grams
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

    // IMU
    IMUData imu = readIMUData();
    char imuStr[80];
    snprintf(imuStr, sizeof(imuStr),
             "AX:%d AY:%d AZ:%d GX:%d GY:%d GZ:%d",
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

    //spo2
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
