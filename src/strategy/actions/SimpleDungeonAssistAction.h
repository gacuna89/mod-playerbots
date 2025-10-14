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
    bool IsBossAccessible(Creature* boss);
    
    // NUEVO: Sistema de waypoints con movimiento natural
    bool MoveToBossUsingWaypoints(Creature* boss);
    bool MoveToBossDirect(Creature* boss);
    bool TryAlternativeMovementStrategies(Creature* boss, uint32 retryCount);
    WorldLocation GetSafeWaypoint(uint32 waypointId);
    bool MoveToWaypointWithNaturalMovement(const WorldLocation& waypoint);
    void AddNaturalVariation(float& x, float& y, float& z);
    void SetNaturalMovementSpeed();
    void ShowArrivalMessage(uint32 waypointId);
    void EnsurePartyFollowsTank();
    void HandoverGuideToMaster();
    
    // Variables para evitar spam de mensajes
    uint32 lastTargetBoss = 0;
    uint32 lastMessageTime = 0;
    
    // Variables para sistema de retry (como Travel)
    uint32 retryCount = 0;
    uint32 maxRetries = 3;
    
    // NUEVO: Variables para movimiento natural
    uint32 currentWaypointIndex = 0;
    uint32 lastWaypointTime = 0;
    float naturalMovementSpeed = 1.0f;
    bool isMovingNaturally = false;
    
    // Sistema dinámico para obtener coordenadas de bosses
    WorldLocation GetBossLocation(uint32 bossEntry);
    std::vector<uint32> GetDungeonBosses(uint32 mapId);
    std::vector<WorldLocation> GetDungeonWaypoints(uint32 mapId);
    
    // NUEVO: Sistema de waypoints optimizado para cualquier mapa
    std::vector<WorldLocation> GetDungeonWaypointsForMap(uint32 mapId);
    WorldLocation GetWaypointLocation(uint32 waypointId);
};

#endif
