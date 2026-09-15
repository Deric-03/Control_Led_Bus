#pragma once
#include <Arduino.h>
#include <new>
#include <Esp_Lite_Core.h>
#include "Select.h"
#include "Effect.h"
#include "Preset.h"
#include "../matricableLed/StripLed.h"
#include "soc/soc_caps.h"


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

  Plusieurs rubans peuvent etre attaches. Ils partagent alors un espace
  d'index unique, comme les rubans de StripDmx partagent l'espace de canaux :
  celui attache a l'offset 100 recoit les index 100 et suivants. Une Select
  ignore donc les coupures, et un effet balaye plusieurs rubans d'un seul
  tenant. Deux rubans au meme offset sont en miroir.

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

  Taches : toutes les methodes publiques peuvent etre appelees depuis une
  autre tache que celle de tick(). Au retour de removePreset(),
  removeEffect() ou clear(), le rendu ne lit plus les objets retires : on
  peut les detruire. Un objet encore enregistre ne doit jamais l'etre.
*/

struct RenderPreset {
  Preset* preset = nullptr;
  Select* select = nullptr;
};

struct RenderEffect {
  Effect* effect = nullptr;
  Select* select = nullptr;
};

struct RenderStrip {
  StripLed* strip = nullptr;
  int offset = 0;           // index global de sa premiere LED
  int size   = 0;
  bool pending = false;     // image calculee en attente d'envoi
};

class ManualRender {
private:

  // Autant de rubans que de canaux RMT en emission sur la cible, comme
  // StripDmx : 2 sur C3, 8 sur un ESP32 classique, 4 sur S3...
  static const int MAX_STRIP = SOC_RMT_TX_CANDIDATES_PER_GROUP;

  RenderStrip stripSlot[MAX_STRIP];

  static const int MAX_RENDER = 30;

  RenderPreset preset[MAX_RENDER];

  int nbPreset = 0;

  RenderEffect effect[MAX_RENDER];

  int nbEffect = 0;

  SemaphoreHandle_t ParamMtx = NULL;

  unsigned long update = 0; //Fps Timer
  int inter = 16; //Frame Interval 60fps (ms)

  bool gamma = false;

  // Image en construction, 4 octets par LED (r, g, b, w). Elle couvre tout
  // l'espace d'index, de 0 a la derniere LED du ruban le plus eloigne.
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

  // Fin de l'espace d'index, tous rubans confondus. Sans verrou : les
  // appelants le tiennent deja.
  int spanLocked() const {
    int end = 0;
    for (int s = 0; s < MAX_STRIP; s++) {
      if (!stripSlot[s].strip) continue;
      int e = stripSlot[s].offset + stripSlot[s].size;
      if (e > end) end = e;
    }
    return end;
  }

  // ---- rendu ----------------------------------------------------------

  void renderPreset(RenderPreset& rp) {
    if (!rp.preset || !rp.select) return;

    Color c = rp.preset->getColor();
    rp.select->forEach([&](int, int, int led) { accWrite(led, c); });
  }

  void renderEffect(RenderEffect& re) {
    if (!re.effect || !re.select) return;

    EffectState e = re.effect->getState();
    if (!e.run || e.speed == 0) return;

    unsigned long period = 60000UL / (unsigned long)abs(e.speed);
    if (period == 0) period = 1;

    // millis() est lu APRES l'instantane : un start() ou un reset() arrive
    // d'une autre tache juste avant aurait sinon une origine dans le futur,
    // et l'ecart non signe vaudrait ~4 milliards.
    unsigned long now = millis();
    uint16_t t = (uint16_t)(((now - e.t0) % period) * 65536UL / period);
    if (e.speed < 0) t = (uint16_t)(65535 - t);

    int span = e.align.to - e.align.from;

    re.select->forEach([&](int k, int nb, int led) {

      // Ecart divise par le NOMBRE de LEDs, pas par nb - 1 : to est le point
      // de bouclage, donc 0 a 360 se repartit sans doublon (comme MA2).
      int deg = e.align.from + (span * k) / nb;
      deg %= 360;
      if (deg < 0) deg += 360;
      uint16_t ph = (uint16_t)(((uint32_t)deg * 65536UL) / 360);

      // Phase sur 16 bits : le rebouclage est le debordement du uint16_t.
      uint8_t v = curveAt(e.courbe, (uint16_t)(t + ph));
      accWrite(led, mixColor(e.lowValue, e.highValue, v));
    });
  }

  // Le gamma s'applique apres la fusion HTP : une fois par LED, et non une
  // fois par couche. La courbe etant croissante, le resultat est le meme.
  void flush(bool Gamma) {
    for (int s = 0; s < MAX_STRIP; s++) {

      RenderStrip& rs = stripSlot[s];
      if (!rs.strip) continue;

      for (int i = 0; i < rs.size; i++) {
        int g = rs.offset + i;
        if ((g * 4 + 3) >= accSize) break;

        uint8_t* p = acc + (g * 4);
        if (Gamma) {
          rs.strip->setPixel(i, rs.strip->gamma8(p[0]), rs.strip->gamma8(p[1]),
                                rs.strip->gamma8(p[2]), rs.strip->gamma8(p[3]));
        } else {
          rs.strip->setPixel(i, p[0], p[1], p[2], p[3]);
        }
      }

      rs.pending = true;
    }
  }

public:

  ManualRender() {
    ParamMtx = xSemaphoreCreateMutex();
    buildSinus();
  }


  ~ManualRender() {
    delete[] acc;
    if (ParamMtx) vSemaphoreDelete(ParamMtx);
  }

  // Un rendu possede son accumulateur et pointe vers des rubans : le copier
  // n'a pas de sens.
  ManualRender(const ManualRender&) = delete;
  ManualRender& operator=(const ManualRender&) = delete;

  // Forme courante : les rubans s'enchainent dans l'ordre des appels, le
  // premier a l'index 0 et a l'offset 0, le suivant juste derriere.
  bool addStrip(StripLed* str) { return addStrip(str, -1, -1); }

  /*
    Forme complete. index vise un slot precis, -1 prend le premier libre.

    Offset est l'index global de la premiere LED du ruban, l'equivalent de
    l'adresse DMX dans StripDmx ; -1 le place a la suite du plus eloigne.
    Le renseigner ne sert qu'a deux choses : laisser un trou dans l'espace
    d'index, ou donner le meme offset a deux rubans pour qu'ils affichent
    la meme chose.
  */
  bool addStrip(StripLed* str, int index, int Offset) {
    if (!str) return false;
    if (index >= MAX_STRIP) return false;

    int n = str->getStripSize();
    if (n < 1) return false;

    MutexLock lock(ParamMtx);

    if (index < 0) {
      index = 0;
      while (index < MAX_STRIP && stripSlot[index].strip) index++;
      if (index >= MAX_STRIP) return false;
    }

    if (stripSlot[index].strip) return false;   // slot deja pris

    int ofs = (Offset < 0) ? spanLocked() : Offset;

    // L'accumulateur couvre tout l'espace d'index, il grandit donc avec le
    // ruban le plus eloigne. flush() le lit sous ce meme verrou, la
    // reallocation est sans danger pour le rendu.
    int span = spanLocked();
    if (ofs + n > span) span = ofs + n;

    if (span * 4 > accSize) {
      uint8_t* na = new (std::nothrow) uint8_t[span * 4]();   // voir StripLed::init()
      if (!na) return false;
      delete[] acc;
      acc = na;
      accSize = span * 4;
    }

    // Le pointeur est ecrit en dernier : la boucle d'envoi de tick() lit les
    // slots hors verrou, elle doit voir soit nullptr, soit un slot complet.
    stripSlot[index].offset  = ofs;
    stripSlot[index].size    = n;
    stripSlot[index].pending = false;
    stripSlot[index].strip   = str;
    return true;
  }

  int getStripCount() {
    MutexLock lock(ParamMtx);
    int n = 0;
    for (int s = 0; s < MAX_STRIP; s++) if (stripSlot[s].strip) n++;
    return n;
  }

  // Taille de l'espace d'index. C'est la capacite a donner a une Select qui
  // doit pouvoir designer n'importe quelle LED du rig.
  int getSpan() {
    MutexLock lock(ParamMtx);
    return spanLocked();
  }

  bool addPreset(Preset* pres, Select* select) {

    if (!pres || !select) return false;

    MutexLock lock(ParamMtx);
    if (nbPreset >= MAX_RENDER) return false;

    preset[nbPreset].preset = pres;
    preset[nbPreset].select = select;
    nbPreset++;
    return true;
  }

  /*
    Retire un preset du rendu : sans select, de toutes ses selections ; avec,
    de ce couple seulement. Renvoie false si rien n'a ete trouve.
  */
  bool removePreset(Preset* pres, Select* select = nullptr) {
    if (!pres) return false;
    MutexLock lock(ParamMtx);

    // On tasse la table en ne gardant que ce qui ne correspond pas.
    int kept = 0;
    for (int i = 0; i < nbPreset; i++) {
      bool hit = (preset[i].preset == pres) && (!select || preset[i].select == select);
      if (!hit) preset[kept++] = preset[i];
    }

    bool found = (kept != nbPreset);
    nbPreset = kept;
    return found;
  }

  int getNbPreset() {
    MutexLock lock(ParamMtx);
    int out = nbPreset;
    return out;
  }

  bool addEffect(Effect* eff, Select* select) {

    if (!eff || !select) return false;

    MutexLock lock(ParamMtx);
    if (nbEffect >= MAX_RENDER) return false;

    effect[nbEffect].effect = eff;
    effect[nbEffect].select = select;
    nbEffect++;
    return true;
  }

  // Meme regle que removePreset().
  bool removeEffect(Effect* eff, Select* select = nullptr) {
    if (!eff) return false;
    MutexLock lock(ParamMtx);

    int kept = 0;
    for (int i = 0; i < nbEffect; i++) {
      bool hit = (effect[i].effect == eff) && (!select || effect[i].select == select);
      if (!hit) effect[kept++] = effect[i];
    }

    bool found = (kept != nbEffect);
    nbEffect = kept;
    return found;
  }

  int getNbEffect() {
    MutexLock lock(ParamMtx);
    int out = nbEffect;
    return out;
  }

  // Retire tous les presets et effets : le ruban s'eteint a l'image suivante.
  void clear() {
    MutexLock lock(ParamMtx);
    nbPreset = 0;
    nbEffect = 0;
  }

  void setTickFps(int fps) {

    if (fps < 1) return;
    if (fps > 1000) fps = 1000;   // au-dela, l'intervalle tomberait a 0 ms

    MutexLock lock(ParamMtx);
    inter = 1000 / fps;

  }

  int getTickFps() {
    MutexLock lock(ParamMtx);
    int out = (inter > 0) ? (1000 / inter) : 0;
    return out;
  }

  // Corrige la reponse lineaire du PWM, comme StripDmx::setGamma().
  void setGamma(bool on) {
    MutexLock lock(ParamMtx);
    gamma = on;
  }

  bool getGamma() {
    MutexLock lock(ParamMtx);
    bool out = gamma;
    return out;
  }

  void tick() {

    int Inter;
    bool Gamma;
    {
      MutexLock lock(ParamMtx);
      Inter = inter;
      Gamma = gamma;
    }

    if (millis() - update >= (unsigned long)Inter) {
      update = millis();

      {
        // A la difference de StripDmx, le verrou couvre tout le calcul de
        // l'image : les tables contiennent des pointeurs, et removeEffect()
        // doit garantir a son retour que l'objet retire n'est plus lu.
        // flush() est dedans aussi, parce que addStrip() peut reallouer
        // l'accumulateur. Seul l'envoi reste hors verrou.
        MutexLock lock(ParamMtx);

        if (acc) {
          // Tout est reconstruit a chaque image : une LED qui sort d'une
          // selection doit s'eteindre, pas garder sa derniere couleur.
          memset(acc, 0, accSize);

          for (int i = 0; i < nbPreset; i++) renderPreset(preset[i]);
          for (int i = 0; i < nbEffect;  i++) renderEffect(effect[i]);

          flush(Gamma);
        }
      }
    }

    // Reessai a chaque passage jusqu'a ce que le RMT se libere : attendre
    // l'intervalle suivant diviserait la cadence reelle par deux.
    //
    // Le drapeau est par ruban : deux rubans de longueurs differentes ne se
    // liberent pas ensemble, et un drapeau global les ferait se reemettre
    // mutuellement en boucle sans jamais se synchroniser.
    for (int s = 0; s < MAX_STRIP; s++) {
      RenderStrip& rs = stripSlot[s];
      if (!rs.strip || !rs.pending) continue;
      if (rs.strip->show()) rs.pending = false;
    }
  }

};
