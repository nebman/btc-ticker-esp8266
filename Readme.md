# Bitcoin price ticker with ESP8266

* **COMING SOON:** Working on STL file for 3D printed case, maybe switch to more compact ESP-12E/F!
* **NEW:** bitstamp websocket interfacing for real time updates!
* cheap components (total cost ~6 USD)
* no soldering required (order display with pre-soldered pin headers)
* low power (<0.5W)
* wifi + easy config with WifiManager captive portal

## pictures in action
![animation](https://thumbs.gfycat.com/VainBeautifulAcornwoodpecker-size_restricted.gif)  
v0.2-bitstamp-websockets in action ([gfycat link](https://gfycat.com/gifs/detail/VainBeautifulAcornwoodpecker))

![picture](btc-ticker-esp8266.jpg)
displaying coindesk bpi BTCUSD

## components
* ESP8266 Wemos D1 R2 Uno ([link](https://www.aliexpress.com/item/WeMos-D1R2-analog-WiFi-D1-R2-integration-of-ESP8266-32Mb-flash-form-factor-for-Ard-Uno/32739176923.html)), other ESP8266s/NodeMCUs might work too
* 7 segment display with MAX7219 ([link](https://www.aliexpress.com/wholesale?catId=0&initiative_id=SB_20170409112750&SearchText=max7219+display))
* dupont cables (female-to-male) and a cheap antenna ;)

## wiring

ESP | Display
--- | ---
GND | GND
3.3 | VCC
D6  | CS
D7  | DIN
D8  | CLK

## how to install
- flash the board
  * upload source sketch with arduino IDE
  * or flash binary with [esptool (python)](https://github.com/espressif/esptool) or [flash download tools (WIN)](https://espressif.com/en/support/download/other-tools) @ address 0x0
- connect board to power 
- connect your smartphone/computer to ESPxxxxxx wifi
- enter your home wifi settings at the captive portal

## known issues

- compilation error in LedControl.h:  
solution: comment out or delete pgmspace.h include


## TODO

* ! correct level-shifting to 5V (3.3V is out of spec, but works anyway)
* ~~add 5th digit for next ATHs ;-)~~
* web portal configuration 
* ~~better error handling (although it seems to be pretty stable with good connection)~~
* ~~maybe use websockets for real-time ticker~~
* add more APIs and currencies plus option to choose
* ~~TLS support for HTTPS requests~~
* 3D printed case

