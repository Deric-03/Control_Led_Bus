#pragma once
#include <Arduino.h>

struct Color { int r = 0; int g = 0; int b = 0; int w = 0; };

inline Color baseRed = {255, 0, 0, 0};

inline Color baseGreen = {0, 255, 0, 0};

inline Color baseBleu = {0, 0, 255, 0};

inline Color baseWhite = {0, 0, 0, 255};

inline Color baseWhiteFull = {255, 255, 255, 255};

inline Color baseCyan = {0, 255, 255, 0};

inline Color baseMagenta = {255, 0, 255, 0};

inline Color baseYellow = {255, 255, 0, 0};