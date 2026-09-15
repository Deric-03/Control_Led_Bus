/*
  Control_Manual.h -- couleurs fixes et effets, sans source externe.

  Deux rubans mis bout a bout forment un seul espace d'index. Les 10
  premieres LEDs recoivent un blanc doux fixe, et une comete rouge parcourt
  toutes les autres en traversant la coupure entre les deux rubans.
*/

#include <Control_Manual.h>

const int PIN_A = 10;       // a adapter a ta carte
const int PIN_B = 5;

Debug debug;
StripLed stripA;
StripLed stripB;
ManualRender render;

Preset fond;
Effect comete;

void setup() {
  debug.Init(true);

  stripA.init(PIN_A, 60, &debug);
  stripB.init(PIN_B, 60, &debug);

  render.addStrip(&stripA);           // index 0 a 59
  render.addStrip(&stripB);           // index 60 a 119, a la suite

  int span = render.getSpan();        // 120

  // La capacite d'une Select est le nombre de LEDs qu'elle contient, pas
  // l'index le plus grand qu'elle designe.
  Select* debut = new Select(10);
  debut->select(0, 9);

  Select* reste = new Select(span - 10);
  reste->select(10, span - 1);
  // reste->shuffle();                // meme effet, ordre aleatoire : scintillement

  fond.setColor(0, 0, 0, 60);

  comete.setFrom(Pwm4);
  comete.setWidth(20);
  comete.setDecay(100);
  comete.setAlign(0, 360);
  comete.setSpeed(20);                // un tour toutes les 3 s
  comete.setHighValue(baseRed);
  comete.start(true);

  render.addPreset(&fond, debut);
  render.addEffect(&comete, reste);
  render.setGamma(true);
}

void loop() {
  render.tick();
}
