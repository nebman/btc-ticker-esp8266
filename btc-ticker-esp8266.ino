// CONFIGURATION PART *************************************************************************

// Set DEBUGGING for addiditonal debugging output over serial
//#define DEBUGGING

// Set Display type, either SEGMENT7 or MATRIX32
#define SEGMENT7
//#define MATRIX32

// Features MDNS, OTA
#define FEATURE_OTA
#define FEATURE_MDNS

// set Hostname
#define HOSTNAME "ESP-BTC-TICKER"

// END CONFIG *********************************************************************************



#include "exchanges.h"

#ifdef FEATURE_MDNS
  #include <ESP8266mDNS.h>
#endif

#ifdef FEATURE_OTA
  #include <ArduinoOTA.h>
#endif

#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>


#include <ArduinoJson.h>

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <WebSocketClient.h>  // https://github.com/pablorodiz/Arduino-Websocket

#ifdef SEGMENT7
  #include <LedControl.h>
#endif

exchange_settings exchange = bitstampUSDBTC;

WiFiManager wifiManager;
WiFiClient client;
WebSocketClient ws;

// Variables and constants for timeouts 

const int     timeout_hard_threshold = 120000; // reconnect after 120sec
const int     timeout_soft_threshold = 60000; // timeout default 60sec, send ping to check connection
boolean       timeout_soft_sent_ping = false;
unsigned long timeout_next = 0;
unsigned long timeout_flashing_dot = 0;
unsigned int  timeout_reconnect_count = 0;

#ifdef SEGMENT7
  LedControl lc = LedControl(D7, D8, D6, 4);
  unsigned int  timeout_swap_usdbtc = 0;
  boolean       usdbtc = false;
#endif

// current values
int last = 0;
int err  = 0;

void setup() {
  // start serial for debug output
  Serial.begin(115200);
  Serial.println();
  Serial.println("INIT");

  initDisplay();
  clearDisplay();
  writeStringDisplay("boot");

  // use 8 dots for startup animation
  setProgress(1);
  Serial.println("WIFI AUTO-CONFIG");
  writeStringDisplay("Auto");
 
  // autoconfiguration portal for wifi settings
#ifdef HOSTNAME
  WiFi.hostname(HOSTNAME);
#endif
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.autoConnect();

  // wifi setup finished 
  setProgress(2);
  Serial.println("WIFI DONE");

#ifdef FEATURE_MDNS
  // MDNS
  if (!MDNS.begin(HOSTNAME)) {
    Serial.println("MDNS ERROR!");
  } else {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS UP");
  }
  setProgress(3);
#endif

#ifdef FEATURE_OTA
  // Arduino OTA
  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
    writeStringDisplay("OTASTART");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA End");
    writeStringDisplay("OTAEND");
  });
  ArduinoOTA.begin();
  setProgress(4);
  Serial.println("OTA started");
#endif

  setProgress(5);
  connect(false);
}

void connect(boolean reconnect) {
  timeout_soft_sent_ping = false;

  if (reconnect) {
    timeout_reconnect_count++;
    if (timeout_reconnect_count > 2) reboot();
    clearDisplay();
  }

  // connect to the server
  Serial.println("Client connecting...");
  if (client.connect(exchange.host, exchange.port)) {
    Serial.println("WS Connected");
    setProgress(6);
  } else {
    Serial.println("WS Connection failed.");
    reboot();
  }

  // set up websocket details
  ws.setPath(exchange.url);
  ws.setHost(exchange.host);
  ws.setProtocol(exchange.wsproto);

  Serial.println("Starting WS handshake...");
  
  if (ws.handshake(client)) {
    Serial.println("WS Handshake successful");
    setProgress(7);
  } else {
    Serial.println("WS Handshake failed.");
    reboot();
  }

  // subscribe to event channel "live_trades"
  Serial.println("WS Subscribe");
  ws.sendData(exchange.subscribe);
  setProgress(8);

  

  // Finish setup, complete animation, set first timeout
  clearDisplay();
  setProgress(0);
  timeout_next = millis() + timeout_hard_threshold;
  
  Serial.println("All set up, waiting for first trade...");
  Serial.println();
}

void loop() {
#ifdef FEATURE_OTA
  ArduinoOTA.handle();
#endif
  ws.process(); // process websocket 

  // check for hard timeout
  if( (long)(millis() - timeout_next) >= 0) {
    Serial.println();
    Serial.println("TIMEOUT -> RECONNECT");
    Serial.println();
    connect(true);
  }

  // check for soft timeout (slow day?) send websocket ping
  if(!timeout_soft_sent_ping && client.connected() && (long)(millis() - timeout_next + timeout_soft_threshold) >= 0) {
    // ok, lets send a PING to check connection
    Serial.println("soft timeout -> sending ping to server");
    ws.sendData("", WS_OPCODE_PING);
    timeout_soft_sent_ping = true;
    yield();
  }

  // flash the dot when a trade occurs
  flashDotTrade();

  // alternate currency display
  alternateCurrency();

  // check if socket still connected
  if (client.connected()) {
    String line;
    uint8_t opcode = 0;
    
    if (ws.getData(line, &opcode)) {
  
      // check for PING packets, need to reply with PONG, else we get disconnected
      if (opcode == WS_OPCODE_PING) {
        Serial.println("GOT PING");
        ws.sendData("{\"event\": \"pusher:pong\"}", WS_OPCODE_PONG);
        Serial.println("SENT PONG");
        yield();
      } else if (opcode == WS_OPCODE_PONG) {
        Serial.println("GOT PONG, connection still active");
        timeout_soft_sent_ping = false;
        timeout_next = millis() + timeout_hard_threshold;
      }
  
      // check for data in received packet
      if (line.length() > 0) {
#ifdef DEBUGGING
        Serial.print("Received data: ");
        Serial.println(line);
#endif

        // parse JSON
        StaticJsonBuffer<768> jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(line);
  
        yield();
  
        // alright, check for trade events
        if (root["event"] == "trade") {
          timeout_next = millis() + timeout_hard_threshold;
          Serial.print("GOT TRADE ");
  
          // need to deserialize twice, data field is escaped
          JsonObject& trade = jsonBuffer.parseObject(root["data"].as<const char*>());
          yield();
            
          if (!trade.success()) {
            Serial.println("parse json failed");
            return;
          }
#ifdef DEBUGGING 
          trade.printTo(Serial);
#endif
  
          // extract price
          last = trade["price_str"]; // last USD btc
      
          Serial.print("price = ");
          Serial.println(last);
          
        } else {
          Serial.println("Unknown Event occured");
          Serial.println(root["event"].as<const char*>());
          root.printTo(Serial);
          Serial.println();
        }
  
        writePriceDisplay(true);
      } 
    }
  } else {
    Serial.println("Client disconnected.");
    connect(true);
  }

  delay(5);
}

void writePriceDisplay(boolean flashDot) {
#ifdef SEGMENT7
  // this is gentlemen.
  if (last >= 10000) {
    lc.setDigit(0, 4, (last/10000), false);
  } else {
    lc.setChar(0, 4, ' ',false); //  
  }
  
  lc.setDigit(0, 3, (last%10000)/1000, false);
  lc.setDigit(0, 2, (last%1000)/100, false);
  lc.setDigit(0, 1, (last%100)/10, false);
  lc.setDigit(0, 0, (last%10), flashDot);

  if (flashDot) {
    timeout_flashing_dot = millis() + 100;
  }
#endif
}

void setAll(char c, boolean dot = false, int from = 0, int len = 4);
void setAll(char c, boolean dot, int from, int len) {
  for (int i=from; i<len; i++) {
    lc.setChar(0, i, c, dot);
  }
}

void setDashes (int len = 4);
void setDashes (int len) {
  setAll('-', false, 0, len);
}

void reboot (void) {
  setDashes(8);
  delay(1000);
  ESP.reset();
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered wifi portal config mode");
  Serial.println(WiFi.softAPIP());
  writeStringDisplay(myWiFiManager->getConfigPortalSSID());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}
 
void writeStringDisplay(String s) {
  int len = s.length();
  if (len > 8) len = 8;
  char c;

  for (int i=0; i<len; i++) {
    c = s.charAt(i);
    switch (c) {
      case 'S':
      case 's':
        lc.setChar(0, 7-i, '5', false);
        break;
      case 'T':
      case 't':
        lc.setRow (0, 7-i, B00001111); // t
        break;
      case 'U':
      case 'u':
        lc.setRow (0, 7-i, B00111110); // U
        break;
      default:
        lc.setChar(0, 7-i, c, false);
    }
    
  }
}

void clearDisplay() {
#ifdef SEGMENT7
  // clear all digits
  setAll(' ', false, 0, 8);
#endif
}

void initDisplay() {
#ifdef SEGMENT7
  // wakeup 7 segment display
  lc.shutdown(0, false);
  // set brightness
  lc.setIntensity(0, 6);
#endif
}

void setProgress(byte progress) {
#ifdef SEGMENT7
  // set first dot... etc
  for (byte i=0; i<8; i++) {
    lc.setLed(0, 7-i, 0, i<progress);
  }
#endif
}

void flashDotTrade() {
#ifdef SEGMENT7
  if(last && ((long)(millis() - timeout_flashing_dot) >= 0)) {
    lc.setLed(0, 0, 0, false);
  } else {
    lc.setLed(0, 0, 0, true);
  }
#endif
}

void alternateCurrency() {
#ifdef SEGMENT7  
  // alternate USD and BTC in display every 10sec
  if (((long)(millis() - timeout_swap_usdbtc) >= 0)) {
    if (usdbtc) {
      writeStringDisplay("USD");
    } else {
      writeStringDisplay("BTC");
    }
    usdbtc = !usdbtc;
    timeout_swap_usdbtc = millis() + 10000;
  }
#endif
}

