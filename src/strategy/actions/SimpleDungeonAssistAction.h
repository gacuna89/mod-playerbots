/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_SIMPLEDUNGEONASSISTACTION_H
#define _PLAYERBOT_SIMPLEDUNGEONASSISTACTION_H

#include "MovementActions.h"

class PlayerbotAI;

class SimpleDungeonAssistAction : public MovementAction
{
public:
    SimpleDungeonAssistAction(PlayerbotAI* botAI) : MovementAction(botAI, "simple dungeon assist") {}

    bool Execute(Event event) override;
    bool isUseful() override;
    bool isPossible() override { return true; } // Siempre es posible

private:
    bool IsDeadmines();
    bool IsBossDead(uint32 bossEntry);
    void MoveToNextBoss();
    bool IsPlayerInRange();
    bool MoveToBoss(uint32 bossEntry);
    void SayMessage(const std::string& message);
    bool FindPathToBoss(Creature* boss);
    bool MoveToWaypoint(const WorldLocation& waypoint);
    
    // Variables para evitar spam de mensajes
    uint32 lastTargetBoss = 0;
    uint32 lastMessageTime = 0;
    
    // Variables para sistema de retry (como Travel)
    uint32 retryCount = 0;
    uint32 maxRetries = 3;
    
    // Sistema dinámico para obtener coordenadas de bosses
    WorldLocation GetBossLocation(uint32 bossEntry);
    std::vector<uint32> GetDungeonBosses(uint32 mapId);
    std::vector<WorldLocation> GetDungeonWaypoints(uint32 mapId);
    bool MoveToNextWaypoint();
};

#endif
