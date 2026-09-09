#pragma once
#include <Arduino.h>

enum Form {
  Sinus,
  Pwm4,
  Ramp,
  RampPlus,
  RampMinus
};

struct Align { int from = 0; int to = 0; };

struct Color { int r = 0; int g = 0; int b = 0; int w = 0; };

struct Courbe {
  Form form = Sinus;
  int width = 100;
  int attack = 0;
  int decay = 0;
};

class Effect {
private:

  Courbe courbe;

  Align align;

  Color highValue;

  Color lowValue;

  int speed = 60;

  bool run = false;

  bool resetPending = false;

public:

  void setFrom(Form courb) { courbe.form = courb; }

  void setWidth(int width) { courbe.width = width; }

  void setAttack(int attack) { courbe.attack = attack; }

  void setDecay(int decay) { courbe.decay = decay; }

  void setCourbe(Form courb, int width, int attack, int decay) { 

    courbe.form = courb; 
    courbe.width = width;
    courbe.attack = attack;
    courbe.decay = decay;
    

  }

  void setHighValue(Color color) { highValue = color; }

  void setHighValue(int r, int g, int b, int w) {

    highValue.r = r;
    highValue.g = g;
    highValue.b = b;
    highValue.w = w;

  }

  void setLowValue(Color color) { lowValue = color; }

  void setLowValue(int r, int g, int b, int w) {

    lowValue.r = r;
    lowValue.g = g;
    lowValue.b = b;
    lowValue.w = w;

  }

  void setAlign(int from, int to) {
    align.from = from;
    align.to = to;
  }

  void setAlign(int al) {
    align.from = al;
    align.to = al;
  }

  void setSpeed(int spee) { speed = spee; }

  void start(bool st) { run = st; }

  void reset() { resetPending = true; }

  Courbe getCourbe() { return courbe; } 

  Align getAlign() { return align; }

  Color getHighValue() { return highValue; }

  Color getLowValue() { return lowValue; }

  int getSpeed() { return speed; }

protected:

  bool getRun() { return run; }

  bool hasReset() { 
    if (resetPending) {
      resetPending = false;
      return true;
    }
    return false;
  }
  
};