#pragma once
#include <Arduino.h>
#include <Esp_Lite_Core.h>

// Liste ordonnee de LEDs. Protegee par un verrou : on peut la modifier
// depuis une autre tache pendant que le rendu la parcourt.
class Select {
private:

  static const int MAX_LED  = 512;

  int nbSelect = 0;

  uint16_t buf[MAX_LED];

  mutable SemaphoreHandle_t mtx = NULL;

public:

  Select() { mtx = xSemaphoreCreateMutex(); }

  ~Select() { if (mtx) vSemaphoreDelete(mtx); }

  // Copie la liste, pas le verrou : chaque instance garde le sien. L'objet
  // neuf n'est encore connu de personne, seul celui de la source compte.
  Select(const Select& o) : Select() {
    MutexLock lock(o.mtx);
    nbSelect = o.nbSelect;
    memcpy(buf, o.buf, nbSelect * sizeof(uint16_t));
  }

  Select& operator=(const Select& o) {
    if (this == &o) return *this;

    // Deux verrous, toujours pris dans le meme ordre (par adresse) : sinon
    // a = b et b = a lances en parallele s'interbloqueraient.
    bool thisFirst = (uintptr_t)this < (uintptr_t)&o;
    MutexLock l1(thisFirst ? mtx : o.mtx);
    MutexLock l2(thisFirst ? o.mtx : mtx);

    nbSelect = o.nbSelect;
    memcpy(buf, o.buf, nbSelect * sizeof(uint16_t));
    return *this;
  }

  int getMaxLed() const { return MAX_LED; }

  void clear() { MutexLock lock(mtx); nbSelect = 0; }

  void select(int led) {
    MutexLock lock(mtx);
    if (led < 0 || led >= MAX_LED) { nbSelect = 0; return; }
    buf[0] = led;
    nbSelect = 1;
  }

  void select(int led, int thru) {
    MutexLock lock(mtx);
    if (led < 0 || thru >= MAX_LED || thru < led) { nbSelect = 0; return; }

    int nb = thru - led + 1;
    for (int i = 0; i < nb; i++) buf[i] = led + i;
    nbSelect = nb;
  }

  void shuffle() {
    MutexLock lock(mtx);
    for (int i = nbSelect - 1; i > 0; i--) {
      int j = random(i + 1);
      uint16_t tmp = buf[i];
      buf[i] = buf[j];
      buf[j] = tmp;
    }
  }

  int count() const { MutexLock lock(mtx); return nbSelect; }

  int getSelect(int index) const {
    MutexLock lock(mtx);
    if (index < 0 || index >= nbSelect) return -1;
    return buf[index];
  }

  /*
    Parcourt la selection sous verrou : fn(k, nb, led) est appelee pour
    chaque LED, k etant son rang et nb la taille de la selection.

    Le rendu passe par ici plutot que par count() / getSelect() : la liste
    reste coherente d'un bout a l'autre de l'image, et le verrou n'est pris
    qu'une fois au lieu d'une fois par LED.

    fn ne doit pas rappeler cette Select : le verrou n'est pas recursif.
  */
  template <typename F>
  void forEach(F fn) const {
    MutexLock lock(mtx);
    for (int k = 0; k < nbSelect; k++) fn(k, nbSelect, (int)buf[k]);
  }

};
