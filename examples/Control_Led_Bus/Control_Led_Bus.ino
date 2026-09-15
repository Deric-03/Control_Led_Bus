/*
  Control_Led_Bus.h -- toute la bibliotheque.

  Le ruban suit l'Art-Net tant que la console emet. Si elle se tait, il
  bascule sur un effet de repli joue en local : une respiration bleue.
*/

#include <Control_Led_Bus.h>
#include <WiFi.h>

const int PIN_STRIP = 10;   // a adapter a ta carte

const char* WIFI_SSID = "MonReseau";    // a adapter
const char* WIFI_PASS = "MotDePasse";

Debug debug;
CArtnet artnet;

StripLed strip;
StripDmx link;
ManualRender manuel;

Effect attente;

void setup() {
  debug.Init(true);

  strip.init(PIN_STRIP, 64, &debug);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);               // la mise en veille du WiFi retarde les paquets
  WiFi.begin(WIFI_SSID, WIFI_PASS);   // le core se reconnecte seul

  artnet.init(&debug, 0, 1);

  // Mode Art-Net.
  link.init(&artnet);
  link.addStrip(&strip, 1);
  link.setGamma(true);

  // Mode de repli, sur le meme ruban.
  manuel.addStrip(&strip);

  Select* tout = new Select(manuel.getSpan());
  tout->select(0, manuel.getSpan() - 1);

  attente.setFrom(Sinus);
  attente.setHighValue(0, 0, 80, 0);
  attente.setSpeed(12);               // une respiration toutes les 5 s
  attente.setAlign(0);                // tout le ruban en phase
  attente.start(true);

  manuel.addEffect(&attente, tout);
  manuel.setGamma(true);
}

void loop() {
  artnet.tick();

  // Un seul moteur a la fois sur un meme ruban : deux tick() enverraient
  // deux images concurrentes. La source est declaree perdue apres 4 s sans
  // trame (setTimeout), delai prevu par la norme Art-Net.
  if (artnet.state()) link.tick();
  else                manuel.tick();
}
