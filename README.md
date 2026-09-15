# Control_Led_Bus

Pilotage de rubans LED adressables sur ESP32, depuis une console DMX, un réseau Art-Net, ou un générateur d'effets intégré.

Pensée pour le spectacle : sortie non bloquante, plusieurs rubans en parallèle, lissage des niveaux, et un mode manuel inspiré du moteur d'effets grandMA2 pour jouer des looks sans console.

## Fonctionnalités

### Sortie LED

- Pilotage par le périphérique **RMT** : `show()` ne bloque jamais, l'image part pendant que le code continue.
- **Plusieurs rubans en parallèle**, autant que de canaux RMT en émission : 8 sur ESP32, 4 sur ESP32-S3, 2 sur ESP32-C3.
- Profils de puces fournis : SK6812 RGBW, SK6812, WS2812B, WS2815, WS2811. Une puce non listée s'ajoute en héritant de `LedType`.
- Correction gamma intégrée.

### Sources DMX et Art-Net

- **DMX512 physique** via un transceiver RS485, en réception comme en émission. Les trames RDM et les trames en erreur sont écartées.
- **Art-Net** : jusqu'à 16 univers consécutifs, modifiables à chaud sans redémarrer. Les paquets en retard ou dupliqués sont écartés, la présence et la perte de la console sont détectées.
- À la perte de la source : maintien de la dernière image (*hold last look*) ou noir, au choix.
- Les deux sources partagent l'interface `CSource` : le reste du code ignore le protocole utilisé.

### Liaison source → rubans (`StripDmx`)

- Chaque ruban a sa propre adresse dans un espace de canaux continu : un ruban peut chevaucher plusieurs univers.
- Regroupement de LEDs par zone, y compris **fractionnaire** (131 LEDs réparties en 20 zones alternant 7 et 6), avec espacement réglable entre les zones.
- Lissage des niveaux, seuil de saut franc (*snap*), gamma et cadence de rendu réglables.

### Mode manuel

- `Preset` : une couleur fixe.
- `Effect` : un effet paramétrique à la manière de grandMA2 — formes sinus, PWM et rampes, vitesse en cycles par minute, largeur, attack et decay, déphasage étalé sur la sélection (*phase from → to*), couleurs haute et basse.
- `Select` : une liste de LEDs, par LED ou par plage, mélangeable avec `shuffle()`.
- `ManualRender` : cumule presets et effets en HTP. Plusieurs rubans forment un espace d'index unique : un effet traverse les coupures, et deux rubans placés au même offset sont en miroir.
- Toutes les méthodes peuvent être appelées depuis une autre tâche que celle du rendu.

## Compatibilité

- ESP32, ESP32-S3, ESP32-C3… avec le core **arduino-esp32 3.x**.
- Dépend de la bibliothèque [Esp_Lite_Core](https://github.com/Deric-03/Esp_Lite_Core).
- [esp_dmx](https://github.com/someweisguy/esp_dmx) est intégré dans `src/third_party/`, rien à installer à part.
- La connexion réseau est laissée au sketch : l'Art-Net écoute sur n'importe quelle interface, WiFi comme Ethernet. Les exemples utilisent le WiFi du core ESP32.

## Quel en-tête inclure

| En-tête | Contenu |
|---|---|
| `Control_Led.h` | `StripLed` et les profils de puces, seuls |
| `Control_Dmx.h` | la source DMX physique `CDmx` |
| `Control_Artnet.h` | la source Art-Net `CArtnet` |
| `Control_Link.h` | rubans, sources DMX / Art-Net et `StripDmx` |
| `Control_Manual.h` | rubans et mode manuel |
| `Control_Led_Bus.h` | toute la bibliothèque |

## Exemple

Une vague rouge qui parcourt un ruban de 60 LEDs, sans aucune source externe :

```cpp
#include <Control_Manual.h>

Debug debug;
StripLed strip;
ManualRender render;
Effect vague;

void setup() {
  debug.Init(true);
  strip.init(10, 60, &debug);          // broche 10, 60 LEDs SK6812 RGBW

  render.addStrip(&strip);

  Select* tout = new Select(60);
  tout->select(0, 59);

  vague.setHighValue(255, 0, 0, 0);
  vague.setSpeed(30);                  // un cycle toutes les 2 s
  vague.setAlign(0, 360);              // la phase s'etale sur tout le ruban
  vague.start(true);

  render.addEffect(&vague, tout);
}

void loop() {
  render.tick();
}
```

Le dossier [`examples/`](examples) contient un exemple complet par en-tête, dont un sketch qui suit l'Art-Net et bascule sur un effet local quand la console se tait.

## Licence

MIT — voir [LICENSE](LICENSE). esp_dmx est distribué sous sa propre licence MIT, dans `src/third_party/esp_dmx/LICENSE`.
