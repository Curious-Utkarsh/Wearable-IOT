#include "BLEDevice.h"

// ===============================
// UUID DEFINITIONS
// ===============================
static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID charUUID_imu((uint16_t)0x27A8);
static BLEUUID charUUID_ecg((uint16_t)0x2A37);
static BLEUUID charUUID_load((uint16_t)0x2A98);
static BLEUUID charUUID_spo2((uint16_t)0x2A8D);


static BLEAdvertisedDevice* myDevice;
static BLERemoteCharacteristic* imuChar;
static BLERemoteCharacteristic* ecgChar;
static BLERemoteCharacteristic* loadChar;
static BLERemoteCharacteristic* spo2Char;

bool doConnect = false;
bool connected = false;
bool doScan = true;

// ===============================
// PARSER FUNCTION
// ===============================
void parseMessage(uint16_t type_, uint8_t* pData, size_t length) {
  char message[300];
  int idx = 0;
  message[0] = '\0'; // Clear buffer

  // Convert payload bytes to string
  String dataStr = "";
  for (size_t i = 0; i < length; i++) dataStr += (char)pData[i];

  // JSON start
  idx += sprintf(message + idx, "{");

  if (type_ == 1) { 
    // IMU data: ax,ay,az,gx,gy,gz
    float ax, ay, az, gx, gy, gz;
    int count = sscanf(dataStr.c_str(), "%f,%f,%f,%f,%f,%f", &ax, &ay, &az, &gx, &gy, &gz);
    if (count == 6) {
      idx += sprintf(message + idx,
                     "\"dev\":\"IMU\",\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,"
                     "\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f",
                     ax, ay, az, gx, gy, gz);
     } 
    else {
      idx += sprintf(message + idx, "\"dev\":\"IMU\",\"raw\":\"%s\"", dataStr.c_str());
    }
  } 
  else if (type_ == 2) { 
    // ECG data
    int ecgVal = atoi(dataStr.c_str());
    idx += sprintf(message + idx, "\"dev\":\"ECG\",\"ecg\":%d", ecgVal);
  } 
  else if (type_ == 3) { 
    // Loadcell data
    float load = atof(dataStr.c_str());
    idx += sprintf(message + idx, "\"dev\":\"Load\",\"load\":%.2f", load);
  }
 else if (type_ == 4) {
   long red, ir;
  int count = sscanf(dataStr.c_str(), "%ld,%ld", &red, &ir);
  if (count == 2) {
  idx += sprintf(message + idx,"\"dev\":\"RedIR\",\"Red\":%ld,\"IR\":%ld", red, ir);
    
  }
 }
  // JSON end
  idx += sprintf(message + idx, "}\n");

  // Print to both Serial and Serial1
  Serial.print(message);
  Serial1.print(message);
}

// ===============================
// NOTIFICATION CALLBACK
// ===============================
void notifyCallback(BLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify) {
  BLEUUID uuid = characteristic->getUUID();
  uint8_t type_ = 0;

  if (uuid.equals(charUUID_imu)) type_ = 1;
  else if (uuid.equals(charUUID_ecg)) type_ = 2;
  else if (uuid.equals(charUUID_load)) type_ = 3;
  else if (uuid.equals(charUUID_spo2)) type_ = 4;

  if (type_ > 0) parseMessage(type_, data, length);
  else Serial.println("Unknown characteristic notification received!");
}

// ===============================
// CLIENT CALLBACKS
// ===============================
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pClient) {}
  void onDisconnect(BLEClient* pClient) {
    connected = false;
    Serial.println("Disconnected from server");
  }
};

// ===============================
// CONNECT FUNCTION
// ===============================
bool connectToServer() {
  Serial.print("Connecting to ");
  Serial.println(myDevice->getName().c_str());

  BLEClient* pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  pClient->connect(myDevice);

  BLERemoteService* service = pClient->getService(serviceUUID);
  if (service == nullptr) {
    Serial.println("Failed to find service");
    pClient->disconnect(); 
    return false;
  }

  imuChar = service->getCharacteristic(charUUID_imu);
  ecgChar = service->getCharacteristic(charUUID_ecg);
  loadChar = service->getCharacteristic(charUUID_load);
  spo2Char = service->getCharacteristic(charUUID_spo2);

  if (imuChar && imuChar->canNotify()) imuChar->registerForNotify(notifyCallback);
  if (ecgChar && ecgChar->canNotify()) ecgChar->registerForNotify(notifyCallback);
  if (loadChar && loadChar->canNotify()) loadChar->registerForNotify(notifyCallback);
  if (spo2Char && spo2Char->canNotify()) spo2Char->registerForNotify(notifyCallback);

  connected = true;
  return true;
}

// ===============================
// ADVERTISEMENT CALLBACK
// ===============================
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.getName() == "Wearables") {
      Serial.print("Found device: ");
      Serial.println(advertisedDevice.toString().c_str());
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = false;
    }
  }
};

// ===============================
// SETUP + LOOP
// ===============================
void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17
  Serial.println("Starting BLE Client...");
  Serial1.println("BLE Client Ready");

  BLEDevice::init("Wearables_Client");
  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  scan->setInterval(1349);
  scan->setWindow(749);
  scan->setActiveScan(true);
  scan->start(5, false);
}

void loop() {
  if (doConnect) {
    if (connectToServer()) {
      Serial.println("Connected to server");
      Serial1.println("Connected to server");
    } else {
      Serial.println("Failed to connect");
      Serial1.println("Failed to connect");
    }
    doConnect = false;
  }

  if (!connected && doScan) {
    BLEDevice::getScan()->start(0);
  }

  delay(10);
}
