#pragma once
#include <Arduino.h>
#include "Select.h"
#include "Effect.h"
#include "Preset.h"
#include "../matricableLed/StripLed.h"

class ManualRender {
private:
  
  StripLed* strip;

  static const int MAX_RENDER = 30;

  Preset* preset[MAX_RENDER];

  int nbPreset = 0;

  Effect* effect[MAX_RENDER];

  int nbEffect = 0;

  unsigned long update = 0; //Fps Timer
  int inter = 16; //Frame Interval 60fps (ms)

public:
  
  void addStrip(StripLed* str) { strip = str; }
  
  void addPreset(Preset* pres) {

    if (nbPreset >= MAX_RENDER) return;

    preset[nbPreset] = pres;
    nbPreset++;
  }

  int getNbPreset() { return nbPreset; }

  void addEffect(Effect* eff) {

    if (nbEffect >= MAX_RENDER) return;

    effect[nbEffect] = eff;
    nbEffect++;
  }

  int getNbEffect() { return nbEffect; }

  void setTickFps(int fps) {

    if (fps < 1) return;

    inter = 1000 / fps;

  }

  void tick() {

  }

};


