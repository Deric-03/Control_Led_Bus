# Code tiers integre

## esp_dmx

- Source : https://github.com/someweisguy/esp_dmx
- Version : 4.1.0
- Licence : MIT, voir `esp_dmx/LICENSE` (a conserver avec le code)
- Modifications par rapport a la 4.1.0 d'origine :
  1. Tous les `#include` internes, reecrits en chemins relatifs pour que le
     code compile depuis `src/third_party/` sans installer la bibliotheque
     esp_dmx a part.
  2. `dmx/hal/uart.c` : compatibilite ESP-IDF >= 5.3 (core Arduino ESP32
     3.1 et suivants). Le champ `module` de `uart_signal_conn_t` a disparu :
     la macro `DMX_UART_PERIPH_MODULE(num)` le recalcule a partir du numero
     de port (`PERIPH_UART0_MODULE + num`) et remplace les 4 appels a
     `uart_periph_signal[num].module`.

  Rien d'autre n'a change : verifie par diff contre le tag v4.1.0.

Pour mettre a jour : recopier le `src/` d'une nouvelle version d'esp_dmx
dans `esp_dmx/`, refaire la reecriture des `#include`, puis verifier si le
correctif de `uart.c` est encore necessaire (il l'est tant que le depot
d'origine utilise `uart_periph_signal[].module`).
