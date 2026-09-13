#pragma once
#include "../third_party/esp_dmx/esp_dmx.h"
#include <Esp_Lite_Core.h>
#include "CSource.h"

class CDmx : public CSource {
private:

  bool init_ = false;

  Debug* debug = nullptr;
  bool Adebug = false;

  void print (const String &deb) {
    if (!Adebug) return;
    debug->Print(deb);
  }

  dmx_port_t DMX_PORT = DMX_NUM_1;
  int TX_PIN  = 3; 
  int RX_PIN  = 1;               
  int DERE_PIN = 6;

  // Double buffer : tick() ecrit dans back, getChanel() lit front.
  // L'echange de pointeur permet de faire tourner tick() dans une autre
  // tache que le rendu sans verrou et sans trame dechiree.
  uint8_t bufA[DMX_PACKET_SIZE];
  uint8_t bufB[DMX_PACKET_SIZE];
  uint8_t* volatile front = bufA;
  uint8_t* back = bufB;

  volatile bool dmx_present = false;
  volatile unsigned long last_dmx_time = 0;

  volatile bool newFrame = false;   // levee a chaque trame valide, consommee par hasNewFrame()

  // Emission : buffer separe de la reception, ecrit par write() et envoye par tick().
  // Pas de verrou : comme getChanel(), chaque octet est ecrit/lu atomiquement,
  // donc un write() concurrent a un tick() d'envoi ne peut jamais corrompre la
  // trame -- au pire un canal part avec une frame de retard, jamais une valeur
  // invalide.
  bool txMode = false;
  uint8_t txBuf[DMX_PACKET_SIZE];

  // Publie l'image recue et repart de son contenu, pour qu'une trame plus
  // courte que la precedente garde les dernieres valeurs connues au-dela.
  void publish() {
    uint8_t* tmp = front;
    front = back;   // ecriture de pointeur alignee = atomique en 32 bits
    back = tmp;

    memcpy(back, front, DMX_PACKET_SIZE);
    newFrame = true;
  }

public:

  // rx et tx sont obligatoires : la classe sait recevoir et emettre. dir
  // peut valoir -1 quand le sens du transceiver est gere en dur. esp_dmx
  // verifie le reste (broche existante, capable de sortie...).
  void init(int rx, int tx, int dir, Debug* deb = nullptr) {
    if (init_) return;

    if (deb) {
      debug = deb;
      Adebug = true;
    }

    if (rx < 0 || tx < 0) {
      print("Dmx : broche rx ou tx manquante");
      return;
    }

    TX_PIN  = tx;
    RX_PIN  = rx;
    DERE_PIN = dir;

    memset(bufA, 0, sizeof(bufA));
    memset(bufB, 0, sizeof(bufB));
    memset(txBuf, 0, sizeof(txBuf));

    dmx_config_t cfg = DMX_CONFIG_DEFAULT; // 250k 8N2
    if (!dmx_driver_install(DMX_PORT, &cfg, nullptr, 0)) {
      print("Dmx : echec installation driver");
      return;
    }

    if (!dmx_set_pin(DMX_PORT, TX_PIN, RX_PIN, DERE_PIN)) {
      print("Dmx : broches invalides");
      dmx_driver_delete(DMX_PORT);   // libere le port pour un nouvel essai
      return;
    }

    print("Init Dmx Ok");
    print("Rx pin : " + String(RX_PIN));
    print("Tx pin : " + String(TX_PIN));
    print("Dir pin : " + String(DERE_PIN));

    init_ = true;
  }

  bool state() override {
    if (!init_) return false;
    if (!dmx_present) return false;
    return true;
  }

  // Vrai une seule fois par trame recue : la lecture consomme le drapeau.
  bool hasNewFrame() override {
    if (!newFrame) return false;
    newFrame = false;
    return true;
  }

  // Bascule entre reception (defaut) et emission. En emission, tick() envoie
  // txBuf a chaque appel des que le port est libre, au lieu d'ecouter le bus.
  void setSendMode(bool send) {
    if (!init_) return;
    txMode = send;
  }

  // Ecrit un canal du buffer d'emission. Accepte aussi en reception : rien
  // ne part tant que setSendMode(true) n'est pas appele, ce qui permet de
  // preparer la trame avant de basculer.
  void write(int ch, uint8_t value) {
    if (!init_) return;
    if (ch < 1 || ch > 512) return;
    txBuf[ch] = value;
  }

  void tick() override {
    if (!init_) return;

    if (txMode) {
      // En emission on n'ecoute plus le bus : sans ca, state() resterait
      // fige sur la derniere presence constatee avant la bascule. Fait ici
      // plutot que dans setSendMode() pour rester dans la tache de tick().
      if (dmx_present) {
        print("DMX : passage en emission");
        dmx_present = false;
      }

      if (!dmx_wait_sent(DMX_PORT, 0)) return;   // envoi precedent pas encore termine
      dmx_write(DMX_PORT, txBuf, DMX_PACKET_SIZE);
      dmx_send(DMX_PORT);
      return;
    }

    dmx_packet_t pkt;
    int size = dmx_receive(DMX_PORT, &pkt, 0);

    // Seules les trames de niveaux valides sont retenues : les paquets RDM et
    // les trames en erreur ne doivent pas etre lus comme des niveaux de LED.
    if (size) {
      last_dmx_time = millis();
      if (dmx_present != true) {
        print("DMX detecter");
        dmx_present = true;
      }

      // Presence deja actee : on jette le paquet, pas l'emetteur.
      if (pkt.err != DMX_OK) return;
      if (pkt.sc != DMX_SC) return;   // start code nul = niveaux, 0xCC = RDM

      dmx_read(DMX_PORT, back, size);
      publish();

      return;
    }

    if (dmx_present != false && millis() - last_dmx_time > 500) {
      print("DMX perdu");
      dmx_present = false;
    }
  }

  int getMaxChanel() override { return 512; }

  uint8_t getChanel(int ch, bool autoStop = false) override {
    if (!init_) return 0;
    if (!dmx_present && autoStop) return 0;
    if (ch < 1 || ch > 512) return 0;
    return front[ch];
  }

};


