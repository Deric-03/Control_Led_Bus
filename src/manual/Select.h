#pragma once
#include <Arduino.h>
#include <new>
#include <Esp_Lite_Core.h>

/*
  Liste ordonnee de LEDs. Protegee par un verrou : on peut la modifier depuis
  une autre tache pendant que le rendu la parcourt.

  La capacite est le nombre de LEDs que la liste peut CONTENIR, pas l'index
  le plus grand qu'elle peut designer. Une selection de 20 LEDs au milieu
  d'un rig de 2000 coute 40 octets, pas 4 ko. Les index ne sont donc plus
  bornes ici : le rendu ecarte de lui-meme ceux qui tombent hors des rubans.
*/
class Select {
private:

  int cap = 0;              // fixe a la construction, ne change jamais
  int nbSelect = 0;

  uint16_t* buf = nullptr;

  mutable SemaphoreHandle_t mtx = NULL;

public:

  explicit Select(int capacity = 512) {
    mtx = xSemaphoreCreateMutex();
    if (capacity < 1) capacity = 1;
    buf = new (std::nothrow) uint16_t[capacity];   // voir StripLed::init()
    if (buf) cap = capacity;
  }

  ~Select() {
    delete[] buf;
    if (mtx) vSemaphoreDelete(mtx);
  }

  // Copie la liste, pas le verrou : chaque instance garde le sien. L'objet
  // neuf n'est encore connu de personne, seul celui de la source compte.
  // cap est fixe a la construction, le lire sans verrou est sans risque.
  Select(const Select& o) : Select(o.cap) {
    if (!buf) return;
    MutexLock lock(o.mtx);
    nbSelect = (o.nbSelect < cap) ? o.nbSelect : cap;
    memcpy(buf, o.buf, nbSelect * sizeof(uint16_t));
  }

  Select& operator=(const Select& o) {
    if (this == &o) return *this;
    if (!buf) return *this;

    // Deux verrous, toujours pris dans le meme ordre (par adresse) : sinon
    // a = b et b = a lances en parallele s'interbloqueraient.
    bool thisFirst = (uintptr_t)this < (uintptr_t)&o;
    MutexLock l1(thisFirst ? mtx : o.mtx);
    MutexLock l2(thisFirst ? o.mtx : mtx);

    // Les capacites peuvent differer : on ne garde que ce qui tient.
    nbSelect = (o.nbSelect < cap) ? o.nbSelect : cap;
    memcpy(buf, o.buf, nbSelect * sizeof(uint16_t));
    return *this;
  }

  int getCapacity() const { return cap; }

  void clear() { MutexLock lock(mtx); nbSelect = 0; }

  void select(int led) {
    MutexLock lock(mtx);
    nbSelect = 0;
    if (!buf || led < 0) return;
    buf[0] = (uint16_t)led;
    nbSelect = 1;
  }

  // Une plage plus longue que la capacite est refusee plutot que tronquee :
  // une selection amputee en silence se voit sur le ruban, pas dans le code.
  void select(int led, int thru) {
    MutexLock lock(mtx);
    nbSelect = 0;
    if (!buf || led < 0 || thru < led) return;

    int nb = thru - led + 1;
    if (nb > cap) return;

    for (int i = 0; i < nb; i++) buf[i] = (uint16_t)(led + i);
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
