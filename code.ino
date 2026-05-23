#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <BH1750.h>
#include <Preferences.h> // To save API Key permanently

// ---------------- Pins ----------------
#define PIN_MQ135 34
#define PIN_RAIN  35  

// ---------------- Objects ----------------
Adafruit_BMP280 bmp;
BH1750 lightMeter;
Preferences preferences;

// ---------------- Variables ----------------
char thingspeak_key[20] = ""; // Variable to hold the user's key
const char* host = "api.thingspeak.com";
const float SEA_LEVEL_PRESSURE = 1013.25; // hPa

// Flag for saving data
bool shouldSaveConfig = false;

// Callback notifying us of the need to save config
void saveConfigCallback () {
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(); // Standard I2C (SDA=21, SCL=22)

  // 1. Initialize Sensors
  // BMP280
  if (!bmp.begin(0x76)) {
    Serial.println("❌ BMP280 not found! Check Wiring.");
  } else {
    Serial.println("✅ BMP280 OK");
  }

  // BH1750
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("✅ BH1750 OK");
  } else {
    Serial.println("❌ BH1750 not found! Check Wiring.");
  }

  // 2. Load Saved API Key from Memory
  preferences.begin("weather_cfg", false);
  String temp_key = preferences.getString("api_key", "");
  temp_key.toCharArray(thingspeak_key, 20);

  // 3. Setup WiFiManager
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // Create Custom Text Box for ThingSpeak Key
  // Fields: ID, Label, Default Value, Length
  WiFiManagerParameter custom_api_key("apikey", "ThingSpeak Write API Key", thingspeak_key, 20);
  wifiManager.addParameter(&custom_api_key);

  // 4. Start Connection Portal
  // If it can't connect, it creates a WiFi named "Robofret_Weather_Setup"
  if (!wifiManager.autoConnect("Robofret_Weather_Setup", "password123")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
  }

  // 5. Save New Key if User Updated It
  if (shouldSaveConfig) {
    strcpy(thingspeak_key, custom_api_key.getValue());
    preferences.putString("api_key", thingspeak_key);
    Serial.println("New API Key Saved!");
  }
  preferences.end(); 

  Serial.println("\n✅ WiFi Connected & Ready!");
  Serial.print("Using API Key: ");
  Serial.println(thingspeak_key);
}

void loop() {
  // -------- Read Sensors --------
  float temperature = bmp.readTemperature();
  float pressure = bmp.readPressure() / 100.0F; // hPa
  float altitude = 44330.0 * (1.0 - pow(pressure / SEA_LEVEL_PRESSURE, 0.1903));
  float lux = lightMeter.readLightLevel();

  // MQ135 (Air Quality)
  int mqRaw = analogRead(PIN_MQ135);
  int aqi = map(mqRaw, 400, 3500, 50, 500);
  aqi = constrain(aqi, 0, 500);

  // Rain Sensor
  int rainRaw = analogRead(PIN_RAIN);
  int rainLevel = map(rainRaw, 0, 4095, 100, 0); // Invert map (Low raw = Wet)
  rainLevel = constrain(rainLevel, 0, 100);

  // -------- Debug Output --------
  Serial.println("-----------------------------");
  Serial.printf("Temp: %.2f C\n", temperature);
  Serial.printf("Pressure: %.2f hPa\n", pressure);
  Serial.printf("Light: %.2f Lux\n", lux);
  Serial.printf("AQI: %d\n", aqi);
  Serial.printf("Rain: %d %%\n", rainLevel);

  // -------- ThingSpeak Push --------
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    if (client.connect(host, 80)) {
      
      // Construct URL dynamically using the saved 'thingspeak_key'
      String url = "/update?api_key=" + String(thingspeak_key) +
                   "&field1=" + String(temperature) +
                   "&field2=" + String(pressure) +
                   "&field3=" + String(lux) +
                   "&field4=" + String(aqi) +
                   "&field5=" + String(rainLevel);

      client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" +
                   "Connection: close\r\n\r\n");

      Serial.println("☁️ Data pushed to ThingSpeak");
    } else {
      Serial.println("❌ Connection to ThingSpeak Failed");
    }
  }

  // ThingSpeak Free Tier limit is 15 seconds. 20s is safe.
  delay(20000); 
}