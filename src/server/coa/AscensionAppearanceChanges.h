/* Copyright (C) 2016+ AzerothCore, GNU AGPL v3. */

#ifndef ASCENSION_APPEARANCE_CHANGES_H
#define ASCENSION_APPEARANCE_CHANGES_H

#include "Define.h"
#include <utility>
#include <vector>

class Player;

uint32 AscensionAppearanceOfItem(uint32 itemId);
bool ApplyAscensionAppearanceChanges(Player* player, std::vector<std::pair<uint32, uint32>> const& changes);

#endif
