#pragma once
#include <new>
#include "../matricableLed/StripLed.h"
#include "CSource.h"
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
    float group = 1.0f;       // LEDs consecutives partageant la meme couleur
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

  /*
    Nombre de zones pour un motif donne.

    Group est fractionnaire : au lieu d'imposer une taille fixe et de laisser
    un reste en fin de ruban, on calcule combien de zones tiennent dans la
    longueur, puis on leur repartit les LEDs au plus juste. Avec 131 LEDs et
    Group = 6.55 on obtient 20 zones, alternant 7 et 6 LEDs.
  */
  int zonesFor(float Group, int Space, int size) {
    float period = Group + (float)Space;
    if (period <= 0.0f || size < 1) return 0;
    int z = (int)(size / period + 0.5f);
    if (z < 1) z = 1;
    if (z > size) z = size;
    return z;
  }

  /*
    Premiere LED de la zone n.

    Les bornes sont construites en miroir autour du centre du ruban : la
    seconde moitie est le reflet exact de la premiere. Deux consequences --
    le motif est symetrique vu depuis le milieu, et les tailles alternent
    au lieu de grouper les grandes zones d'un cote.

    L'arrondi ne peut etre parfait que si la longueur et le nombre de zones
    ont la meme parite : la somme des tailles d'un motif symetrique a nombre
    de zones pair est forcement paire. Quand ce n'est pas le cas, il reste
    une LED d'ecart, placee au centre plutot que laissee en bout de ruban.

      131 LEDs, 20 zones -> 7,6,7,6,7,6,7,6,7,6 | 7,7,6,7,6,7,6,7,6,7
      140 LEDs, 21 zones -> parfaitement symetrique
  */
  static int zoneStart(int n, int zones, int size) {
    if (n * 2 < zones) return (int)(((long)n * size + zones / 2) / zones);
    if (n * 2 > zones) return size - (int)(((long)(zones - n) * size + zones / 2) / zones);
    return size / 2;   // borne centrale, uniquement si zones est pair
  }

  void readSlot(int s, float Group, int Space, int Addr, int Snap) {

    StripSlot& sl = slot[s];
    int zones = zonesFor(Group, Space, sl.strip->getStripSize());

    for (int n = 0; n < zones; n++) {

      int c = n * 4;
      if (c + 3 >= sl.nCh) return;

      int dmx_i = (n * 4) + Addr;

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

  void renderSlot(int s, float Group, int Space, int Alpha, bool Gamma) {

    StripSlot& sl = slot[s];
    int size = sl.strip->getStripSize();
    int zones = zonesFor(Group, Space, size);
    float period = Group + (float)Space;

    for (int n = 0; n < zones; n++) {

      int c = n * 4;
      if (c + 3 >= sl.nCh) break;

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

      // Bornes de la zone. Leur largeur peut varier d'une LED d'une zone a
      // l'autre quand Group est fractionnaire, c'est le principe.
      int start = zoneStart(n, zones, size);
      int end   = zoneStart(n + 1, zones, size);
      int span  = end - start;
      if (span < 1) continue;

      // Part allumee de la zone, le reste servant d'espacement.
      int lit = span;
      if (Space > 0) {
        lit = (int)(span * Group / period + 0.5f);
        if (lit < 1) lit = 1;
        if (lit > span) lit = span;
      }

      for (int i = start; i < start + lit; i++) {
        sl.strip->setPixel(i, r, g, b, w);
      }

      // L'espacement est eteint explicitement : sinon les LEDs gardent
      // leur ancienne valeur quand group ou space change en cours de route.
      for (int i = start + lit; i < end; i++) {
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

    L'adresse n'est pas bornee par la source : un canal au-dela de ce
    qu'elle fournit est lu a 0, le ruban reste noir. L'ordre des init()
    n'a ainsi aucune importance (CArtnet annonce 512 canaux tant qu'il
    n'est pas initialise). getLastChanel() sert a verifier que la
    configuration tient dans la source.
  */
  bool addStrip(StripLed* Strip, int index, int Addr) {
    if (!init_) return false;
    if (index < 0 || index >= MAX_STRIP) return false;
    if (!Strip) return false;
    if (Addr < 1) return false;
    if (slot[index].strip) return false;        // slot deja pris

    int size = Strip->getStripSize();
    if (size < 1) return false;

    MutexLock lock(ParamMtx);

    // nothrow : voir StripLed::init(), un new classique planterait la carte.
    uint8_t*  targ = new (std::nothrow) uint8_t[size * 4]();
    uint16_t* cur  = new (std::nothrow) uint16_t[size * 4]();
    if (!targ || !cur) {
      delete[] targ;   // delete[] sur nullptr ne fait rien
      delete[] cur;
      return false;
    }

    StripSlot& sl = slot[index];
    sl.nCh  = size * 4;
    sl.targ = targ;
    sl.cur  = cur;

    sl.strip = Strip;
    sl.addr  = Addr;
    sl.group = 1.0f;
    sl.space = 0;

    return true;
  }

  // ---- reglages par ruban -------------------------------------------

  // Meme regle que addStrip() : pas de borne haute.
  void setAddr(int index, int Addr) {
    if (!validIndex(index)) return;
    if (Addr < 1) return;
    MutexLock lock(ParamMtx);
    slot[index].addr = Addr;
  }

  int getAddr(int index) {
    if (!validIndex(index)) return 0;
    MutexLock lock(ParamMtx);
    int out = slot[index].addr;
    return out;
  }

  /*
    Nombre de LEDs par zone. Accepte une valeur fractionnaire : les zones se
    repartissent alors les LEDs au plus juste, certaines en comptant une de
    plus que les autres, sans jamais laisser de reste en fin de ruban.

      setPixGroup(0, 7)      -> 140 LEDs = 20 zones de 7
      setPixGroup(0, 6.55f)  -> 131 LEDs = 20 zones alternant 7 et 6
  */
  void setPixGroup(int index, float Group) {
    if (!validIndex(index)) return;
    if (Group < 0.01f) return;
    MutexLock lock(ParamMtx);
    slot[index].group = Group;
  }

  float getPixGroup(int index) {
    if (!validIndex(index)) return 0.0f;
    MutexLock lock(ParamMtx);
    float out = slot[index].group;
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
    float Group;
    int Space, size;
    {
      MutexLock lock(ParamMtx);
      Group = slot[index].group;
      Space = slot[index].space;
      size  = slot[index].strip->getStripSize();
    }
    return zonesFor(Group, Space, size);
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
    if (fps > 1000) fps = 1000;   // au-dela, l'intervalle tomberait a 0 ms et tick() ne rendrait plus rien
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
    float Group[MAX_STRIP];
    int Space[MAX_STRIP], Addr[MAX_STRIP];
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
