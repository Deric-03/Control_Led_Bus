#pragma once

/*
  Point d'entree unique de la bibliotheque.

      #include <Control_Led_Bus.h>

  suffit : tout le reste est tire d'ici, l'organisation interne en
  sous-dossiers ne regarde pas l'utilisateur.

  Contenu :
    LedType   -- profils de puces adressables (SK6812, WS2812...)
    StripLed  -- pilotage d'un ruban via RMT, show() non bloquant
    CSource   -- interface commune aux sources de niveaux
    CDmx      -- source DMX physique (reception ou emission)
    CArtnet   -- source Art-Net (reception UDP)
    StripDmx  -- fait le lien entre une source et plusieurs rubans

  Mode manuel (pilotage sans source externe) :
    Color        -- couleur RGBW
    Select       -- liste des LEDs visees
    Preset       -- une couleur fixe
    Effect       -- parametres d'un effet (forme, vitesse, dephasage)
    ManualRender -- applique presets et effets sur un ruban

  Depend de Esp_Lite_Core (Debug, mutex) et de esp_dmx : les deux sont
  tirees ici, un seul #include suffit donc cote sketch.
*/

#include <Esp_Lite_Core.h>

#include "matricableLed/LedType.h"
#include "matricableLed/StripLed.h"

#include "dmx/CSource.h"
#include "dmx/CDmx.h"
#include "dmx/CArtnet.h"
#include "dmx/StripDmx.h"

#include "manual/Color.h"
#include "manual/Select.h"
#include "manual/Preset.h"
#include "manual/Effect.h"
#include "manual/Render.h"
