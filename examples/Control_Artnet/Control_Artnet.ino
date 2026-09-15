/*
  Control_Artnet.h -- reception Art-Net en WiFi.

  Affiche les canaux 1 a 4 de l'univers 0 deux fois par seconde, tant que
  la console emet.

  La lib ne gere pas la connexion : CArtnet ecoute sur n'importe quelle
  interface reseau. Choisis ci-dessous comment te connecter.
*/

#include <Control_Artnet.h>

// 1 = CWifi (bibliotheque Network_Lite_Esp) : reconnexion geree, reglages
//     conserves en NVS.
// 0 = WiFi du core ESP32, rien a installer.
#define USE_CWIFI 1

#if USE_CWIFI
#include <Wifi_Lite_Esp.h>
#else
#include <WiFi.h>
#endif

const char* WIFI_SSID = "MonReseau";    // a adapter
const char* WIFI_PASS = "MotDePasse";

Debug debug;
CArtnet artnet;

#if USE_CWIFI
CWifi wifi;
#endif

unsigned long lastPrint = 0;

void setup() {
  debug.Init(true);

#if USE_CWIFI
  wifi.initHOST("ControlLedBus");
  wifi.initSSID(WIFI_SSID);
  wifi.initPASSWORD(WIFI_PASS);
  wifi.initWifi(&debug);              // DHCP
#else
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);               // la mise en veille du WiFi retarde les paquets
  WiFi.begin(WIFI_SSID, WIFI_PASS);   // le core se reconnecte seul
#endif

  // Univers de depart 0, un seul univers collecte (16 consecutifs au plus).
  // setUniverse() permet d'en changer a chaud, sans redemarrer.
  artnet.init(&debug, 0, 1);
}

void loop() {
#if USE_CWIFI
  wifi.connectTick();   // reconnexion automatique, non bloquant
#endif
  artnet.tick();        // la reception est asynchrone, ici on suit la presence

  if (artnet.state() && millis() - lastPrint >= 500) {
    lastPrint = millis();
    debug.Print("Canaux 1-4 : " + String(artnet.getChanel(1)) + " " + String(artnet.getChanel(2))
                + " " + String(artnet.getChanel(3)) + " " + String(artnet.getChanel(4)));
  }
}
