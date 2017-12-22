
struct exchange_settings {
  char* host;
  int   port;
  char* url;
  char* wsproto;
  char* subscribe;
};


const exchange_settings bitstampUSDBTC = {
  "ws.pusherapp.com",
  80,
  "/app/de504dc5763aeef9ff52",
  "pusher",
  "{\"event\": \"pusher:subscribe\", \"data\": {\"channel\": \"live_trades\"}}"
};

const exchange_settings bitstampEURBTC = {
  "ws.pusherapp.com",
  80,
  "/app/de504dc5763aeef9ff52",
  "pusher",
  "{\"event\": \"pusher:subscribe\", \"data\": {\"channel\": \"live_trades_btceur\"}}"
};

const exchange_settings gdaxUSDBTC = {
  "ws-feed-public.sandbox.gdax.com",
  80,
  "/",
  "websocket",
  "{\"type\": \"subscribe\", \"channels\": [{\"name\": \"ticker\",\"product_ids\": [\"BTC-USD\"]}]}"
};


