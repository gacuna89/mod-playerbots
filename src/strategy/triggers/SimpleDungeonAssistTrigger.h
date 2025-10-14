/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_SIMPLEDUNGEONASSISTTRIGGER_H
#define _PLAYERBOT_SIMPLEDUNGEONASSISTTRIGGER_H

#include "Trigger.h"

class PlayerbotAI;

class SimpleDungeonAssistTrigger : public Trigger
{
public:
    SimpleDungeonAssistTrigger(PlayerbotAI* botAI) : Trigger(botAI, "simple dungeon assist", 5) {}

    bool IsActive() override;
};

#endif
