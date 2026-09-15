#pragma once
#include <Arduino.h>

// L'ordre fixe aussi le nombre d'octets par pixel : 3 ou 4.
enum LedOrder {
  ORDER_GRB, ORDER_RGB, ORDER_BRG,
  ORDER_RBG, ORDER_GBR, ORDER_BGR,
  ORDER_GRBW, ORDER_RGBW
};

/*
  Description d'une puce adressable.

  Les durees sont en ticks de 0,1 us (resolution RMT a 10 MHz), le latch
  en microsecondes. La tolerance des puces est large (+/- 150 ns), d'ou des
  profils qui fonctionnent souvent au-dela du modele annonce.

  Pour une puce non listee, il suffit d'heriter de LedType : rien a
  modifier dans StripLed.
*/
class LedType {
public:

  virtual ~LedType() {}

  virtual int T0H() = 0;
  virtual int T0L() = 0;
  virtual int T1H() = 0;
  virtual int T1L() = 0;
  virtual int Latch() = 0;
  virtual LedOrder Order() = 0;
  virtual const char* Name() = 0;

  // Deduits, mais surchargeables si une puce sort de l'ordinaire.
  virtual int Periode() { return T0H() + T0L(); }

  virtual int Octets() {
    LedOrder o = Order();
    return (o == ORDER_GRBW || o == ORDER_RGBW) ? 4 : 3;
  }

  // Position de chaque couleur dans la trame. -1 = canal absent.
  virtual int OfsR() {
    switch (Order()) {
      case ORDER_RGB: case ORDER_RBG: case ORDER_RGBW: return 0;
      case ORDER_GRB: case ORDER_BRG: case ORDER_GRBW: return 1;
      default: return 2;
    }
  }

  virtual int OfsG() {
    switch (Order()) {
      case ORDER_GRB: case ORDER_GBR: case ORDER_GRBW: return 0;
      case ORDER_RGB: case ORDER_BGR: case ORDER_RGBW: return 1;
      default: return 2;
    }
  }

  virtual int OfsB() {
    switch (Order()) {
      case ORDER_BRG: case ORDER_BGR: return 0;
      case ORDER_RBG: case ORDER_GBR: return 1;
      default: return 2;
    }
  }

  virtual int OfsW() { return (Octets() == 4) ? 3 : -1; }
};


class WS2812B : public LedType {
public:
  int T0H() override { return 4; }
  int T0L() override { return 8; }
  int T1H() override { return 8; }
  int T1L() override { return 4; }
  int Latch() override { return 300; }
  LedOrder Order() override { return ORDER_GRB; }
  const char* Name() override { return "WS2812B"; }
};


class SK6812 : public LedType {
public:
  int T0H() override { return 3; }
  int T0L() override { return 9; }
  int T1H() override { return 6; }
  int T1L() override { return 6; }
  int Latch() override { return 80; }
  LedOrder Order() override { return ORDER_GRB; }
  const char* Name() override { return "SK6812"; }
};


class SK6812W : public SK6812 {
public:
  LedOrder Order() override { return ORDER_GRBW; }
  const char* Name() override { return "SK6812 RGBW"; }
};


class WS2815 : public LedType {
public:
  int T0H() override { return 3; }
  int T0L() override { return 9; }
  int T1H() override { return 9; }
  int T1L() override { return 3; }
  int Latch() override { return 300; }
  LedOrder Order() override { return ORDER_GRB; }
  const char* Name() override { return "WS2815"; }
};


class WS2811 : public LedType {
public:
  int T0H() override { return 5; }
  int T0L() override { return 20; }
  int T1H() override { return 12; }
  int T1L() override { return 13; }
  int Latch() override { return 300; }
  LedOrder Order() override { return ORDER_RGB; }
  const char* Name() override { return "WS2811 400kHz"; }
};


// Instances pretes a l'emploi : ces objets sont sans etat.
inline WS2812B ledWS2812B;
inline SK6812  ledSK6812;
inline SK6812W ledSK6812W;
inline WS2815  ledWS2815;
inline WS2811  ledWS2811;
