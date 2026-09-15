/*
  Control_Link.h -- une source DMX ou Art-Net pilote plusieurs rubans.

  StripDmx ne connait que l'interface CSource : passer de l'Art-Net au DMX
  physique ne change que la declaration de la source.

  Chaque ruban RGBW consomme 4 canaux par zone. A group = 1 un ruban de 64
  LEDs occupe donc 256 canaux ; l'espace de canaux est continu, un ruban
  peut chevaucher deux univers.
*/

#include <Control_Link.h>

// 1 = Art-Net en WiFi, 0 = DMX physique
#define SOURCE_ARTNET 1

const int PIN_A = 10;       // a adapter a ta carte
const int PIN_B = 5;

Debug debug;
StripLed stripA;
StripLed stripB;
StripDmx link;

#if SOURCE_ARTNET
CWifi wifi;
CArtnet source;
#else
CDmx source;
const int PIN_RX  = 1;
const int PIN_TX  = 3;
const int PIN_DIR = 6;
#endif

void setup() {
  debug.Init(true);

  stripA.init(PIN_A, 64, &debug);
  stripB.init(PIN_B, 64, &debug);

#if SOURCE_ARTNET
  wifi.initHOST("ControlLedBus");
  wifi.initSSID("MonReseau");         // a adapter
  wifi.initPASSWORD("MotDePasse");
  wifi.initWifi(&debug);
  source.init(&debug, 0, 1);          // univers 0
#else
  source.init(PIN_RX, PIN_TX, PIN_DIR, &debug);
#endif

  link.init(&source);

  // Les index suivent l'ordre des appels : A = 0, B = 1.
  link.addStrip(&stripA, 1);          // canaux 1 a 256
  link.addStrip(&stripB, 257);        // canaux 257 a 512

  // Sur B, deux LEDs par zone : 32 zones, 128 canaux seulement.
  link.setPixGroup(1, 2);

  link.setSmooth(64);
  link.setGamma(true);

  debug.Print("Dernier canal : " + String(link.getLastChanel())
              + " / " + String(source.getMaxChanel()));
}

void loop() {
#if SOURCE_ARTNET
  wifi.connectTick();
#endif
  source.tick();
  link.tick();
}
