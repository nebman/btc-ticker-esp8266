#include "exchanges.h"

// CONFIGURATION PART *************************************************************************

// Set DEBUGGING for addiditonal debugging output over serial
// #define DEBUGGING

// Set Display type, either SEGMENT7 or MATRIX32
#define SEGMENT7
//#define MATRIX32

// Features MDNS, OTA
#define FEATURE_OTA
#define FEATURE_MDNS

// set Hostname
#define HOSTNAME "ESP-BTC-TICKER"

#define ENABLE_BITFINEX  // enable WSS support and array handling
exchange_settings exchange = bitfinexUSDBTC;

// END CONFIG *********************************************************************************


#ifdef FEATURE_MDNS
  #include <ESP8266mDNS.h>
#endif

#ifdef FEATURE_OTA
  #include <ArduinoOTA.h>
#endif

#include <ArduinoJson.h>

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <WebSocketClient.h>  // https://github.com/pablorodiz/Arduino-Websocket

#ifdef SEGMENT7
  #include <LedControl.h>
#endif
#ifdef MATRIX32
  #include <U8g2lib.h>
  #include <U8x8lib.h>
#endif

WiFiManager wifiManager;
WiFiClientSecure  client;
WebSocketClient ws;

// Variables and constants for timeouts 

const int     timeout_hard_threshold = 120000; // reconnect after 120sec
const int     timeout_soft_threshold = 60000; // timeout default 60sec, send ping to check connection
boolean       timeout_soft_sent_ping = false;
unsigned long timeout_next = 0;
unsigned long timeout_flashing_dot = 0;
unsigned int  timeout_reconnect_count = 0;

#ifdef SEGMENT7
  LedControl lc = LedControl(D7, D5, D8, 1);
  unsigned int  timeout_swap_usdbtc = 0;
  boolean       usdbtc = false;
#endif
#ifdef MATRIX32
  U8G2_MAX7219_32X8_F_4W_HW_SPI matrix(U8G2_R0, D2, U8X8_PIN_NONE);
  byte _progress = 0;
  String _text = "";
  boolean _dot = false;
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
  writeStringDisplay("boot", true);

  // use 8 dots for startup animation
  setProgress(1);
  Serial.println("WIFI AUTO-CONFIG");
  writeStringDisplay("Auto", true);
 
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
    writeStringDisplay("OTASTART", true);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA End");
    writeStringDisplay("OTAEND", true);
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
        JsonArray& rootarray = jsonBuffer.parseArray(line);

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
          
        } 
        
#ifdef ENABLE_BITFINEX        
        else if (rootarray[1] == "tu") {  // Bitfinex Tradeupdate
          last = rootarray[2][3];
          Serial.print("price = ");
          Serial.println(last);
          timeout_next = millis() + timeout_hard_threshold;
        } else if (rootarray[1] == "hb") {  // Bitfinex Heartbeat
          timeout_next = millis() + timeout_hard_threshold;
        }
#endif
        
        else { // print unknown events and arrays
          root.printTo(Serial);
          rootarray.printTo(Serial);
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
#endif
  
  if (flashDot) {
    timeout_flashing_dot = millis() + 100;
  }
  
#ifdef MATRIX32
  char val[8] = "$";
  itoa(last, val+1, 10);
  flashDotTrade();
  writeStringDisplay(val, true);
#endif
}

#ifdef SEGMENT7
void setAll(char c, boolean dot = false, int from = 0, int len = 4);
void setAll(char c, boolean dot, int from, int len) {
  for (int i=from; i<len; i++) {
    lc.setChar(0, i, c, dot);
  }
}
#endif SEGMENT7


void setDashes (int len = 4);
void setDashes (int len) {
#ifdef SEGMENT7
  setAll('-', false, 0, len);
#endif
#ifdef MATRIX32
  writeStringDisplay("--------", true);
#endif
}

void reboot (void) {
  setDashes(8);
  delay(1000);
  ESP.reset();
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered wifi portal config mode");
  Serial.println(WiFi.softAPIP());
  writeStringDisplay(myWiFiManager->getConfigPortalSSID(), true);
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

/*     
 *  display segment bits
 *  
 *      1
 *    -----
 *   |     |
 * 6 |     | 2
 *   |     |
 *    -----
 *   |  7  |
 * 5 |     | 3
 *   |     |
 *    ----- . 0
 *      4
 * 
 */

void writeStringDisplay(String s, boolean fillEmpty = false);
void writeStringDisplay(String s, boolean fillEmpty) {
#ifdef SEGMENT7
  int len = s.length();
  if (len > 8) len = 8;
  char c;

  for (byte i=0; i<len; i++) {
    c = s.charAt(i);
    switch (c) {
      case 'A':
      case 'a':
        lc.setRow (0, 7-i, B01110111);
        break;
      case 'L':
      case 'l':
        lc.setRow (0, 7-i, B00001110);
        break;
      case 'N':
      case 'n':
        lc.setRow (0, 7-i, B00010101);
        break;
      case 'O':
      case 'o':
        lc.setRow (0, 7-i, B00011101);
        break;
      case 'P':
      case 'p':
        lc.setRow (0, 7-i, B01100111);
        break;
      case 'R':
      case 'r':
        lc.setRow (0, 7-i, B00000101);
        break;
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

  if (fillEmpty) {
    // fill rest with empty space
    for (byte i=len; i<8; i++) {
      lc.setRow(0, 7-i, 0);
    }
  }
#endif
#ifdef MATRIX32
  _text = s;
  redrawDisplay();
#endif
}

#ifdef MATRIX32
void redrawDisplay() {
  //matrix.clear();
  matrix.firstPage();
  do {
    matrix.setFont(u8g2_font_5x8_tr);
    matrix.drawStr(0, 7, _text.c_str());
    if (_progress > 0)   matrix.drawLine(0, 7, (_progress*4)-1, 7);
    if (_dot)            matrix.drawPixel(31,7);
  } while ( matrix.nextPage() );
}
#endif 

void initDisplay() {
#ifdef SEGMENT7
  // wakeup 7 segment display
  lc.shutdown(0, false);
  // set brightness
  lc.setIntensity(0, 6);
#endif
#ifdef MATRIX32
  matrix.begin();
  matrix.setContrast(70);
#endif
}

void clearDisplay() {
#ifdef SEGMENT7
  // clear all digits
  setAll(' ', false, 0, 8);
#endif
#ifdef MATRIX32
  matrix.clear();
#endif
}

void setProgress(byte progress) {
#ifdef SEGMENT7
  // set first dot... etc
  for (byte i=0; i<8; i++) {
    lc.setLed(0, 7-i, 0, i<progress);
  }
#endif
#ifdef MATRIX32
  _progress = progress;
  redrawDisplay();
#endif
}

void flashDotTrade() {
  bool flashdot;
  
  if(last && ((long)(millis() - timeout_flashing_dot) >= 0)) {
    flashdot = false;
  } else {
    flashdot = true;
  }
  
#ifdef SEGMENT7
  lc.setLed(0, 0, 0, flashdot);
#endif
#ifdef MATRIX32
  if (_dot != flashdot) {
    _dot = flashdot;
    redrawDisplay();
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
