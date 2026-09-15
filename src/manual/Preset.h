#pragma once
#include <Arduino.h>
#include <Esp_Lite_Core.h>
#include "Color.h"

// Une couleur fixe. Protegee par un verrou : modifiable depuis une autre
// tache que celle du rendu, sans qu'une image melange deux couleurs.
class Preset {
private:

  Color color;

  mutable SemaphoreHandle_t mtx = NULL;

public:

  Preset() { mtx = xSemaphoreCreateMutex(); }

  ~Preset() { if (mtx) vSemaphoreDelete(mtx); }

  // Copie la couleur, pas le verrou : chaque instance garde le sien.
  Preset(const Preset& o) : Preset() { color = o.getColor(); }

  Preset& operator=(const Preset& o) {
    if (this != &o) setColor(o.getColor());
    return *this;
  }

  void setColor(Color col) {
    MutexLock lock(mtx);
    color = col;
  }

  void setColor(int r, int g, int b, int w) { setColor(Color{r, g, b, w}); }

  Color getColor() const { MutexLock lock(mtx); return color; }

};
