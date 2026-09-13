#pragma once
#include <Arduino.h>
#include <Esp_Lite_Core.h>
#include "Color.h"

enum Form {
  Sinus,
  Pwm4,
  Ramp,
  RampPlus,
  RampMinus
};

struct Align { int from = 0; int to = 0; };

struct Courbe {
  Form form = Sinus;
  int width = 100;
  int attack = 0;
  int decay = 0;
};

// Etat complet d'un effet, recopie d'un bloc par le rendu : une image ne
// melange jamais deux reglages, meme si l'UI les change en cours de calcul.
struct EffectState {
  Courbe courbe;
  Align align;
  Color highValue;
  Color lowValue;
  int speed = 60;
  bool run = false;
  unsigned long t0 = 0;     // origine du cycle, en millis()
};

/*
  Parametres d'un effet. Chaque methode prend le verrou de l'objet : on peut
  les appeler depuis une autre tache que celle du rendu.

  L'origine du cycle vit ici et non dans le rendu. Un meme effet pose sur
  plusieurs selections, ou sur plusieurs rubans, reste ainsi en phase
  partout, et reset() s'applique a toutes ses utilisations d'un coup.
*/
class Effect {
private:

  EffectState state;

  mutable SemaphoreHandle_t mtx = NULL;

public:

  Effect() { mtx = xSemaphoreCreateMutex(); }

  ~Effect() { if (mtx) vSemaphoreDelete(mtx); }

  // Copie les reglages, pas le verrou : chaque instance garde le sien.
  Effect(const Effect& o) : Effect() { state = o.getState(); }

  Effect& operator=(const Effect& o) {
    if (this == &o) return *this;
    EffectState s = o.getState();
    MutexLock lock(mtx);
    state = s;
    return *this;
  }

  void setFrom(Form courb) { MutexLock lock(mtx); state.courbe.form = courb; }

  void setWidth(int width) { MutexLock lock(mtx); state.courbe.width = width; }

  void setAttack(int attack) { MutexLock lock(mtx); state.courbe.attack = attack; }

  void setDecay(int decay) { MutexLock lock(mtx); state.courbe.decay = decay; }

  void setCourbe(Form courb, int width, int attack, int decay) {

    MutexLock lock(mtx);
    state.courbe.form = courb;
    state.courbe.width = width;
    state.courbe.attack = attack;
    state.courbe.decay = decay;

  }

  void setHighValue(Color color) { MutexLock lock(mtx); state.highValue = color; }

  void setHighValue(int r, int g, int b, int w) { setHighValue(Color{r, g, b, w}); }

  void setLowValue(Color color) { MutexLock lock(mtx); state.lowValue = color; }

  void setLowValue(int r, int g, int b, int w) { setLowValue(Color{r, g, b, w}); }

  void setAlign(int from, int to) {
    MutexLock lock(mtx);
    state.align.from = from;
    state.align.to = to;
  }

  void setAlign(int al) { setAlign(al, al); }

  void setSpeed(int spee) { MutexLock lock(mtx); state.speed = spee; }

  // Front montant : l'effet repart du debut de son cycle. Un start(true)
  // sur un effet deja lance ne change rien.
  void start(bool st) {
    MutexLock lock(mtx);
    if (st && !state.run) state.t0 = millis();
    state.run = st;
  }

  // Ramene le cycle a son origine, pour toutes les selections qui utilisent
  // cet effet.
  void reset() { MutexLock lock(mtx); state.t0 = millis(); }

  Courbe getCourbe() const { MutexLock lock(mtx); return state.courbe; }

  Align getAlign() const { MutexLock lock(mtx); return state.align; }

  Color getHighValue() const { MutexLock lock(mtx); return state.highValue; }

  Color getLowValue() const { MutexLock lock(mtx); return state.lowValue; }

  int getSpeed() const { MutexLock lock(mtx); return state.speed; }

  bool getRun() const { MutexLock lock(mtx); return state.run; }

  EffectState getState() const { MutexLock lock(mtx); return state; }

};
