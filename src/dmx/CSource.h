#pragma once
#include <Arduino.h>

// Interface commune aux sources de niveaux : DMX physique, Art-Net, ...
// Permet a StripDmx d'ignorer completement le protocole utilise.
class CSource {
public:

  virtual ~CSource() {}

  virtual void tick() = 0;
  virtual bool state() = 0;
  virtual bool hasNewFrame() = 0;
  // autoStop : true = noir des que la source est perdue,
  //            false = on garde la derniere valeur recue (hold last look).
  virtual uint8_t getChanel(int ch, bool autoStop = false) = 0;

  // Dernier canal adressable : 512 en DMX, count * 512 en Art-Net.
  virtual int getMaxChanel() = 0;

};
