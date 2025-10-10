/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "SimpleDungeonAssistAction.h"
#include "Playerbots.h"
#include "LootObjectStack.h"
#include "PathGenerator.h"

bool SimpleDungeonAssistAction::isUseful()
{
    // Solo para tanques
    if (!botAI->IsTank(bot))
        return false;
    
    // Solo en Deadmines por ahora
    if (bot->GetMapId() != 36)
        return false;
    
    // Solo cuando el modo assist está activo
    AiObjectContext* context = botAI->GetAiObjectContext();
    if (!context)
        return false;
    
    auto assistModeValue = context->GetValue<bool>("assist mode");
    if (!assistModeValue || !assistModeValue->Get())
        return false;
    
    // Verificaciones similares a MoveToTravelTargetAction
    if (bot->HasUnitState(UNIT_STATE_IN_FLIGHT))
        return false;
    
    if (bot->IsFlying())
        return false;
    
    if (bot->isMoving())
        return false;
    
    if (!AI_VALUE(bool, "can move around"))
        return false;
    
    // No interrumpir si hay loot disponible (como Travel)
    LootObject loot = AI_VALUE(LootObject, "loot target");
    if (loot.IsLootPossible(bot))
        return false;
    
    return true;
}

bool SimpleDungeonAssistAction::Execute(Event event)
{
    // Verificar si el jugador está en rango (20 yardas)
    if (!IsPlayerInRange())
    {
        SayMessage("¡Héroe! ¿Dónde estás? ¡Te necesito!");
        return false; // Esperar al jugador
    }
    
    // Verificar si hay demasiados enemigos (2+)
    int enemyCount = 0;
    if (bot->IsInCombat())
    {
        for (Unit* attacker : bot->getAttackers())
        {
            if (attacker && attacker->IsAlive() && attacker->IsInCombat())
                enemyCount++;
        }
        
        if (enemyCount >= 2)
        {
            SayMessage("Demasiados enemigos, limpiando...");
            return false; // Parar para limpiar
        }
    }
    
    // Verificar grupo (similar a Travel) - esperar a otros miembros si están lejos
    Group* group = bot->GetGroup();
    if (group && !urand(0, 1) && bot == botAI->GetGroupMaster() && !bot->IsInCombat())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member == bot)
                continue;
            
            if (!member->IsAlive())
                continue;
            
            if (!member->isMoving())
                continue;
            
            float memberDistance = bot->GetDistance(member);
            if (memberDistance < 50.0f)
                continue;
            if (memberDistance > sPlayerbotAIConfig->reactDistance * 20)
                continue;
            
            // Esperar al miembro del grupo
            SayMessage("Esperando a " + member->GetName() + "...");
            botAI->SetNextCheckDelay(sPlayerbotAIConfig->maxWaitForMove);
            return true;
        }
    }
    
    // SIEMPRE intentar mover al siguiente boss (incluso después de loot/comida)
    MoveToNextBoss();
    
    // Retornar true para que se ejecute continuamente
    return true;
}

bool SimpleDungeonAssistAction::IsDeadmines()
{
    return bot->GetMapId() == 36; // Deadmines map ID
}

bool SimpleDungeonAssistAction::IsBossDead(uint32 bossEntry)
{
    // Verificar si el boss está muerto dentro de 200 yardas
    std::list<Creature*> creatures;
    bot->GetCreatureListWithEntryInGrid(creatures, bossEntry, 200.0f);
    
    for (Creature* creature : creatures)
    {
        if (creature && creature->IsAlive())
        {
            return false; // Boss está vivo
        }
    }
    
    return true; // Boss está muerto o no encontrado
}

void SimpleDungeonAssistAction::MoveToNextBoss()
{
    // Obtener bosses del dungeon actual dinámicamente
    std::vector<uint32> bossEntries = GetDungeonBosses(bot->GetMapId());
    
    for (uint32 bossEntry : bossEntries)
    {
        if (!IsBossDead(bossEntry))
        {
            if (MoveToBoss(bossEntry))
            {
                return; // Exitosamente empezó a moverse al boss
            }
            else
            {
                // No pudo moverse al boss directamente, intentar con waypoints
                SayMessage("Usando waypoints para navegar...");
                if (MoveToNextWaypoint())
                {
                    return; // Exitosamente empezó a moverse a un waypoint
                }
            }
        }
    }
    
    // Si no hay bosses disponibles, verificar si ya terminamos el dungeon
    bool allBossesDead = true;
    for (uint32 bossEntry : bossEntries)
    {
        if (!IsBossDead(bossEntry))
        {
            allBossesDead = false;
            break;
        }
    }
    
    if (allBossesDead)
    {
        SayMessage("¡Dungeon completado! Todos los bosses han sido derrotados.");
    }
    else
    {
        SayMessage("Buscando siguiente objetivo...");
    }
}

bool SimpleDungeonAssistAction::MoveToBoss(uint32 bossEntry)
{
    // Encontrar el boss y moverse hacia él
    std::list<Creature*> creatures;
    bot->GetCreatureListWithEntryInGrid(creatures, bossEntry, 200.0f);
    
    for (Creature* creature : creatures)
    {
        if (creature && creature->IsAlive())
        {
            // Verificar si ya estamos moviéndonos hacia este boss
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE && lastTargetBoss == bossEntry)
            {
                // Ya estamos en movimiento hacia este boss, no interrumpir
                return true;
            }
            
            // Solo decir mensaje si es un nuevo boss o han pasado 10 segundos
            uint32 currentTime = getMSTime();
            if (lastTargetBoss != bossEntry || (currentTime - lastMessageTime) > 10000)
            {
                SayMessage("Dirigiendo al boss: " + creature->GetName());
                lastTargetBoss = bossEntry;
                lastMessageTime = currentTime;
            }
            
            // Usar el sistema de movimiento como MoveToTravelTargetAction
            WorldLocation targetLocation(creature->GetMapId(), creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ());
            
            // En dungeons, usar pathfinding inteligente
            bool canMove = false;
            float distanceToBoss = bot->GetDistance(creature);
            
            // Verificar si hay un camino válido al boss
            bool hasPath = FindPathToBoss(creature);
            
            if (distanceToBoss <= 10.0f && bot->IsWithinLOS(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ()))
            {
                // Muy cerca y con línea de visión - movimiento directo
                canMove = MoveNear(targetLocation.GetMapId(), targetLocation.GetPositionX(), targetLocation.GetPositionY(), targetLocation.GetPositionZ(), 0);
            }
            else if (hasPath)
            {
                // Hay un camino válido - usar pathfinding
                canMove = MoveTo(targetLocation.GetMapId(), targetLocation.GetPositionX(), targetLocation.GetPositionY(), targetLocation.GetPositionZ(), false, false);
            }
            else
            {
                // No hay camino directo - intentar acercarse gradualmente
                // Buscar un punto intermedio más cercano
                float midX = (bot->GetPositionX() + creature->GetPositionX()) / 2.0f;
                float midY = (bot->GetPositionY() + creature->GetPositionY()) / 2.0f;
                float midZ = (bot->GetPositionZ() + creature->GetPositionZ()) / 2.0f;
                
                canMove = MoveTo(targetLocation.GetMapId(), midX, midY, midZ, false, false);
                
                if (canMove)
                {
                    SayMessage("Navegando por el camino disponible...");
                }
            }
            
            // Sistema de retry como Travel
            if (!canMove)
            {
                retryCount++;
                if (retryCount >= maxRetries)
                {
                    SayMessage("No puedo llegar al boss, intentando siguiente objetivo...");
                    retryCount = 0;
                    return false; // Intentar siguiente boss
                }
                else
                {
                    SayMessage("Intentando llegar al boss... (" + std::to_string(retryCount) + "/" + std::to_string(maxRetries) + ")");
                    return false; // Reintentar mismo boss
                }
            }
            else
            {
                retryCount = 0; // Reset retry count on success
            }
            
            return canMove;
        }
    }
    
    return false; // Boss no encontrado
}

bool SimpleDungeonAssistAction::IsPlayerInRange()
{
    Player* master = botAI->GetMaster();
    if (!master)
        return false;
    
    float distance = bot->GetDistance(master);
    return distance <= sPlayerbotAIConfig->farDistance; // Por defecto 20 yardas
}

void SimpleDungeonAssistAction::SayMessage(const std::string& message)
{
    bot->Say(message, LANG_UNIVERSAL);
}

bool SimpleDungeonAssistAction::FindPathToBoss(Creature* boss)
{
    if (!boss)
        return false;
    
    // Usar PathGenerator para encontrar el camino
    PathGenerator path(bot);
    path.SetPathLengthLimit(500.0f); // Límite de 500 yardas
    
    bool result = path.CalculatePath(boss->GetPositionX(), boss->GetPositionY(), boss->GetPositionZ());
    
    if (result && !path.GetPath().empty())
    {
        // El pathfinding encontró un camino
        return true;
    }
    
    return false;
}

bool SimpleDungeonAssistAction::MoveToWaypoint(const WorldLocation& waypoint)
{
    // Mover a un waypoint específico usando pathfinding
    return MoveTo(waypoint.GetMapId(), waypoint.GetPositionX(), waypoint.GetPositionY(), waypoint.GetPositionZ(), false, false);
}

WorldLocation SimpleDungeonAssistAction::GetBossLocation(uint32 bossEntry)
{
    // Buscar dinámicamente las coordenadas del boss en la base de datos
    uint32 mapId = bot->GetMapId();
    
    // Query para obtener las coordenadas del boss
    QueryResult result = WorldDatabase.Query(
        "SELECT position_x, position_y, position_z FROM creature WHERE id1 = {} AND map = {} LIMIT 1",
        bossEntry, mapId
    );
    
    if (result)
    {
        Field* fields = result->Fetch();
        float x = fields[0].Get<float>();
        float y = fields[1].Get<float>();
        float z = fields[2].Get<float>();
        
        return WorldLocation(mapId, x, y, z);
    }
    
    // Si no se encuentra en la base de datos, buscar el boss en el mundo actual
    std::list<Creature*> creatures;
    bot->GetCreatureListWithEntryInGrid(creatures, bossEntry, 500.0f);
    
    for (Creature* creature : creatures)
    {
        if (creature && creature->IsAlive())
        {
            return WorldLocation(mapId, creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ());
        }
    }
    
    // Si no se encuentra, retornar ubicación por defecto
    return WorldLocation(mapId, 0.0f, 0.0f, 0.0f);
}

std::vector<uint32> SimpleDungeonAssistAction::GetDungeonBosses(uint32 mapId)
{
    std::vector<uint32> bosses;
    
    // Por ahora solo soportamos Deadmines
    // En el futuro se puede crear una tabla con información de dungeons
    if (mapId == 36) // Deadmines
    {
        bosses = {644, 643, 646, 645, 647, 1763, 639}; // Rhahk'Zor, Sneed, Mr. Smite, Cookie, Gilnid, Captain Greenskin, Edwin VanCleef
    }
    
    return bosses;
}

std::vector<WorldLocation> SimpleDungeonAssistAction::GetDungeonWaypoints(uint32 mapId)
{
    std::vector<WorldLocation> waypoints;
    
    // Por ahora solo soportamos Deadmines
    if (mapId == 36) // Deadmines
    {
        // Obtener bosses del dungeon
        std::vector<uint32> bosses = GetDungeonBosses(mapId);
        
        // Crear waypoints basados en las ubicaciones de los bosses
        for (uint32 bossEntry : bosses)
        {
            WorldLocation bossLocation = GetBossLocation(bossEntry);
            if (bossLocation.GetPositionX() != 0.0f || bossLocation.GetPositionY() != 0.0f)
            {
                waypoints.push_back(bossLocation);
            }
        }
    }
    
    return waypoints;
}

bool SimpleDungeonAssistAction::MoveToNextWaypoint()
{
    std::vector<WorldLocation> waypoints = GetDungeonWaypoints(bot->GetMapId());
    
    // Encontrar el waypoint más cercano al bot
    WorldLocation closestWaypoint = waypoints[0];
    float closestDistance = bot->GetDistance(waypoints[0].GetPositionX(), waypoints[0].GetPositionY(), waypoints[0].GetPositionZ());
    
    for (const WorldLocation& waypoint : waypoints)
    {
        float distance = bot->GetDistance(waypoint.GetPositionX(), waypoint.GetPositionY(), waypoint.GetPositionZ());
        if (distance < closestDistance)
        {
            closestDistance = distance;
            closestWaypoint = waypoint;
        }
    }
    
    // Mover al waypoint más cercano
    return MoveToWaypoint(closestWaypoint);
}
