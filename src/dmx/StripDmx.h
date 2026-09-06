#pragma once
#include "../matricableLed/StripLed.h"
#include "../matricableLed/CSource.h"
#include <Esp_Lite_Core.h>
#include "soc/soc_caps.h"

/*
  Pilote plusieurs rubans depuis une meme source DMX / Art-Net.

  Chaque ruban occupe un slot avec sa propre adresse et son propre motif
  (group / space). Les reglages de rendu -- lissage, snap, gamma, cadence --
  sont volontairement globaux : ils decrivent le comportement du projecteur,
  pas celui d'un ruban en particulier.

  MAX_STRIP suit le nombre reel de canaux RMT en emission de la cible de
  compilation (SOC_RMT_TX_CANDIDATES_PER_GROUP) : 2 sur C3, 8 sur un ESP32
  classique, 4 sur S3...
*/

class StripDmx{
private:

  // Autant de rubans simultanes que de canaux RMT en emission sur la cible
  // de compilation (2 sur C3, 8 sur un ESP32 classique, 4 sur S3...).
  static const int MAX_STRIP = SOC_RMT_TX_CANDIDATES_PER_GROUP;

  struct StripSlot {
    StripLed* strip = nullptr;
    int addr  = 1;
    int group = 1;            // LEDs consecutives partageant la meme couleur
    int space = 0;            // LEDs eteintes entre deux groupes
    uint8_t*  targ = nullptr; // valeur DMX visee, 0-255 par canal
    uint16_t* cur  = nullptr; // valeur affichee, virgule fixe 8.8
    int nCh = 0;              // nombre de canaux bufferises
    bool pending = false;     // image calculee en attente d'envoi
  };

  bool init_ = false;

  CSource* dmx = nullptr;
  StripSlot slot[MAX_STRIP];

  SemaphoreHandle_t ParamMtx = NULL;

  unsigned long update = 0;
  int inter = 16;            // cadence de rendu, ~60 fps

  int alpha = 64;            // vitesse de rapprochement, 1 (lent) a 256 (immediat)
  int snap = 32;             // ecart DMX au-dela duquel on claque au lieu de lisser
  bool gamma = false;

  bool validIndex(int index) {
    if (index < 0 || index >= MAX_STRIP) return false;
    return slot[index].strip != nullptr;
  }

  void readSlot(int s, int Group, int Space, int Addr, int Snap) {

    StripSlot& sl = slot[s];
    int period = Group + Space;   // motif complet : groupe allume + espacement
    int n = 0;                    // index de la zone pilotee

    for (int start = 0; start < sl.strip->getStripSize(); start += period) {

      int c = n * 4;
      if (c + 3 >= sl.nCh) return;

      int dmx_i = (n * 4) + Addr;
      n++;

      for (int k = 0; k < 4; k++) {
        uint8_t nt = dmx->getChanel(dmx_i + k);

        // Le seuil porte sur le mouvement de la SOURCE d'une trame a l'autre,
        // et non sur le retard accumule par le lissage. Sinon un lissage lent
        // finit toujours par franchir le seuil et produit une dent de scie :
        // rampe lente, saut brutal, rampe lente, saut brutal.
        if (Snap > 0 && abs((int)nt - (int)sl.targ[c + k]) >= Snap) {
          sl.cur[c + k] = (uint16_t)nt << 8;
        }

        sl.targ[c + k] = nt;
      }
    }
  }

  // Rapproche une voie de sa cible. Les sauts francs sont deja traites en
  // amont par readSlot(), ici on ne fait plus que du lissage.
  void stepChannel(StripSlot& sl, int c, int Alpha) {

    int target = sl.targ[c] << 8;
    int diff = target - sl.cur[c];

    if (diff == 0) return;

    int move = (diff * Alpha) >> 8;
    if (move == 0) move = (diff > 0) ? 1 : -1;   // garantit la convergence
    sl.cur[c] += move;
  }

  void renderSlot(int s, int Group, int Space, int Alpha, bool Gamma) {

    StripSlot& sl = slot[s];
    int period = Group + Space;
    int size = sl.strip->getStripSize();
    int n = 0;

    for (int start = 0; start < size; start += period) {

      int c = n * 4;
      if (c + 3 >= sl.nCh) break;
      n++;

      stepChannel(sl, c,     Alpha);
      stepChannel(sl, c + 1, Alpha);
      stepChannel(sl, c + 2, Alpha);
      stepChannel(sl, c + 3, Alpha);

      uint8_t r = sl.cur[c] >> 8;
      uint8_t g = sl.cur[c + 1] >> 8;
      uint8_t b = sl.cur[c + 2] >> 8;
      uint8_t w = sl.cur[c + 3] >> 8;

      if (Gamma) {
        r = sl.strip->gamma8(r);
        g = sl.strip->gamma8(g);
        b = sl.strip->gamma8(b);
        w = sl.strip->gamma8(w);
      }

      // Le groupe recoit la couleur de la zone.
      for (int i = start; i < start + Group && i < size; i++) {
        sl.strip->setPixel(i, r, g, b, w);
      }

      // L'espacement est eteint explicitement : sinon les LEDs gardent
      // leur ancienne valeur quand group ou space change en cours de route.
      for (int i = start + Group; i < start + period && i < size; i++) {
        sl.strip->setPixel(i, 0, 0, 0, 0);
      }
    }
  }

public:

  void init(CSource* Dmx) {
    if (init_) return;
    if (!Dmx) return;

    dmx = Dmx;
    ParamMtx = xSemaphoreCreateMutex();

    init_ = true;
  }

  /*
    Attache un ruban a un slot. L'adresse est celle du premier canal du
    ruban ; chaque ruban a la sienne, ce qui permet de les enchainer ou de
    les superposer librement.
  */
  bool addStrip(StripLed* Strip, int index, int Addr) {
    if (!init_) return false;
    if (index < 0 || index >= MAX_STRIP) return false;
    if (!Strip) return false;
    if (slot[index].strip) return false;        // slot deja pris

    int size = Strip->getStripSize();
    if (size < 1) return false;

    MutexLock lock(ParamMtx);

    StripSlot& sl = slot[index];
    sl.nCh = size * 4;
    sl.targ = new uint8_t[sl.nCh]();
    sl.cur  = new uint16_t[sl.nCh]();
    if (!sl.targ || !sl.cur) return false;

    sl.strip = Strip;
    sl.addr  = (Addr >= 1 && Addr <= dmx->getMaxChanel()) ? Addr : 1;
    sl.group = 1;
    sl.space = 0;

    return true;
  }

  // ---- reglages par ruban -------------------------------------------

  void setAddr(int index, int Addr) {
    if (!validIndex(index)) return;
    if (Addr < 1) return;
    if (Addr > dmx->getMaxChanel()) return;
    MutexLock lock(ParamMtx);
    slot[index].addr = Addr;
  }

  int getAddr(int index) {
    if (!validIndex(index)) return 0;
    MutexLock lock(ParamMtx);
    int out = slot[index].addr;
    return out;
  }

  // Nombre de LEDs consecutives pilotees ensemble par une meme zone.
  void setPixGroup(int index, int Group) {
    if (!validIndex(index)) return;
    if (Group < 1) return;
    MutexLock lock(ParamMtx);
    slot[index].group = Group;
  }

  int getPixGroup(int index) {
    if (!validIndex(index)) return 0;
    MutexLock lock(ParamMtx);
    int out = slot[index].group;
    return out;
  }

  // Nombre de LEDs laissees eteintes entre deux groupes.
  void setPixSpace(int index, int Space) {
    if (!validIndex(index)) return;
    if (Space < 0) return;
    MutexLock lock(ParamMtx);
    slot[index].space = Space;
  }

  int getPixSpace(int index) {
    if (!validIndex(index)) return 0;
    MutexLock lock(ParamMtx);
    int out = slot[index].space;
    return out;
  }

  // Zones pilotees et canaux consommes par un ruban.
  int getZoneCount(int index) {
    if (!validIndex(index)) return 0;
    int period, size;
    {
      MutexLock lock(ParamMtx);
      period = slot[index].group + slot[index].space;
      size = slot[index].strip->getStripSize();
    }
    return (size + period - 1) / period;
  }

  int getFootprint(int index) { return getZoneCount(index) * 4; }

  // Dernier canal occupe, tous rubans confondus. A comparer au maximum
  // de la source pour verifier que la configuration tient.
  int getLastChanel() {
    if (!init_) return 0;
    int last = 0;
    for (int s = 0; s < MAX_STRIP; s++) {
      if (!slot[s].strip) continue;
      int end = getAddr(s) + getFootprint(s) - 1;
      if (end > last) last = end;
    }
    return last;
  }

  int getStripCount() {
    int n = 0;
    for (int s = 0; s < MAX_STRIP; s++) if (slot[s].strip) n++;
    return n;
  }

  // ---- reglages globaux ---------------------------------------------

  void setTickFps(int fps) {
    if (!init_) return;
    if (fps < 1) return;
    MutexLock lock(ParamMtx);
    inter = 1000 / fps;
  }

  int getTickFps() {
    if (!init_) return 0;
    MutexLock lock(ParamMtx);
    int out = (inter > 0) ? (1000 / inter) : 0;
    return out;
  }

  // 0 = aucun lissage (rendu direct), 255 = tres doux.
  void setSmooth(int level) {
    if (!init_) return;
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    MutexLock lock(ParamMtx);
    alpha = 256 - level;
    if (alpha < 1) alpha = 1;
  }

  int getSmooth() {
    if (!init_) return 0;
    MutexLock lock(ParamMtx);
    int out = 256 - alpha;
    return out;
  }

  // Ecart DMX (0-255) au-dela duquel on claque. 0 = ne claque jamais.
  void setSnap(int levels) {
    if (!init_) return;
    if (levels < 0) return;
    if (levels > 255) levels = 255;
    MutexLock lock(ParamMtx);
    snap = levels;
  }

  int getSnap() {
    if (!init_) return 0;
    MutexLock lock(ParamMtx);
    int out = snap;
    return out;
  }

  void setGamma(bool on) {
    if (!init_) return;
    MutexLock lock(ParamMtx);
    gamma = on;
  }

  bool getGamma() {
    if (!init_) return false;
    MutexLock lock(ParamMtx);
    bool out = gamma;
    return out;
  }

  // ---- boucle ---------------------------------------------------------

  void tick() {

    if (!init_) return;

    // Instantane des parametres : le verrou n'est jamais tenu pendant le
    // travail, seulement le temps de recopier quelques entiers.
    int Inter, Alpha, Snap;
    bool Gamma;
    int Group[MAX_STRIP], Space[MAX_STRIP], Addr[MAX_STRIP];
    {
      MutexLock lock(ParamMtx);
      Inter = inter;
      Alpha = alpha;
      Snap  = snap;
      Gamma = gamma;
      for (int s = 0; s < MAX_STRIP; s++) {
        Group[s] = slot[s].group;
        Space[s] = slot[s].space;
        Addr[s]  = slot[s].addr;
      }
    }

    if (dmx->hasNewFrame()) {
      for (int s = 0; s < MAX_STRIP; s++) {
        if (slot[s].strip) readSlot(s, Group[s], Space[s], Addr[s], Snap);
      }
    }

    if (Inter <= 0) return;

    // Le rendu suit la cadence demandee : c'est lui qui fait avancer le
    // lissage, il ne doit surtout pas tourner plus vite.
    if (millis() - update >= (unsigned long)Inter) {
      update = millis();
      for (int s = 0; s < MAX_STRIP; s++) {
        if (!slot[s].strip) continue;
        renderSlot(s, Group[s], Space[s], Alpha, Gamma);
        slot[s].pending = true;
      }
    }

    // L'envoi est retente a chaque passage jusqu'a ce que le bus se libere.
    // Sans ca, un show() refuse ferait attendre un intervalle complet et
    // diviserait la cadence reelle par deux.
    //
    // Le drapeau est par ruban : deux rubans de longueurs differentes ne se
    // liberent pas ensemble, et un drapeau global les ferait se reemettre
    // mutuellement en boucle sans jamais se synchroniser.
    for (int s = 0; s < MAX_STRIP; s++) {
      if (!slot[s].strip || !slot[s].pending) continue;
      if (slot[s].strip->show()) slot[s].pending = false;
    }
  }

};
