#pragma once
#include <Arduino.h>

class Select {
private:

  static const int MAX_LED  = 512;

  int nbSelect = 0;

  uint16_t buf[MAX_LED];

public:

  int getMaxLed() { return MAX_LED; }

  void clear() { nbSelect = 0;}

  void select(int led) {
    if (led < 0 || led >= MAX_LED) { clear(); return; }
    buf[0] = led;
    nbSelect = 1;
  }

  void select(int led, int thru) {
    if (led < 0 || thru >= MAX_LED || thru < led) { clear(); return; }

    int nb = thru - led + 1;
    for (int i = 0; i < nb; i++) buf[i] = led + i;
    nbSelect = nb;
  }

  void shuffle() {
    for (int i = nbSelect - 1; i > 0; i--) {
      int j = random(i + 1);
      uint16_t tmp = buf[i];
      buf[i] = buf[j];
      buf[j] = tmp;
    }
  }

  int count() { return nbSelect; }

  int getSelect(int index) { 
    if (index < 0 || index >= nbSelect) return -1;
    return buf[index]; 
  }

};