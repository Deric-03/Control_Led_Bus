#pragma once
#include <Arduino.h>
#include <Esp_Lite_Core.h>
#include "LedType.h"

/*
  Pilotage d'UN ruban de LEDs adressables via le peripherique RMT.

  Une instance = un ruban. Pour en piloter plusieurs, on en declare
  plusieurs ; la lib ne decide rien du decoupage.

  show() ne bloque JAMAIS. Deux tampons cohabitent :
    - pixels[] : l'etat vivant, ecrit librement par setPixel()
    - sym[]    : la trame gelee que le DMA est en train de sortir

  encode() prend un instantane de pixels[] au moment ou l'emission demarre,
  donc ce qui part est toujours la derniere image en date. Si le RMT est
  encore occupe, show() renvoie false et ne touche a rien : on continue a
  remplir pixels[], et la trame suivante partira des que le bus se libere.

  Deux rubans sortent ainsi EN PARALLELE sans effort :

      stripA.show();
      stripB.show();

  Mesure sur 2 x 256 LEDs RGBW : 9,7 ms au lieu de 19,5 ms en sequentiel.
  L'ESP32-C3 dispose de 2 canaux RMT en emission, donc 2 rubans au plus.
*/

class StripLed {
private:

  static const int RMT_FREQ = 10000000;   // 10 MHz -> 1 tick = 0,1 us

  bool init_ = false;

  Debug* debug = nullptr;
  bool Adebug = false;

  void print (const String &deb) {
    if (!Adebug) return;
    debug->Print(deb);
  }

  int pin = -1;
  int numLed = 0;

  // Profil de la puce, recopie a l'init pour eviter un appel virtuel
  // par pixel dans les boucles chaudes.
  LedType* ledType = nullptr;
  int latchUs = 300;
  int bytesPx = 4;
  int ofsR = 1, ofsG = 0, ofsB = 2, ofsW = 3;   // -1 = canal absent

  uint8_t* pixels = nullptr;      // etat vivant
  rmt_data_t* sym = nullptr;      // trame en cours d'emission
  int symCount = 0;

  uint32_t bit0 = 0;
  uint32_t bit1 = 0;

  bool busy = false;              // une trame est en cours d'emission
  unsigned long txEnd = 0;        // fin estimee de la trame en cours
  unsigned long txDurUs = 0;      // duree d'une trame complete
  int bitTicks = 12;              // duree d'un bit en ticks de 0,1 us

  uint8_t gammaTable[256];

  void applyType() {
    latchUs = ledType->Latch();
    bytesPx = ledType->Octets();
    ofsR = ledType->OfsR();
    ofsG = ledType->OfsG();
    ofsB = ledType->OfsB();
    ofsW = ledType->OfsW();

    bitTicks = ledType->T0H() + ledType->T0L();

    rmt_data_t s;
    s.level0 = 1; s.duration0 = ledType->T1H(); s.level1 = 0; s.duration1 = ledType->T1L();
    bit1 = s.val;
    s.level0 = 1; s.duration0 = ledType->T0H(); s.level1 = 0; s.duration1 = ledType->T0L();
    bit0 = s.val;
  }

  // Developpe les octets en symboles RMT, un symbole par bit.
  void encode() {
    rmt_data_t* dst = sym;
    int nBytes = numLed * bytesPx;

    for (int i = 0; i < nBytes; i++) {
      uint8_t v = pixels[i];
      for (int bit = 7; bit >= 0; bit--) {
        dst->val = (v & (1 << bit)) ? bit1 : bit0;
        dst++;
      }
    }
  }

public:

  void init(int Pin, int NumLed, Debug* deb = nullptr, LedType* type = &ledSK6812W) {

    if (init_) return;
    if (Pin < 0 || NumLed < 1 || !type) return;

    if (deb) {
      debug = deb;
      Adebug = true;
    }

    ledType = type;
    applyType();

    pin = Pin;
    numLed = NumLed;

    pixels = new uint8_t[numLed * bytesPx]();
    if (!pixels) {
      print("StripLed : echec alloc pixels");
      return;
    }

    symCount = numLed * bytesPx * 8;
    txDurUs = ((unsigned long)symCount * bitTicks) / 10;   // ticks de 0,1 us

    sym = new rmt_data_t[symCount];
    if (!sym) {
      print("StripLed : echec alloc RMT");
      return;
    }

    if (!rmtInit(pin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, RMT_FREQ)) {
      print("StripLed : rmtInit KO sur pin " + String(pin));
      return;
    }
    rmtSetEOT(pin, 0);   // ligne au repos a l'etat bas

    // Exposant 2.6, le meme qu'Adafruit. Calcule une fois plutot que
    // recopie, pour eviter toute erreur de transcription.
    for (int i = 0; i < 256; i++) {
      gammaTable[i] = (uint8_t)(powf((float)i / 255.0f, 2.6f) * 255.0f + 0.5f);
    }

    init_ = true;

    print("Init StripLed Ok");
    print("Type : " + String(ledType->Name()));
    print("Pin : " + String(pin) + ", " + String(numLed) + " LEDs");
    print("RAM RMT : " + String((symCount * 4) / 1024) + " ko");

    clear();
    show();
  }

  void setPixel(int index, uint8_t r, uint8_t g, uint8_t b) {
    setPixel(index, r, g, b, 0);
  }

  // Ecrit dans le tampon vivant. Jamais bloquant, meme pendant une emission :
  // le DMA lit sym[], pas pixels[].
  void setPixel(int index, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (!init_) return;
    if (index < 0 || index >= numLed) return;

    uint8_t* p = pixels + (index * bytesPx);
    p[ofsR] = r;
    p[ofsG] = g;
    p[ofsB] = b;
    if (ofsW >= 0) p[ofsW] = w;
  }

  void clear() {
    if (!pixels) return;
    memset(pixels, 0, numLed * bytesPx);
  }

  // Corrige la reponse lineaire du PWM vers la perception logarithmique.
  uint8_t gamma8(uint8_t v) { return gammaTable[v]; }

  /*
    Envoie l'etat courant si le bus est libre.
    Renvoie false si le RMT emet encore ou si le latch n'est pas ecoule :
    dans ce cas rien n'est touche, on retentera au tour suivant.
  */
  bool show() {
    if (!init_) return false;

    if (busy) {
      if (!rmtTransmitCompleted(pin)) return false;   // occupe, on passe
      busy = false;
    }

    // Le latch se compte depuis la FIN REELLE de la trame, calculee a
    // l'avance car le RMT est deterministe. Le compter depuis l'instant ou
    // l'on CONSTATE la fin ferait echouer un show() sur deux : l'ecart
    // vaudrait toujours zero.
    if ((long)(micros() - txEnd) < (long)latchUs) return false;

    encode();                            // instantane de pixels[] maintenant

    if (!rmtWriteAsync(pin, sym, symCount)) return false;   // rien n'est parti

    txEnd = micros() + txDurUs;
    busy = true;
    return true;
  }

  bool isBusy() { return busy; }

  int getStripSize() { return numLed; }
  int getBytesPx()   { return bytesPx; }
  int getPin()       { return pin; }
  LedType* getLedType() { return ledType; }

  // Pas de tick() ici a dessein : la cadence appartient a l'appelant.
  // En avoir un ici en plus de celui de StripDmx enverrait deux trames
  // concurrentes sur le meme ruban.

};
