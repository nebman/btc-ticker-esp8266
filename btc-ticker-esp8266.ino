//#define DEBUGGING 1

#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <ArduinoJson.h>

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <WebSocketClient.h>  // https://github.com/morrissinger/ESP8266-Websocket

#include <LedControl.h>

char  host[] = "ws.pusherapp.com";
const int   port = 80;
char  url[] = "/app/de504dc5763aeef9ff52";
WiFiClient client;
WebSocketClient ws;

const int     timeout_hard_threshold = 120000; // reconnect after 120sec
const int     timeout_soft_threshold = 60000; // timeout default 60sec, send ping to check connection
boolean       timeout_soft_sent_ping = false;
unsigned long timeout_next = 0;
unsigned long timeout_flashing_dot = 0;
unsigned int  timeout_reconnect_count = 0;

LedControl lc = LedControl(D7, D8, D6, 1);
WiFiManager wifiManager;

int last = 0;
int err  = 0;

unsigned int  timeout_swap_usdbtc = 0;
boolean       usdbtc = false;

void setup() {
  // start serial for debug output
  Serial.begin(115200);
  Serial.println();
  Serial.println("INIT");
  
  // wakeup 7 segment display
  lc.shutdown(0, false);
  // set brightness
  lc.setIntensity(0, 8);

  // clear all digits
  setAll(' ', false, 0, 8);

  // use 8 dots for startup animation
  int i=7;

  // set first dot... etc
  lc.setChar(0, i--, ' ', true);
  Serial.println("WIFI AUTO-CONFIG");
 
  // autoconfiguration portal for wifi settings
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.autoConnect();

  // wifi setup finished 
  lc.setChar(0, i--, ' ', true);
  Serial.println("WIFI DONE");

  // MDNS
  if (!MDNS.begin("esp8266")) {
    Serial.println("MDNS ERROR!");
  } else {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS UP");
  }
  lc.setChar(0, i--, ' ', true);

  // Arduino OTA
  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA End");
  });
  ArduinoOTA.begin();
  lc.setChar(0, i--, ' ', true);
  Serial.println("OTA started");
 
  connect(false);
}

void connect(boolean reconnect) {
  int i=3;
  timeout_soft_sent_ping = false;

  if (reconnect) {
    timeout_reconnect_count++;
    if (timeout_reconnect_count > 2) reboot();
    setAll(' ', true, 0, 8);
    setAll(' ', false, 0, i);
  }

  // connect to the server
  Serial.println("Client connecting...");
  if (client.connect(host, 80)) {
    Serial.println("WS Connected");
    lc.setChar(0, i--, ' ', true);
  } else {
    Serial.println("WS Connection failed.");
    reboot();
  }

  // set up websocket details
  ws.path = url;
  ws.host = host;
  ws.protocol = "pusher";

  Serial.println("Starting WS handshake...");
  
  if (ws.handshake(client)) {
    Serial.println("WS Handshake successful");
    lc.setChar(0, i--, ' ', true);
  } else {
    Serial.println("WS Handshake failed.");
    reboot();
  }

  // subscribe to event channel "live_trades"
  Serial.println("WS Subscribe");
  ws.sendData("{\"event\": \"pusher:subscribe\", \"data\": {\"channel\": \"live_trades\"}}");
  lc.setChar(0, i--, ' ', true);

  

  // Finish setup, complete animation, set first timeout
  setAll(' ', true, 0, 8);
  timeout_next = millis() + timeout_hard_threshold;
  Serial.println("All set up, waiting for first trade...");

  Serial.println();
}

void loop() {
  ArduinoOTA.handle();

  // check for hard timeout
  if( (long)(millis() - timeout_next) >= 0) {
    Serial.println();
    Serial.println("TIMEOUT -> RECONNECT");
    Serial.println();

    connect(true);
  }

  // check for soft timeout (slow day?)
  if(!timeout_soft_sent_ping && client.connected() && (long)(millis() - timeout_next + timeout_soft_threshold) >= 0) {
    // ok, lets send a PING to check connection
    Serial.println("soft timeout -> sending ping to server");
    ws.sendData("", WS_OPCODE_PING);
    timeout_soft_sent_ping = true;
  }
  
  if(last && ((long)(millis() - timeout_flashing_dot) >= 0)) {
    lc.setDigit(0, 0, (last%10), false);
  }

  // alternate USD and BTC in display every 10sec
  if (((long)(millis() - timeout_swap_usdbtc) >= 0)) {
    if (usdbtc) {
      lc.setRow(0, 7, B00111110); // U
      lc.setChar(0, 6, '5', false);
      lc.setChar(0, 5, 'd', false);
    } else {
      lc.setChar(0, 7, 'B',false); // b
      lc.setRow (0, 6, B00001111); // t
      lc.setChar(0, 5, 'C',false); // c
    }
    usdbtc = !usdbtc;
    timeout_swap_usdbtc = millis() + 10000;
  }

  if (client.connected()) {
    String line;
    uint8_t opcode = 0;
    
    ws.getData(line, &opcode);

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

      // alright, check for trade events
      if (root["event"] == "trade") {
        timeout_next = millis() + timeout_hard_threshold;
        Serial.print("GOT TRADE ");

        // need to deserialize twice, data field is escaped
        JsonObject& trade = jsonBuffer.parseObject(root["data"].as<const char*>());
          
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

      // this is gentlemen.
      if (last >= 10000) {
        lc.setDigit(0, 4, (last/10000), false);
      } else {
        lc.setChar(0, 4, ' ',false); //  
      }
      
      lc.setDigit(0, 3, (last%10000)/1000, false);
      lc.setDigit(0, 2, (last%1000)/100, false);
      lc.setDigit(0, 1, (last%100)/10, false);
      lc.setDigit(0, 0, (last%10), true);

      timeout_flashing_dot = millis() + 100;
    } 
  } else {
    Serial.println("Client disconnected.");
    connect(true);
  }

  delay(5);
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

  Serial.println(myWiFiManager->getConfigPortalSSID());
}


