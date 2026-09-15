/*
  Control_Artnet.h -- reception Art-Net en WiFi.

  Affiche les canaux 1 a 4 de l'univers 0 deux fois par seconde, tant que
  la console emet.
*/

#include <Control_Artnet.h>

Debug debug;
CWifi wifi;
CArtnet artnet;

unsigned long lastPrint = 0;

void setup() {
  debug.Init(true);

  wifi.initHOST("ControlLedBus");
  wifi.initSSID("MonReseau");         // a adapter
  wifi.initPASSWORD("MotDePasse");
  wifi.initWifi(&debug);              // DHCP

  // Univers de depart 0, un seul univers collecte (16 consecutifs au plus).
  // setUniverse() permet d'en changer a chaud, sans redemarrer.
  artnet.init(&debug, 0, 1);
}

void loop() {
  wifi.connectTick();   // reconnexion automatique, non bloquant
  artnet.tick();        // la reception est asynchrone, ici on suit la presence

  if (artnet.state() && millis() - lastPrint >= 500) {
    lastPrint = millis();
    debug.Print("Canaux 1-4 : " + String(artnet.getChanel(1)) + " " + String(artnet.getChanel(2))
                + " " + String(artnet.getChanel(3)) + " " + String(artnet.getChanel(4)));
  }
}
