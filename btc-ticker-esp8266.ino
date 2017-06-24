#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <ArduinoJson.h>

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

#include <LedControl.h>

const String url = "http://api.coindesk.com/v1/bpi/currentprice.json";

const int brightness_full = 8; // 0-15 normal brightness
const int brightness_load = 8; // 0-15 while requesting data

unsigned long next_refresh = 0;

LedControl lc = LedControl(D7, D8, D6, 1);
WiFiManager wifiManager;

HTTPClient http;

void setup() {
  // start serial for debug output
  Serial.begin(115200);
  Serial.println();
  Serial.println("INIT");
  
  // wakeup 7 segment display
  lc.shutdown(0, false);
  // set brightness
  lc.setIntensity(0, brightness_full);

  // start with 8 dots
  for (int i=0; i<8; i++) {
    lc.setChar(0, i, ' ', true); // all spaces with dot
  }

  Serial.println("WIFI AUTO-CONFIG");
 
  // autoconfiguration portal for wifi settings
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.autoConnect();

  Serial.println("WIFI DONE");

  // MDNS
  if (!MDNS.begin("esp8266")) {
    Serial.println("MDNS ERROR!");
  }
  MDNS.addService("http", "tcp", 80);
  Serial.println("mDNS UP");

  // Arduino OTA
  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA End");
  });
  ArduinoOTA.begin();
  
  // clear display
  for (int i=0; i<8; i++) {
    lc.setChar(0,i,' ',0);
  }

  
}

void loop() {
  ArduinoOTA.handle();

  if( (long)(millis() - next_refresh) >= 0)
  {
    setDashes();
    setBrightnessLoading(true);
  
    Serial.print("REQUEST ");
    Serial.println(url);
    
    http.begin(url);
    int code = http.GET();
    Serial.println(code);
    
    if (code == 200) {
      String payload = http.getString();
  
      DynamicJsonBuffer jsonBuffer(1100);
      JsonObject& root = jsonBuffer.parseObject(payload);
      JsonObject& bpi = root["bpi"];
      JsonObject& bpi_USD = bpi["USD"];
      int last = bpi_USD["rate_float"]; // last USD btc
  
      Serial.print("last = ");
      Serial.println(last);
  
      lc.setChar(0, 7, 'B',false); // b
      lc.setRow (0, 6, B00001111); // t
      lc.setChar(0, 5, 'C',false); // c
  
      lc.setDigit(0, 3, (last/1000), false);
      lc.setDigit(0, 2, (last%1000)/100, false);
      lc.setDigit(0, 1, (last%100)/10, false);
      lc.setDigit(0, 0, (last%10), false);
  
      setBrightnessLoading(false);
    } else {
      lc.setChar (0, 3, 'E', true);
      lc.setDigit(0, 2, (code%1000)/100, false);
      lc.setDigit(0, 1, (code%100)/10, false);
      lc.setDigit(0, 0, (code%10), false);
    }
    
    http.end();
    next_refresh = millis() + 30000;
  }
  
}

void setDashes (void) {
  for (int i=0; i<4; i++) {
    lc.setChar(0, i, '-', false);
  }
}

void setBrightnessLoading(bool loading) {
  lc.setIntensity(0, loading ? brightness_load : brightness_full);
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered wifi portal config mode");
  Serial.println(WiFi.softAPIP());

  Serial.println(myWiFiManager->getConfigPortalSSID());
}


