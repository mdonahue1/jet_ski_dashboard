#include <Arduino.h>
#include <WiFi.h>

/* Carrier wiring: XIAO D6/TX -> STM32 PC7/RX, STM32 PC6/TX -> D7/RX. */
HardwareSerial CarrierSerial(0);

static const uint32_t kBaud = 115200U;
static const int kLedPin = 15;
static uint32_t heartbeat = 0U;
static uint32_t scanId = 0U;
static uint32_t nextHeartbeatMs = 0U;
static uint32_t nextScanMs = 0U;
static bool scanRunning = false;

static String csvSafeSsid(const String &ssid)
{
  String safe = ssid;
  safe.replace(',', '_');
  safe.replace('\r', ' ');
  safe.replace('\n', ' ');
  return safe;
}

static void beginScan()
{
  /* Asynchronous, passive observation only; hidden SSIDs are included. */
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true, false, 120U);
  scanRunning = true;
  Serial.println("scan started");
}

static void publishScan(int count)
{
  CarrierSerial.printf("WD,BEGIN,%lu,%d\n", (unsigned long)scanId, count);
  Serial.printf("scan %lu: %d APs\n", (unsigned long)scanId, count);

  for (int i = 0; i < count; ++i)
  {
    String ssid = csvSafeSsid(WiFi.SSID(i));
    String bssid = WiFi.BSSIDstr(i);
    int32_t rssi = WiFi.RSSI(i);
    int32_t channel = WiFi.channel(i);
    int32_t auth = (int32_t)WiFi.encryptionType(i);

    CarrierSerial.printf("WD,AP,%lu,%d,%ld,%ld,%ld,%s,%s\n",
                         (unsigned long)scanId, i, (long)channel, (long)rssi,
                         (long)auth, bssid.c_str(), ssid.c_str());
    Serial.printf("  ch %ld  %ld dBm  %s  %s\n",
                  (long)channel, (long)rssi, bssid.c_str(), ssid.c_str());
    delay(2U);
  }

  CarrierSerial.printf("WD,END,%lu\n", (unsigned long)scanId);
  scanId++;
}

void setup()
{
  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, HIGH);

  /* Native USB CDC remains available for PC diagnostics on COM5. */
  Serial.begin(kBaud);
  CarrierSerial.begin(kBaud, SERIAL_8N1, D7, D6);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);

  Serial.println("XIAO ESP32-C6 carrier link test");
  Serial.print("Station MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("UART0: D7/RX GPIO17, D6/TX GPIO16, 115200 8N1");
  Serial.println("Passive Wi-Fi scanner enabled");
  nextHeartbeatMs = millis();
  nextScanMs = millis() + 500U;
}

void loop()
{
  while (CarrierSerial.available() > 0)
  {
    int value = CarrierSerial.read();
    Serial.write((uint8_t)value);
  }

  uint32_t now = millis();
  if ((int32_t)(now - nextHeartbeatMs) >= 0)
  {
    digitalWrite(kLedPin, (heartbeat & 1U) ? LOW : HIGH);
    CarrierSerial.printf("C6,READY,%lu\n", (unsigned long)heartbeat);
    Serial.printf("heartbeat %lu\n", (unsigned long)heartbeat);
    heartbeat++;
    nextHeartbeatMs = now + 1000U;
  }

  if (!scanRunning && ((int32_t)(now - nextScanMs) >= 0))
  {
    beginScan();
  }

  if (scanRunning)
  {
    int result = WiFi.scanComplete();
    if (result >= 0)
    {
      publishScan(result);
      WiFi.scanDelete();
      scanRunning = false;
      nextScanMs = millis() + 3000U;
    }
    else if (result == WIFI_SCAN_FAILED)
    {
      Serial.println("scan failed; retrying");
      WiFi.scanDelete();
      scanRunning = false;
      nextScanMs = millis() + 3000U;
    }
  }

  delay(2U);
}
