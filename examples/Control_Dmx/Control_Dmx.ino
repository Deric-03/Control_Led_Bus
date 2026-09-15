/*
  Control_Dmx.h -- DMX physique via un transceiver RS485.

  En reception, affiche les canaux 1 a 4 deux fois par seconde. En emission,
  envoie une rampe sur le canal 1.
*/

#include <Control_Dmx.h>

// Broches du transceiver, a adapter a ta carte.
const int PIN_RX  = 1;
const int PIN_TX  = 3;
const int PIN_DIR = 6;

// 0 = reception, 1 = emission
#define EMISSION 0

Debug debug;
CDmx dmx;

unsigned long lastPrint = 0;

void setup() {
  debug.Init(true);
  dmx.init(PIN_RX, PIN_TX, PIN_DIR, &debug);

#if EMISSION
  dmx.setSendMode(true);
#endif
}

void loop() {

  // Recoit ou emet selon le mode, sans jamais bloquer.
  dmx.tick();

#if EMISSION
  dmx.write(1, (millis() / 10) & 0xFF);   // rampe 0-255 en ~2,5 s
#else
  if (dmx.state() && millis() - lastPrint >= 500) {
    lastPrint = millis();
    debug.Print("Canaux 1-4 : " + String(dmx.getChanel(1)) + " " + String(dmx.getChanel(2))
                + " " + String(dmx.getChanel(3)) + " " + String(dmx.getChanel(4)));
  }
#endif
}
