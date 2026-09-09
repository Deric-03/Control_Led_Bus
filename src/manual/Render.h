#pragma once
#include <Arduino.h>
#include "Select.h"
#include "Effect.h"
#include "Preset.h"
#include "../matricableLed/StripLed.h"

/*
  Moteur de rendu du mode manuel.

  Un preset ou un effet ne sait pas sur quelles LEDs il agit : c'est le
  couple (source, Select) enregistre ici qui le decide. Plusieurs couples
  peuvent viser les memes LEDs, ils se cumulent en HTP (la valeur la plus
  forte gagne, canal par canal) -- l'ordre d'ajout n'a donc aucune influence
  sur le resultat.

  Le rendu passe par un accumulateur avant d'atteindre le ruban : sans lui,
  le HTP demanderait de relire les pixels deja ecrits, ce que StripLed
  n'expose pas.

  Unites :
    speed  -- cycles par minute (60 = un cycle par seconde), negatif = sens
              inverse
    align  -- dephasage en degres, etale de from a to sur la selection ; to
              est le point de bouclage, donc 0 a 360 se repartit sans doublon
    width  -- part du cycle passee a la valeur haute, en %
    attack / decay -- montee et descente, en % de width (convention MA2) et
              ajoutees autour d'elle : width 20 + decay 100 donne 20 % du cycle
              en haut, 20 % de descente, 60 % en bas

  attack et decay s'appliquent a toutes les formes, sinus compris, ou ils
  penchent la courbe. MA2 les reserve aux formes a fronts durs : ecart assume.
*/

struct RenderPreset {
  Preset* preset = nullptr;
  Select* select = nullptr;
};

struct RenderEffect {
  Effect* effect = nullptr;
  Select* select = nullptr;
  unsigned long t0 = 0;     // origine du cycle
  bool wasRun = false;      // etat precedent, pour detecter le front de start()
};

class ManualRender {
private:

  StripLed* strip = nullptr;

  static const int MAX_RENDER = 30;

  RenderPreset preset[MAX_RENDER];

  int nbPreset = 0;

  RenderEffect effect[MAX_RENDER];

  int nbEffect = 0;

  unsigned long update = 0; //Fps Timer
  int inter = 16; //Frame Interval 60fps (ms)

  bool pending = false;     // image calculee en attente d'envoi

  // Image en construction, 4 octets par LED (r, g, b, w).
  uint8_t* acc = nullptr;
  int accSize = 0;

  // Un sinus par LED et par image coute trop cher sur C3, qui n'a pas de FPU.
  uint16_t sinTable[256];

  void buildSinus() {
    for (int i = 0; i < 256; i++) {
      sinTable[i] = (uint16_t)(32767.5f * (1.0f - cosf(i * (2.0f * PI / 256.0f))) + 0.5f);
    }
  }

  // ---- courbe ---------------------------------------------------------

  // Forme brute sur un cycle complet, en 16 bits a l'entree comme a la
  // sortie : 256 paliers ne suffisent pas quand un cycle dure 30 s.
  uint16_t formAt(Form f, uint16_t x) {
    switch (f) {
      case Sinus: {
        // La table reste a 256 entrees, on interpole entre deux voisines.
        uint8_t  i = (uint8_t)(x >> 8);
        uint16_t frac = x & 0xFF;
        int32_t  a = sinTable[i];
        int32_t  b = sinTable[(uint8_t)(i + 1)];
        return (uint16_t)(a + ((b - a) * frac) / 256);
      }
      case Pwm4:      return 65535;   // creneau plein : width donne la duree, attack/decay les bords
      case Ramp:      return (x < 32768) ? (uint16_t)(x * 2) : (uint16_t)((65535 - x) * 2);
      case RampPlus:  return x;
      case RampMinus: return (uint16_t)(65535 - x);
    }
    return 0;
  }

  uint8_t curveAt(const Courbe& c, uint16_t x) {

    uint32_t win = (65536UL * constrain(c.width, 0, 100)) / 100;

    // MA2 exprime attack et decay en pourcentage de la LARGEUR, pas du cycle :
    // decay 100 avec width 20 fait durer la descente 20 % du cycle, pas 80 %.
    uint32_t a = (win * constrain(c.attack, 0, 100)) / 100;
    uint32_t d = (win * constrain(c.decay,  0, 100)) / 100;

    // Elles s'ajoutent autour de la fenetre haute sans la rogner. Si elles
    // debordent du cycle, on les reduit a proportion.
    uint32_t rest = 65536UL - win;
    if (a + d > rest) {
      uint32_t s = a + d;
      a = (a * rest) / s;
      d = (d * rest) / s;
    }

    uint32_t span = a + win + d;          // duree active, le reste est eteint
    if (span == 0 || x >= span) return 0;

    uint32_t p = ((uint32_t)x * 65535UL) / span;
    uint32_t v = formAt(c.form, (uint16_t)p);

    // v, x et span tiennent sur 16 bits : les produits remplissent tout juste
    // un uint32, ne pas elargir les bornes sans revoir ce calcul.
    if (a > 0 && x < a)             v = (v * x) / a;
    else if (d > 0 && x >= a + win) v = (v * (span - x)) / d;

    return (uint8_t)(v >> 8);
  }

  Color mixColor(const Color& lo, const Color& hi, uint8_t v) {
    Color out;
    out.r = lo.r + ((hi.r - lo.r) * v) / 255;
    out.g = lo.g + ((hi.g - lo.g) * v) / 255;
    out.b = lo.b + ((hi.b - lo.b) * v) / 255;
    out.w = lo.w + ((hi.w - lo.w) * v) / 255;
    return out;
  }

  // ---- accumulateur ---------------------------------------------------

  void htp(uint8_t& dst, int v) {
    if (v <= dst) return;
    dst = (v > 255) ? 255 : (uint8_t)v;
  }

  void accWrite(int led, const Color& c) {
    if (led < 0 || (led * 4 + 3) >= accSize) return;   // -1 = index hors selection
    uint8_t* p = acc + (led * 4);
    htp(p[0], c.r);
    htp(p[1], c.g);
    htp(p[2], c.b);
    htp(p[3], c.w);
  }

  // ---- rendu ----------------------------------------------------------

  void renderPreset(RenderPreset& rp) {
    if (!rp.preset || !rp.select) return;

    Color c = rp.preset->getColor();
    int nb = rp.select->count();

    for (int k = 0; k < nb; k++) accWrite(rp.select->getSelect(k), c);
  }

  void renderEffect(RenderEffect& re, unsigned long now) {
    if (!re.effect || !re.select) return;

    bool run = re.effect->getRun();

    // Front montant de start() : l'effet demarre au debut de son cycle.
    if (run && !re.wasRun) re.t0 = now;
    re.wasRun = run;

    // Le drapeau se consomme meme a l'arret, sinon un reset() demande
    // pendant la pause s'appliquerait au redemarrage suivant.
    if (re.effect->hasReset()) re.t0 = now;
    if (!run) return;

    int speed = re.effect->getSpeed();
    if (speed == 0) return;

    unsigned long period = 60000UL / (unsigned long)abs(speed);
    if (period == 0) period = 1;

    uint16_t t = (uint16_t)(((now - re.t0) % period) * 65536UL / period);
    if (speed < 0) t = (uint16_t)(65535 - t);

    Courbe c  = re.effect->getCourbe();
    Align  al = re.effect->getAlign();
    Color  lo = re.effect->getLowValue();
    Color  hi = re.effect->getHighValue();

    int nb = re.select->count();
    int span = al.to - al.from;

    for (int k = 0; k < nb; k++) {

      // Ecart divise par le NOMBRE de LEDs, pas par nb - 1 : to est le point
      // de bouclage, donc 0 a 360 se repartit sans doublon (comme MA2).
      int deg = al.from + (span * k) / nb;
      deg %= 360;
      if (deg < 0) deg += 360;
      uint16_t ph = (uint16_t)(((uint32_t)deg * 65536UL) / 360);

      // Phase sur 16 bits : le rebouclage est le debordement du uint16_t.
      uint8_t v = curveAt(c, (uint16_t)(t + ph));
      accWrite(re.select->getSelect(k), mixColor(lo, hi, v));
    }
  }

  void flush() {
    int n = accSize / 4;
    for (int i = 0; i < n; i++) {
      uint8_t* p = acc + (i * 4);
      strip->setPixel(i, p[0], p[1], p[2], p[3]);
    }
  }

public:

  bool addStrip(StripLed* str) {
    if (strip) return false;          // deja attache
    if (!str) return false;

    int n = str->getStripSize();
    if (n < 1) return false;

    accSize = n * 4;
    acc = new uint8_t[accSize]();
    if (!acc) {
      accSize = 0;
      return false;
    }

    buildSinus();

    strip = str;
    return true;
  }

  bool addPreset(Preset* pres, Select* select) {

    if (nbPreset >= MAX_RENDER) return false;
    if (!pres || !select) return false;

    preset[nbPreset].preset = pres;
    preset[nbPreset].select = select;
    nbPreset++;
    return true;
  }

  int getNbPreset() { return nbPreset; }

  bool addEffect(Effect* eff, Select* select) {

    if (nbEffect >= MAX_RENDER) return false;
    if (!eff || !select) return false;

    effect[nbEffect].effect = eff;
    effect[nbEffect].select = select;
    nbEffect++;
    return true;
  }

  int getNbEffect() { return nbEffect; }

  void setTickFps(int fps) {

    if (fps < 1) return;

    inter = 1000 / fps;

  }

  int getTickFps() { return (inter > 0) ? (1000 / inter) : 0; }

  void tick() {

    if (!strip || !acc) return;

    if (millis() - update >= (unsigned long)inter) {
      update = millis();

      // Tout est reconstruit a chaque image : une LED qui sort d'une
      // selection doit s'eteindre, pas garder sa derniere couleur.
      memset(acc, 0, accSize);

      for (int i = 0; i < nbPreset; i++) renderPreset(preset[i]);
      for (int i = 0; i < nbEffect;  i++) renderEffect(effect[i], update);

      flush();
      pending = true;
    }

    // Reessai a chaque passage jusqu'a ce que le RMT se libere : attendre
    // l'intervalle suivant diviserait la cadence reelle par deux.
    if (pending && strip->show()) pending = false;
  }

};
