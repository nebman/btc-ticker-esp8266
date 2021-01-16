
struct exchange_settings {
  char* host;
  int   port;
  char* url;
  char* wsproto;
  char* subscribe;
};

/*

  // bitstamp USD BTC not working

  const exchange_settings bitstampUSDBTC = {
    "ws.pusherapp.com",
    80,
    "/app/de504dc5763aeef9ff52?protocol=7",
    "pusher",
    "{\"event\": \"pusher:subscribe\", \"data\": {\"channel\": \"live_trades\"}}"
  };

  // UNTESTED

  const exchange_settings bitstampEURBTC = {
    "ws.pusherapp.com",
    80,
    "/app/de504dc5763aeef9ff52",
    "pusher",
    "{\"event\": \"pusher:subscribe\", \"data\": {\"channel\": \"live_trades_btceur\"}}"
  };

  // UNTESTED

  const exchange_settings gdaxUSDBTC = {
    "ws-feed-public.sandbox.gdax.com",
    80,
    "/",
    "websocket",
    "{\"type\": \"subscribe\", \"channels\": [{\"name\": \"ticker\",\"product_ids\": [\"BTC-USD\"]}]}"
  };

*/

// bitfinex websocket WORKING

const exchange_settings bitfinexUSDBTC = {
  (char *)"api.bitfinex.com",
  443,
  (char *)"/ws/2",
  (char *)"websocket",
  (char *)"{\"event\":\"subscribe\",\"channel\":\"trades\",\"symbol\":\"tBTCUSD\" }"
};

