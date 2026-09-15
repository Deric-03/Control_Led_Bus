/*
  Control_Artnet.h -- reception Art-Net en WiFi.

  Affiche les canaux 1 a 4 de l'univers 0 deux fois par seconde, tant que
  la console emet.

  La lib ne gere pas la connexion : CArtnet ecoute sur n'importe quelle
  interface reseau. Ici, le WiFi du core ESP32.
*/

#include <Control_Artnet.h>
#include <WiFi.h>

const char* WIFI_SSID = "MonReseau";    // a adapter
const char* WIFI_PASS = "MotDePasse";

Debug debug;
CArtnet artnet;

unsigned long lastPrint = 0;

void setup() {
  debug.Init(true);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);               // la mise en veille du WiFi retarde les paquets
  WiFi.begin(WIFI_SSID, WIFI_PASS);   // le core se reconnecte seul

  // Univers de depart 0, un seul univers collecte (16 consecutifs au plus).
  // setUniverse() permet d'en changer a chaud, sans redemarrer.
  artnet.init(&debug, 0, 1);
}

void loop() {
  artnet.tick();        // la reception est asynchrone, ici on suit la presence

  if (artnet.state() && millis() - lastPrint >= 500) {
    lastPrint = millis();
    debug.Print("Canaux 1-4 : " + String(artnet.getChanel(1)) + " " + String(artnet.getChanel(2))
                + " " + String(artnet.getChanel(3)) + " " + String(artnet.getChanel(4)));
  }
}
