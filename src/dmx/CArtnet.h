#pragma once
#include <WiFi.h>
#include <AsyncUDP.h>
#include <Esp_Lite_Core.h>
#include "CSource.h"

class CArtnet : public CSource {
private:

  bool init_ = false;

  Debug* debug = nullptr;
  bool Adebug = false;

  void print (const String &deb) {
    if (!Adebug) return;
    debug->Print(deb);
  }

  static const int ART_PORT = 6454;
  static const int UNI_SIZE = 512;
  static const int MAX_UNI  = 4;     // plage maximale bufferisee

  AsyncUDP udp;

  SemaphoreHandle_t ParamMtx = NULL;

  // Lus sans verrou par onPacket() : voir la remarque dans cette fonction.
  volatile uint16_t startUniverse = 0;   // Net << 8 | SubUni, 0 a 32767
  volatile int count = 1;                // nombre d'univers consecutifs collectes

  // Double buffer : la callback lwIP ecrit dans back, le rendu lit front.
  // L'echange de pointeur remplace le mutex.
  uint8_t bufA[MAX_UNI * UNI_SIZE + 1];
  uint8_t bufB[MAX_UNI * UNI_SIZE + 1];
  uint8_t* volatile front = bufA;
  uint8_t* back = bufB;

  uint8_t lastSeq[MAX_UNI] = {0};
  bool    seqInit[MAX_UNI] = {false};   // premier paquet vu pour cet univers
  uint8_t seqDrop[MAX_UNI] = {0};       // rejets consecutifs

  volatile bool newFrame = false;
  volatile bool art_present = false;
  volatile unsigned long last_art_time = 0;



  // Beaucoup de consoles n'emettent que sur changement. La spec Art-Net
  // prevoit une retransmission au moins toutes les 4 s : en dessous, la
  // source serait declaree perdue entre deux mouvements de faders.
  unsigned long timeout = 4000;

  // Publie l'image accumulee et repart de son contenu, pour que les univers
  // absents a la prochaine image gardent leur derniere valeur connue.
  void publish() {
    uint8_t* tmp = front;
    front = back;   // ecriture de pointeur alignee = atomique en 32 bits
    back = tmp;

    memcpy(back, front, sizeof(bufA));
    newFrame = true;
  }

  // Tourne dans la tache lwIP, en priorite haute : rester court.
  // Pas de show(), pas de delay(), pas de Serial verbeux ici.
  void onPacket(AsyncUDPPacket &packet) {

    uint8_t* d = packet.data();
    size_t len = packet.length();

    if (len < 18) return;
    if (memcmp(d, "Art-Net", 8) != 0) return;

    uint16_t op = d[8] | (d[9] << 8);          // little endian
    if (op != 0x5000) return;                  // pas de l'ArtDMX

    // Volontairement sans MutexLock : on tourne dans la tache lwIP, et
    // attendre un verrou detenu par l'UI bloquerait la pile reseau. On
    // recopie les deux valeurs une fois ; au pire un paquet est mal filtre
    // pendant le changement d'univers, jamais d'ecriture hors buffer
    // puisque idx reste borne par Count, lui-meme <= MAX_UNI.
    uint16_t Start = startUniverse;
    int Count = count;

    uint16_t uni = d[14] | (d[15] << 8);
    if (uni < Start) return;
    int idx = uni - Start;
    if (idx >= Count || idx >= MAX_UNI) return;   // hors de notre plage

    uint16_t dlen = (d[16] << 8) | d[17];      // big endian
    if (dlen > UNI_SIZE) dlen = UNI_SIZE;
    if (18 + dlen > len) return;               // paquet tronque

    // L'UDP ne garantit ni l'ordre ni l'unicite. Le compteur est propre a
    // chaque univers. Le cast en int8_t gere le rebouclage de 255 a 1.
    //
    // Deux garde-fous indispensables :
    //  - le PREMIER paquet d'un univers est toujours accepte. Sinon, une
    //    console deja en route depuis longtemps arrive avec un numero > 127,
    //    la comparaison le juge "plus ancien" que la valeur initiale 0, et
    //    l'univers reste muet jusqu'au rebouclage du compteur.
    //  - apres 20 rejets d'affilee on se resynchronise, pour survivre a un
    //    redemarrage du compteur cote console.
    uint8_t seq = d[12];
    if (seq != 0 && seqInit[idx]) {
      if ((int8_t)(seq - lastSeq[idx]) <= 0) {
        if (++seqDrop[idx] < 20) return;
      }
    }
    seqDrop[idx] = 0;
    lastSeq[idx] = seq;
    seqInit[idx] = true;

    // Les univers sont ranges bout a bout, canal 1 en indice 1.
    uint8_t* slot = back + 1 + (idx * UNI_SIZE);
    memcpy(slot, d + 18, dlen);
    if (dlen < UNI_SIZE) memset(slot + dlen, 0, UNI_SIZE - dlen);

    last_art_time = millis();

    // Publication immediate, sans attendre les autres univers.
    //
    // Attendre l'image complete introduisait une latence non bornee : a la
    // fin d'un fade, la console envoie sa derniere valeur puis se tait, et
    // une image partielle restait invisible jusqu'au paquet de maintien
    // suivant -- une seconde plus tard. Le ruban se figeait donc juste
    // au-dessus de zero avant de s'eteindre d'un coup.
    //
    // publish() recopiant l'image publiee vers le tampon d'accumulation,
    // les univers non recus conservent leur derniere valeur : au pire un
    // univers a une trame de retard sur l'autre, ce qui est invisible.
    publish();
  }

public:

  void init(Debug* deb = nullptr, uint16_t StartUniverse = 0, int Count = 1) {
    if (init_) return;
    if (Count < 1 || Count > MAX_UNI) return;
    if ((int)StartUniverse + Count - 1 > 32767) return;

    startUniverse = StartUniverse;
    count = Count;

    memset(bufA, 0, sizeof(bufA));
    memset(bufB, 0, sizeof(bufB));

    ParamMtx = xSemaphoreCreateMutex();

    if (!udp.listen(ART_PORT)) return;

    udp.onPacket([this](AsyncUDPPacket packet) { this->onPacket(packet); });

    if (deb) {
      debug = deb;
      Adebug = true;
      print("Init Artnet Ok");
      print("Univers : " + String(startUniverse) + " a " + String(startUniverse + count - 1));
      print("Canaux : " + String(count * UNI_SIZE));
      print("Port : " + String(ART_PORT));
    }

    init_ = true;
  }

  // Modifiable a chaud : le filtre s'applique des le paquet suivant.
  void setUniverse(uint16_t StartUniverse, int Count) {
    if (!init_) return;
    if (Count < 1 || Count > MAX_UNI) return;
    if ((int)StartUniverse + Count - 1 > 32767) return;

    MutexLock lock(ParamMtx);
    startUniverse = StartUniverse;
    count = Count;

    memset(lastSeq, 0, sizeof(lastSeq));
    memset(seqInit, 0, sizeof(seqInit));
    memset(seqDrop, 0, sizeof(seqDrop));
  }

  uint16_t getUniverse() {
    if (!init_) return 0;
    MutexLock lock(ParamMtx);
    uint16_t out = startUniverse;
    return out;
  }

  int getUniverseCount() {
    if (!init_) return 0;
    MutexLock lock(ParamMtx);
    int out = count;
    return out;
  }

  // Delai sans trame avant de declarer la source perdue.
  void setTimeout(unsigned long ms) {
    if (!init_) return;
    if (ms < 100) return;
    MutexLock lock(ParamMtx);
    timeout = ms;
  }

  unsigned long getTimeout() {
    if (!init_) return 0;
    MutexLock lock(ParamMtx);
    unsigned long out = timeout;
    return out;
  }

  int getMaxChanel() override {
    if (!init_) return UNI_SIZE;
    MutexLock lock(ParamMtx);
    int out = count * UNI_SIZE;
    return out;
  }

  bool state() override {
    if (!init_) return false;
    if (!art_present) return false;
    return true;
  }

  // Vrai une seule fois par image recue : la lecture consomme le drapeau.
  bool hasNewFrame() override {
    if (!newFrame) return false;
    newFrame = false;
    return true;
  }

  void tick() override {
    if (!init_) return;

    unsigned long Timeout;
    {
      MutexLock lock(ParamMtx);
      Timeout = timeout;
    }

    // La reception est asynchrone : ici on ne suit que la presence.
    // last_art_time est ecrit par la tache lwIP, qui nous preempte. Il faut
    // le lire AVANT millis() : dans l'autre ordre, un paquet arrivant entre
    // les deux lectures rend la difference negative, et l'arithmetique non
    // signee la transforme en ~4 milliards -> source declaree perdue alors
    // que les trames affluent.
    unsigned long last = last_art_time;
    unsigned long now = millis();

    bool alive = (last != 0) && ((long)(now - last) <= (long)Timeout);

    if (alive && !art_present) {
      print("Artnet detecter");
      art_present = true;
    } else if (!alive && art_present) {
      print("Artnet perdu");
      art_present = false;
    }
  }

  // Volontairement sans MutexLock : appelee des centaines de fois par
  // rendu. count est borne par MAX_UNI, donc la limite reste toujours
  // dans les buffers meme si l'UI la change en pleine lecture.
  uint8_t getChanel(int ch, bool autoStop = false) override {
    if (!init_) return 0;
    if (!art_present && autoStop) return 0;
    if (ch < 1 || ch > count * UNI_SIZE) return 0;
    return front[ch];
  }

};
