#include <Wire.h>
#include <SparkFun_TMP117.h>
#include <SparkFun_SHTC3.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Preferences.h>
//#include <BLE2902.h>  // Not needed - NimBLE auto-adds descriptor for indications

// ================= Configuration =================

// Sensor I2C addresses
#define TMP117_ADDR 0x48
#define SHTC3_ADDR 0x70

const int I2C_SDA = 6;  // for esp32-c6
const int I2C_SCL = 7;  // for exp32-c6

// BLE UUIDs
#define SERVICE_UUID              "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_TEMP_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"


#define BLE_NAMESPACE "UHI_namespace"
#define FILENAME "UHI_TMP117_SHTC3_V2"

// ================= Global Variables =================

BLECharacteristic *pTempCharacteristic;

BLEAdvertising *pAdvertising;

TMP117 sensor;
SHTC3 shtc3;

bool deviceConnected = false;
bool TMP117Present = false;
bool SHTC3Present = false;
unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 1000;

static float temperatureC = 0.0;
static float humidity = 0.0;

String bleName = "quest_xxx";

// ================= BLE Callbacks =================

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("✅ Client connected");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("❌ Client disconnected");
    delay(100);
    pServer->startAdvertising();
  }
};

// ================= Helper Function =================

bool checkSensor(uint8_t address, const char* name) {
  Wire.beginTransmission(address);
  if (Wire.endTransmission() == 0) {
    Serial.print(name); Serial.println(" is connected.");
    return true;
  } else {
    Serial.print(name); Serial.println(" not found.");
    return false;
  }
}

// ================= Helper Function =================

String getOrGenerateBLEName() {
  Preferences prefs;
  prefs.begin(BLE_NAMESPACE, false);

        String forcedNumber = ""; // Set this to your desired value, e.g., "123" to force

          if (forcedNumber != "") {
            prefs.putString("bleNumber", forcedNumber);
            Serial.print("⚠️ FORCING BLE number: "); Serial.println(forcedNumber);
            prefs.end();
            return "quest_" + forcedNumber;
          }

  String storedNumber = prefs.getString("bleNumber", ""); // <--- 1. Reads the number
  if (storedNumber == "") {                               // <--- 2. Checks if it doesn't exist (is empty)
    randomSeed(micros());
    int num = random(0, 1000);  // 000–999

    // The commented out lines below are for specific debugging/testing and should remain commented for normal operation.
    // A SPECIAL CASE WHERE AN ESP32 HAS BEEN USED BEFORE COMMENT OUT OTHERWISE =>    num = 7;
    // num=7;

    char buffer[4];
    sprintf(buffer, "%03d", num);
    storedNumber = String(buffer);
    prefs.putString("bleNumber", storedNumber);           // <--- 3. Creates and stores if it didn't exist
    Serial.print("🔢 Generated new BLE number: "); Serial.println(storedNumber);
  } else {
    Serial.print("🔢 Using stored BLE number: "); Serial.println(storedNumber);
  }

  prefs.end();
  return "quest_" + storedNumber;
}

// ================= Setup =================

void setup() {
  Serial.begin(115200);
  delay(1000);                            
  Serial.println("🔧 Starting BLE Sensor");
  Serial.printf("Filename: %s \n",FILENAME);

Wire.begin(I2C_SDA, I2C_SCL); // Explicitly set the pins for esp32-c6

  if (checkSensor(TMP117_ADDR, "TMP117")) {
          TMP117Present = true;
          Wire.setClock(400000);
  }

  if (checkSensor(SHTC3_ADDR, "SHTC3")){

          SHTC3Present = true;
          Wire.setClock(25000);  // clock must be slower for this sensor
  }

  if (TMP117Present && !sensor.begin()) {
    Serial.println("TMP117 init failed."); while (1);
  }

  if (SHTC3Present && shtc3.begin() != SHTC3_Status_Nominal) {
    Serial.println("SHTC3 init failed."); while (1);
  }

  // Retrieve or generate BLE name
  bleName = getOrGenerateBLEName();
  Serial.print("🔖 BLE Name: "); Serial.println(bleName);

  // Initialize BLE
  BLEDevice::init(bleName.c_str());
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pTempCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_TEMP_UUID,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
  );

  pService->start();

  // Setup BLE advertising

  pAdvertising = BLEDevice::getAdvertising();

      // Advertisement data: service UUID
      BLEAdvertisementData advertisementData;
      advertisementData.setCompleteServices(BLEUUID(SERVICE_UUID));
      pAdvertising->setAdvertisementData(advertisementData);

      // Scan response data: device name
      BLEAdvertisementData scanResponseData;
      scanResponseData.setName(bleName.c_str());
      pAdvertising->setScanResponseData(scanResponseData);

  // Diagnostic: print advertising payload info
  Serial.println("📡 Advertising config:");
  Serial.print("   Adv payload length: ");
  Serial.println(advertisementData.getPayload().length());
  Serial.print("   Scan resp payload length: ");
  Serial.println(scanResponseData.getPayload().length());

  pAdvertising->start();

  // Debug
  Serial.print("📘 Service UUID: "); Serial.println(SERVICE_UUID);
  Serial.print("📘 Temperature UUID: "); Serial.println(CHARACTERISTIC_TEMP_UUID);
  Serial.print("📡 Scan response name: "); Serial.println(bleName);
}

// ================= Loop =================

void loop() {
  unsigned long now = millis();

  if (now - lastSensorRead >= sensorInterval) {
    lastSensorRead = now;

    if (TMP117Present && sensor.dataReady()) {
      temperatureC = sensor.readTempC();
      humidity = 0.0;
    }

    if (SHTC3Present && shtc3.update() == SHTC3_Status_Nominal) {
      temperatureC = shtc3.toDegC();
      humidity = shtc3.toPercent();
    }

    if (deviceConnected) {
      // 1. Increased buffer size to 32 to safely hold two numbers
      char tempStr[32];

      // 2. Modified format to "Temperature,Humidity" (CSV style)
      sprintf(tempStr, "%.2f,%.2f", temperatureC, humidity);

      pTempCharacteristic->setValue(tempStr);
      pTempCharacteristic->notify();

      Serial.print("📤 BLE Sent: ");
      Serial.println(tempStr);
    }

    Serial.print(".🌡 Temp: ");
    Serial.print(temperatureC);
    Serial.print(" °C");
    Serial.print(". 🌡 Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
  }

  delay(10);
}