#pragma once
#include <Arduino.h>
#include "Color.h"

class Preset {
private:
    
  Color color;

public:

  void setColor(Color col) {
    color = col;
  }

  void setColor(int r, int g, int b, int w) {
    color.r = r;
    color.g = g;
    color.b = b;
    color.w = w;
  }

  Color getColor() { return color; }

};


