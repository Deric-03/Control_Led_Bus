/*
  Control_Led.h -- pilotage direct d'un ruban.

  Un point blanc parcourt le ruban, suivi d'une traine bleue. Montre le
  choix du profil de puce et l'usage non bloquant de show().
*/

#include <Control_Led.h>

const int PIN_STRIP = 10;   // a adapter a ta carte
const int NB_LED    = 60;

Debug debug;
StripLed strip;

int pos = 0;
unsigned long lastStep = 0;
bool pending = false;

void setup() {
  debug.Init(true);

  // Profil de puce en dernier argument : ledSK6812W (defaut), ledSK6812,
  // ledWS2812B, ledWS2815, ledWS2811.
  strip.init(PIN_STRIP, NB_LED, &debug, &ledSK6812W);
}

void loop() {

  // 25 pas par seconde.
  if (millis() - lastStep >= 40) {
    lastStep = millis();

    strip.clear();
    strip.setPixel(pos, 255, 255, 255);
    strip.setPixel((pos + NB_LED - 1) % NB_LED, 0, 0, strip.gamma8(64));

    pos = (pos + 1) % NB_LED;
    pending = true;
  }

  // show() ne bloque jamais : s'il renvoie false, le bus est encore occupe
  // et on retente au passage suivant.
  if (pending && strip.show()) pending = false;
}
