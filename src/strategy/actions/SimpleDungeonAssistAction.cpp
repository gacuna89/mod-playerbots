/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "SimpleDungeonAssistAction.h"
#include "Playerbots.h"
#include "LootObjectStack.h"
#include "PathGenerator.h"
#include "Log.h"
#include "PlayerbotTextMgr.h"

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
    // Primero intentar moverse hacia el boss
    MoveToNextBoss();
    
    // DESPUÉS verificar si el jugador está en rango (20 yardas)
    if (!IsPlayerInRange())
    {
        SayMessage("No te alejes, sígueme");
        // Detener cualquier movimiento actual
        bot->GetMotionMaster()->Clear();
        return false; // Esperar al jugador (NO regresar)
    }
    
    // Verificar combate y decidir si continuar o detenerse
    if (bot->IsInCombat())
    {
        int enemyCount = 0;
        bool hasBoss = false;
        bool isLowHealth = bot->GetHealthPct() < 50; // Salud baja
        
        for (Unit* attacker : bot->getAttackers())
        {
            if (attacker && attacker->IsAlive() && attacker->IsInCombat())
            {
                enemyCount++;
                // Verificar si alguno de los atacantes es un boss
                if (attacker->ToCreature() && attacker->ToCreature()->IsDungeonBoss())
                {
                    hasBoss = true;
                }
            }
        }
        
        // Detenerse si hay un boss
        if (hasBoss)
        {
            SayMessage("¡Boss detectado! Enfocando...");
            return false; // Parar para combatir boss
        }
        // Detenerse si hay muchos enemigos (3+) o salud baja
        else if (enemyCount >= 3 || isLowHealth)
        {
            if (isLowHealth)
                SayMessage("Salud baja, limpiando enemigos...");
            else
                SayMessage("Demasiados enemigos, limpiando...");
            return false; // Parar para limpiar
        }
        // Con pocos enemigos y salud buena, continuar pero con precaución
        else if (enemyCount > 0)
        {
            SayMessage("Pocos enemigos, continuando con precaución...");
            // Continuar pero ser más cuidadoso
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
    // Verificar si el boss está muerto dentro de 500 yardas (aumentado para mejor detección)
    std::list<Creature*> creatures;
    bot->GetCreatureListWithEntryInGrid(creatures, bossEntry, 500.0f);
    
    bool foundBoss = false;
    bool foundAliveBoss = false;
    
    for (Creature* creature : creatures)
    {
        if (creature)
        {
            foundBoss = true;
            LOG_ERROR("playerbots", "DEBUG: Encontrado boss {} - Estado: {}", creature->GetName(), creature->IsAlive() ? "VIVO" : "MUERTO");
            
            if (creature->IsAlive())
            {
                foundAliveBoss = true;
                // Si encontramos un boss vivo, definitivamente no está muerto
                return false;
            }
        }
    }
    
    // Si encontramos el boss pero no está vivo, está muerto
    if (foundBoss && !foundAliveBoss)
    {
        LOG_ERROR("playerbots", "DEBUG: Boss {} encontrado pero muerto", bossEntry);
        return true;
    }
    
    // Si no encontramos el boss en 500 yardas, verificar si está en el dungeon pero más lejos
    // Esto puede indicar que el boss ya fue derrotado y no se ha respawneado
    if (!foundBoss)
    {
        // Buscar en un radio más amplio para verificar si existe
        std::list<Creature*> allCreatures;
        bot->GetCreatureListWithEntryInGrid(allCreatures, bossEntry, 2000.0f);
        
        bool existsInDungeon = false;
        for (Creature* creature : allCreatures)
        {
            if (creature)
            {
                existsInDungeon = true;
                if (creature->IsAlive())
                {
                    LOG_ERROR("playerbots", "DEBUG: Boss {} encontrado vivo a {} yardas", bossEntry, (int)bot->GetDistance(creature));
                    return false; // Boss está vivo pero lejos
                }
            }
        }
        
        if (!existsInDungeon)
        {
            LOG_ERROR("playerbots", "DEBUG: Boss {} no encontrado en el dungeon - posiblemente derrotado", bossEntry);
            return true; // Boss no existe = asumir derrotado
        }
    }
    
    // Por defecto, asumir que está vivo si no podemos determinar su estado
    LOG_ERROR("playerbots", "DEBUG: No se pudo determinar el estado del boss {}, asumiendo vivo", bossEntry);
    return false;
}

void SimpleDungeonAssistAction::MoveToNextBoss()
{
    // Obtener bosses del dungeon actual dinámicamente
    std::vector<uint32> bossEntries = GetDungeonBosses(bot->GetMapId());
    
    LOG_ERROR("playerbots", "DEBUG: Encontrados {} bosses en el dungeon", bossEntries.size());
    
    // Mostrar el orden de los bosses
    std::string bossOrder = "Orden de bosses: ";
    for (size_t i = 0; i < bossEntries.size(); ++i)
    {
        bossOrder += std::to_string(bossEntries[i]);
        if (i < bossEntries.size() - 1) bossOrder += ", ";
    }
    LOG_ERROR("playerbots", "DEBUG: {}", bossOrder);
    
    // Contar bosses muertos y vivos para mejor logging
    int deadBosses = 0;
    int aliveBosses = 0;
    uint32 firstAliveBoss = 0;
    
    for (uint32 bossEntry : bossEntries)
    {
        LOG_ERROR("playerbots", "DEBUG: Verificando boss ID: {}", bossEntry);
        
        if (IsBossDead(bossEntry))
        {
            deadBosses++;
            LOG_ERROR("playerbots", "DEBUG: Boss {} ya está muerto", bossEntry);
        }
        else
        {
            aliveBosses++;
            if (firstAliveBoss == 0)
            {
                firstAliveBoss = bossEntry;
            }
            LOG_ERROR("playerbots", "DEBUG: Boss {} está vivo", bossEntry);
        }
    }
    
    LOG_ERROR("playerbots", "DEBUG: Resumen - Bosses muertos: {}, Bosses vivos: {}", deadBosses, aliveBosses);
    
    // Si todos los bosses están muertos
    if (aliveBosses == 0)
    {
        SayMessage("¡Dungeon completado! Todos los bosses han sido derrotados.");
        return;
    }
    
    // Intentar moverse al primer boss vivo en orden secuencial
    if (firstAliveBoss != 0)
    {
        LOG_ERROR("playerbots", "DEBUG: Intentando mover al primer boss vivo en orden: {}", firstAliveBoss);
        
        // Verificar si ya estamos moviéndonos hacia este boss
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE && lastTargetBoss == firstAliveBoss)
        {
            LOG_ERROR("playerbots", "DEBUG: Ya estamos en movimiento hacia el boss {}", firstAliveBoss);
            return; // Ya estamos en movimiento, no interrumpir
        }
        
        if (MoveToBoss(firstAliveBoss))
        {
            LOG_ERROR("playerbots", "DEBUG: Éxito moviendo al boss {} (Sneed debería ser el siguiente después de Rhahk'Zor)", firstAliveBoss);
            return; // Exitosamente empezó a moverse al boss
        }
        else
        {
            LOG_ERROR("playerbots", "DEBUG: Falló al mover al boss {}", firstAliveBoss);
        }
    }
    
    // Si no pudo moverse al primer boss, intentar con otros bosses vivos
    for (uint32 bossEntry : bossEntries)
    {
        if (!IsBossDead(bossEntry) && bossEntry != firstAliveBoss)
        {
            LOG_ERROR("playerbots", "DEBUG: Intentando boss alternativo: {}", bossEntry);
            if (MoveToBoss(bossEntry))
            {
                LOG_ERROR("playerbots", "DEBUG: Éxito moviendo al boss alternativo {}", bossEntry);
                return;
            }
        }
    }
    
    // Si no pudo moverse a ningún boss
    SayMessage("No puedo acceder a ningún boss en este momento. Esperando...");
    LOG_ERROR("playerbots", "DEBUG: No se pudo acceder a ningún boss vivo");
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
                float distance = bot->GetDistance(creature);
                SayMessage("Dirigiendo al boss: " + creature->GetName() + " (Distancia: " + std::to_string((int)distance) + " yardas)");
                lastTargetBoss = bossEntry;
                lastMessageTime = currentTime;
            }
            
            // Usar el sistema de movimiento como MoveToTravelTargetAction
            WorldLocation targetLocation(creature->GetMapId(), creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ());
            
            // En dungeons, usar pathfinding inteligente
            bool canMove = false;
            float distanceToBoss = bot->GetDistance(creature);
            
            // Verificar si el boss es accesible
            bool isBossAccessible = IsBossAccessible(creature);
            
            // Verificar estado del bot antes de intentar movimiento (como Warsong)
            if (bot->IsNonMeleeSpellCast(false, true, true, false, true))
            {
                LOG_ERROR("playerbots", "DEBUG: Bot está casteando, interrumpiendo para moverse");
                bot->InterruptNonMeleeSpells(true);
            }
            
            if (distanceToBoss <= 10.0f && bot->IsWithinLOS(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ()))
            {
                // Muy cerca y con línea de visión - movimiento directo
                LOG_ERROR("playerbots", "DEBUG: Usando MoveNear (muy cerca)");
                canMove = MoveNear(targetLocation.GetMapId(), targetLocation.GetPositionX(), targetLocation.GetPositionY(), targetLocation.GetPositionZ(), 0);
                LOG_ERROR("playerbots", "DEBUG: MoveNear resultado: {}", canMove ? "ÉXITO" : "FALLO");
            }
            else if (isBossAccessible)
            {
                // Boss es accesible - usar pathfinding normal
                LOG_ERROR("playerbots", "DEBUG: Usando MoveTo (pathfinding)");
                
                // Debug técnico detallado
                float botX = bot->GetPositionX();
                float botY = bot->GetPositionY();
                float botZ = bot->GetPositionZ();
                float bossX = creature->GetPositionX();
                float bossY = creature->GetPositionY();
                float bossZ = creature->GetPositionZ();
                
                LOG_ERROR("playerbots", "DEBUG: Bot pos: ({}, {}, {})", (int)botX, (int)botY, (int)botZ);
                LOG_ERROR("playerbots", "DEBUG: Boss pos: ({}, {}, {})", (int)bossX, (int)bossY, (int)bossZ);
                LOG_ERROR("playerbots", "DEBUG: Distancia: {} yardas", (int)distanceToBoss);
                LOG_ERROR("playerbots", "DEBUG: LOS: {}", bot->IsWithinLOS(bossX, bossY, bossZ) ? "SÍ" : "NO");
                
                // Usar la misma lógica que Warsong: PathGenerator con verificación
                bool losBlocked = !bot->IsWithinLOSInMap(creature) || fabs(botZ - bossZ) > 5.0f;
                LOG_ERROR("playerbots", "DEBUG: LOS bloqueado: {}, diferencia altura: {}", losBlocked, (int)fabs(botZ - bossZ));
                
                if (losBlocked)
                {
                    // Interrumpir hechizos si está casteando (como Warsong)
                    if (bot->IsNonMeleeSpellCast(false, true, true, false, true))
                    {
                        bot->InterruptNonMeleeSpells(true);
                        LOG_ERROR("playerbots", "DEBUG: Interrumpido hechizo para reposicionarse");
                    }
                    
                    // FORZAR movimiento incluso con LOS bloqueado - intentar múltiples estrategias
                    LOG_ERROR("playerbots", "DEBUG: LOS bloqueado pero FORZANDO movimiento hacia el boss");
                    
                    // Estrategia 1: Intentar pathfinding normal primero
                    PathGenerator path(bot);
                    path.CalculatePath(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), false);
                    
                    if (path.GetPathType() != PATHFIND_NOPATH)
                    {
                        LOG_ERROR("playerbots", "DEBUG: PathGenerator encontró camino válido");
                        canMove = MoveTo(targetLocation.GetMapId(), targetLocation.GetPositionX(), targetLocation.GetPositionY(), targetLocation.GetPositionZ(), false, false);
                        LOG_ERROR("playerbots", "DEBUG: MoveTo resultado: {}", canMove ? "ÉXITO" : "FALLO");
                    }
                    else
                    {
                        LOG_ERROR("playerbots", "DEBUG: PathGenerator falló, intentando movimiento directo forzado");
                        
                        // Estrategia 2: Movimiento directo forzado (ignorar pathfinding)
                        // Esto es útil cuando el pathfinding falla pero el bot puede moverse directamente
                        canMove = MoveTo(targetLocation.GetMapId(), targetLocation.GetPositionX(), targetLocation.GetPositionY(), targetLocation.GetPositionZ(), true, false); // true = forzar
                        LOG_ERROR("playerbots", "DEBUG: MoveTo forzado resultado: {}", canMove ? "ÉXITO" : "FALLO");
                        
                        // Estrategia 3: Si falla, intentar acercarse gradualmente
                        if (!canMove)
                        {
                            LOG_ERROR("playerbots", "DEBUG: Movimiento forzado falló, intentando acercamiento gradual");
                            
                            // Calcular punto intermedio más cercano al bot
                            float midX = bot->GetPositionX() + (creature->GetPositionX() - bot->GetPositionX()) * 0.5f;
                            float midY = bot->GetPositionY() + (creature->GetPositionY() - bot->GetPositionY()) * 0.5f;
                            float midZ = bot->GetPositionZ() + (creature->GetPositionZ() - bot->GetPositionZ()) * 0.3f; // Menos cambio en Z
                            
                            canMove = MoveTo(targetLocation.GetMapId(), midX, midY, midZ, true, false);
                            LOG_ERROR("playerbots", "DEBUG: Acercamiento gradual resultado: {}", canMove ? "ÉXITO" : "FALLO");
                        }
                    }
                }
                else
                {
                    LOG_ERROR("playerbots", "DEBUG: LOS libre, movimiento directo");
                    canMove = MoveTo(targetLocation.GetMapId(), targetLocation.GetPositionX(), targetLocation.GetPositionY(), targetLocation.GetPositionZ(), false, false);
                    LOG_ERROR("playerbots", "DEBUG: MoveTo resultado: {}", canMove ? "ÉXITO" : "FALLO");
                }
                
                // Si MoveTo falla, intentar múltiples estrategias
                if (!canMove)
                {
                    LOG_ERROR("playerbots", "DEBUG: MoveTo falló, intentando estrategias alternativas...");
                    
                    // Estrategia 1: Punto intermedio con variación aleatoria (como Warsong)
                    float midX = (bot->GetPositionX() + creature->GetPositionX()) / 2.0f;
                    float midY = (bot->GetPositionY() + creature->GetPositionY()) / 2.0f;
                    float midZ = (bot->GetPositionZ() + creature->GetPositionZ()) / 2.0f;
                    
                    // Añadir pequeña variación aleatoria para evitar bucles exactos
                    midX += frand(-1.0f, 1.0f);
                    midY += frand(-1.0f, 1.0f);
                    
                    LOG_ERROR("playerbots", "DEBUG: Intentando punto intermedio con variación...");
                    canMove = MoveTo(targetLocation.GetMapId(), midX, midY, midZ, false, false);
                    LOG_ERROR("playerbots", "DEBUG: MoveTo intermedio resultado: {}", canMove ? "ÉXITO" : "FALLO");
                    
                    // Estrategia 2: Si falla, intentar acercarse más al bot con variación
                    if (!canMove)
                    {
                        LOG_ERROR("playerbots", "DEBUG: Intentando acercarse más al bot con variación...");
                        float closerX = bot->GetPositionX() + (creature->GetPositionX() - bot->GetPositionX()) * 0.3f;
                        float closerY = bot->GetPositionY() + (creature->GetPositionY() - bot->GetPositionY()) * 0.3f;
                        float closerZ = bot->GetPositionZ() + (creature->GetPositionZ() - bot->GetPositionZ()) * 0.3f;
                        
                        // Añadir variación aleatoria
                        closerX += frand(-0.5f, 0.5f);
                        closerY += frand(-0.5f, 0.5f);
                        
                        canMove = MoveTo(targetLocation.GetMapId(), closerX, closerY, closerZ, false, false);
                        LOG_ERROR("playerbots", "DEBUG: MoveTo cercano resultado: {}", canMove ? "ÉXITO" : "FALLO");
                    }
                }
            }
            else
            {
                // Boss no es accesible - buscar boss alternativo o esperar
                SayMessage("Boss no accesible, buscando alternativa...");
                canMove = false; // Forzar búsqueda de siguiente boss
            }
            
            // Sistema de retry más persistente
            if (!canMove)
            {
                retryCount++;
                LOG_ERROR("playerbots", "DEBUG: Intento de movimiento falló, retry {}/{}", retryCount, maxRetries);
                
                // Aumentar el número máximo de reintentos para ser más persistente
                uint32 persistentMaxRetries = maxRetries * 2; // Doblar los reintentos
                
                if (retryCount >= persistentMaxRetries)
                {
                    SayMessage("Persistencia agotada con " + creature->GetName() + ", intentando siguiente objetivo...");
                    LOG_ERROR("playerbots", "DEBUG: Máximo de reintentos persistente alcanzado para boss {}", bossEntry);
                    retryCount = 0;
                    return false; // Intentar siguiente boss
                }
                
                // Esperar menos tiempo entre reintentos para ser más agresivo
                botAI->SetNextCheckDelay(1000); // 1 segundo en lugar de 2
                
                // Intentar una estrategia diferente en cada retry
                if (retryCount % 2 == 0)
                {
                    LOG_ERROR("playerbots", "DEBUG: Retry par, intentando movimiento más agresivo");
                    // En retries pares, intentar movimiento más directo
                    float aggressiveX = bot->GetPositionX() + (creature->GetPositionX() - bot->GetPositionX()) * 0.7f;
                    float aggressiveY = bot->GetPositionY() + (creature->GetPositionY() - bot->GetPositionY()) * 0.7f;
                    float aggressiveZ = bot->GetPositionZ() + (creature->GetPositionZ() - bot->GetPositionZ()) * 0.5f;
                    
                    canMove = MoveTo(targetLocation.GetMapId(), aggressiveX, aggressiveY, aggressiveZ, true, false);
                    LOG_ERROR("playerbots", "DEBUG: Movimiento agresivo resultado: {}", canMove ? "ÉXITO" : "FALLO");
                }
            }
            else
            {
                retryCount = 0; // Reset retry count on success
                LOG_ERROR("playerbots", "DEBUG: Movimiento exitoso hacia boss {}", bossEntry);
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

bool SimpleDungeonAssistAction::IsBossAccessible(Creature* boss)
{
    if (!boss)
    {
        LOG_ERROR("playerbots", "DEBUG: Boss es null");
        return false;
    }
    
    // Verificar si el boss está vivo y en el mundo
    if (boss->isDead())
    {
        LOG_ERROR("playerbots", "DEBUG: Boss {} está muerto", boss->GetName());
        return false;
    }
    
    if (!boss->IsInWorld())
    {
        LOG_ERROR("playerbots", "DEBUG: Boss {} no está en el mundo", boss->GetName());
        return false;
    }
    
    // Verificar si el boss está en la misma instancia
    if (boss->GetInstanceId() != this->bot->GetInstanceId())
    {
        LOG_ERROR("playerbots", "DEBUG: Boss {} en instancia diferente", boss->GetName());
        return false;
    }
    
    // Verificar distancia razonable (máximo 2000 yardas para dungeons)
    float directDistance = this->bot->GetDistance(boss);
    if (directDistance > 2000.0f)
    {
        LOG_ERROR("playerbots", "DEBUG: Boss {} demasiado lejos ({} yardas)", boss->GetName(), (int)directDistance);
        return false; // Demasiado lejos
    }
    
    // Si está en el mismo mapa y no demasiado lejos, asumir que es accesible
    // El pathfinding del juego se encargará de encontrar el camino
    LOG_ERROR("playerbots", "DEBUG: Boss {} es accesible ({} yardas)", boss->GetName(), (int)directDistance);
    return true;
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
        bosses = {644, 642, 643, 646, 645, 647, 1763, 639}; // Rhahk'Zor, Sneed, Mr. Smite, Cookie, Gilnid, Captain Greenskin, Edwin VanCleef
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

// NUEVO: Sistema de waypoints optimizado usando tabla dungeon_waypoints
std::vector<WorldLocation> SimpleDungeonAssistAction::GetDungeonWaypointsForMap(uint32 mapId)
{
    std::vector<WorldLocation> waypoints;
    
    // Obtener waypoints desde la tabla dungeon_waypoints ordenados por waypoint_order
    QueryResult result = WorldDatabase.Query(
        "SELECT position_x, position_y, position_z, waypoint_name, bot_message_id, comment FROM dungeon_waypoints WHERE map_id = {} AND is_critical = 1 ORDER BY waypoint_order",
        mapId
    );
    
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            float x = fields[0].Get<float>();
            float y = fields[1].Get<float>();
            float z = fields[2].Get<float>();
            std::string waypointName = fields[3].Get<std::string>();
            uint32 botMessageId = fields[4].Get<uint32>();
            std::string comment = fields[5].Get<std::string>();
            
            // Crear WorldLocation directamente
            WorldLocation waypoint(mapId, x, y, z);
            waypoints.push_back(waypoint);
            
        } while (result->NextRow());
    }
    
    return waypoints;
}

WorldLocation SimpleDungeonAssistAction::GetWaypointLocation(uint32 waypointId)
{
    // Obtener coordenadas del waypoint desde la tabla dungeon_waypoints
    QueryResult result = WorldDatabase.Query(
        "SELECT position_x, position_y, position_z, waypoint_name, bot_message_id, comment FROM dungeon_waypoints WHERE waypoint_id = {} AND map_id = 36 LIMIT 1",
        waypointId
    );
    
    if (result)
    {
        Field* fields = result->Fetch();
        float x = fields[0].Get<float>();
        float y = fields[1].Get<float>();
        float z = fields[2].Get<float>();
        std::string waypointName = fields[3].Get<std::string>();
        uint32 botMessageId = fields[4].Get<uint32>();
        std::string comment = fields[5].Get<std::string>();
        
        // Usar mensaje personalizado del bot desde ai_playerbot_texts
        if (botMessageId > 0)
        {
            // Obtener el nombre del texto desde la base de datos
            QueryResult textResult = WorldDatabase.Query(
                "SELECT name FROM ai_playerbot_texts WHERE id = {} LIMIT 1",
                botMessageId
            );
            
            if (textResult)
            {
                Field* textFields = textResult->Fetch();
                std::string textName = textFields[0].Get<std::string>();
                std::string botMessage = sPlayerbotTextMgr->GetBotText(textName);
                if (!botMessage.empty())
                {
                    botAI->TellMaster(botMessage);
                }
                else
                {
                    botAI->TellMaster("Usando waypoint: " + waypointName);
                }
            }
            else
            {
                botAI->TellMaster("Usando waypoint: " + waypointName);
            }
        }
        else
        {
            botAI->TellMaster("Usando waypoint: " + waypointName);
        }
        
        // Mostrar comentario si está disponible (para debug)
        if (!comment.empty())
        {
            botAI->TellMaster("Info: " + comment);
        }
        
        return WorldLocation(36, x, y, z); // Deadmines map ID = 36
    }
    
    // Si no se encuentra en dungeon_waypoints, intentar en waypoint_data como fallback
    result = WorldDatabase.Query(
        "SELECT position_x, position_y, position_z FROM waypoint_data WHERE id = {} AND point = 1 LIMIT 1",
        waypointId
    );
    
    if (result)
    {
        Field* fields = result->Fetch();
        float x = fields[0].Get<float>();
        float y = fields[1].Get<float>();
        float z = fields[2].Get<float>();
        
        botAI->TellMaster("Usando waypoint fallback ID " + std::to_string(waypointId) + " en (" + std::to_string((int)x) + ", " + std::to_string((int)y) + ", " + std::to_string((int)z) + ")");
        
        return WorldLocation(36, x, y, z);
    }
    
    // Si no se encuentra, retornar ubicación por defecto
    botAI->TellMaster("ERROR: No se encontró waypoint ID " + std::to_string(waypointId));
    return WorldLocation(36, 0.0f, 0.0f, 0.0f);
}

// NUEVO: Sistema de movimiento natural con waypoints
bool SimpleDungeonAssistAction::MoveToBossUsingWaypoints(Creature* boss)
{
    if (!boss)
        return false;
    
    // Obtener waypoints optimizados para el mapa actual
    uint32 currentMapId = bot->GetMapId();
    std::vector<WorldLocation> waypoints = GetDungeonWaypointsForMap(currentMapId);
    
    if (waypoints.empty())
    {
        botAI->TellMaster("No se encontraron waypoints para el mapa " + std::to_string(currentMapId));
        return false;
    }
    
    // Estrategia inteligente: encontrar el waypoint correcto según el boss
    WorldLocation targetWaypoint;
    bool foundWaypoint = false;
    
    // Primero intentar encontrar el waypoint específico para este boss
    QueryResult result = WorldDatabase.Query(
        "SELECT position_x, position_y, position_z, waypoint_name, bot_message_id FROM dungeon_waypoints WHERE boss_entry = {} AND map_id = {} LIMIT 1",
        boss->GetEntry(), currentMapId
    );
    
    if (result)
    {
        Field* fields = result->Fetch();
        float x = fields[0].Get<float>();
        float y = fields[1].Get<float>();
        float z = fields[2].Get<float>();
        std::string waypointName = fields[3].Get<std::string>();
        uint32 botMessageId = fields[4].Get<uint32>();
        
        targetWaypoint = WorldLocation(currentMapId, x, y, z);
        foundWaypoint = true;
        
        // Mostrar mensaje personalizado
        if (botMessageId > 0)
        {
            // Obtener el nombre del texto desde la base de datos
            QueryResult textResult = WorldDatabase.Query(
                "SELECT name FROM ai_playerbot_texts WHERE id = {} LIMIT 1",
                botMessageId
            );
            
            if (textResult)
            {
                Field* textFields = textResult->Fetch();
                std::string textName = textFields[0].Get<std::string>();
                std::string botMessage = sPlayerbotTextMgr->GetBotText(textName);
                if (!botMessage.empty())
                {
                    botAI->TellMaster(botMessage);
                }
            }
        }
        
        botAI->TellMaster("Encontrado waypoint específico para boss " + std::to_string(boss->GetEntry()));
    }
    else
    {
        // Si no hay waypoint específico, encontrar el más cercano al boss
        float minDistance = FLT_MAX;
        
        for (const WorldLocation& waypoint : waypoints)
        {
            if (waypoint.GetPositionX() == 0.0f && waypoint.GetPositionY() == 0.0f)
                continue;
            
            float distance = boss->GetDistance(waypoint.GetPositionX(), waypoint.GetPositionY(), waypoint.GetPositionZ());
            if (distance < minDistance)
            {
                minDistance = distance;
                targetWaypoint = waypoint;
                foundWaypoint = true;
            }
        }
        
        if (foundWaypoint)
        {
            botAI->TellMaster("Usando waypoint más cercano al boss (distancia: " + std::to_string((int)minDistance) + " yardas)");
        }
    }
    
    if (!foundWaypoint)
    {
        botAI->TellMaster("ERROR: No se encontró waypoint válido para boss " + std::to_string(boss->GetEntry()));
        return false;
    }
    
    // Moverse al waypoint con movimiento natural
    return MoveToWaypointWithNaturalMovement(targetWaypoint);
}

bool SimpleDungeonAssistAction::MoveToWaypointWithNaturalMovement(const WorldLocation& waypoint)
{
    if (waypoint.GetPositionX() == 0.0f && waypoint.GetPositionY() == 0.0f)
        return false;
    
    // Añadir variación natural a las coordenadas
    float x = waypoint.GetPositionX();
    float y = waypoint.GetPositionY();
    float z = waypoint.GetPositionZ();
    
    AddNaturalVariation(x, y, z);
    
    // Configurar velocidad natural
    SetNaturalMovementSpeed();
    
    // Intentar movimiento con pathfinding
    bool canMove = MoveTo(waypoint.GetMapId(), x, y, z, false, false);
    
    if (canMove)
    {
        isMovingNaturally = true;
        lastWaypointTime = getMSTime();
        
        // Añadir pequeña pausa natural ocasionalmente
        if (urand(1, 10) <= 2) // 20% de probabilidad
        {
            botAI->SetNextCheckDelay(urand(500, 1500)); // Pausa de 0.5-1.5 segundos
        }
        
        // Verificar si llegamos al waypoint (distancia < 5 yardas)
        float distanceToWaypoint = bot->GetDistance(x, y, z);
        if (distanceToWaypoint < 5.0f)
        {
            // Buscar el mensaje de llegada por coordenadas aproximadas
            QueryResult result = WorldDatabase.Query(
                "SELECT arrival_message_id FROM dungeon_waypoints WHERE position_x BETWEEN {} AND {} AND position_y BETWEEN {} AND {} AND map_id = {} LIMIT 1",
                x - 10.0f, x + 10.0f, y - 10.0f, y + 10.0f, waypoint.GetMapId()
            );
            
            if (result)
            {
                Field* fields = result->Fetch();
                uint32 arrivalMessageId = fields[0].Get<uint32>();
                
                if (arrivalMessageId > 0)
                {
                    // Obtener el nombre del texto desde la base de datos
                    QueryResult textResult = WorldDatabase.Query(
                        "SELECT name FROM ai_playerbot_texts WHERE id = {} LIMIT 1",
                        arrivalMessageId
                    );
                    
                    if (textResult)
                    {
                        Field* textFields = textResult->Fetch();
                        std::string textName = textFields[0].Get<std::string>();
                        std::string arrivalMessage = sPlayerbotTextMgr->GetBotText(textName);
                        if (!arrivalMessage.empty())
                        {
                            botAI->TellMaster(arrivalMessage);
                        }
                    }
                }
            }
        }
    }
    
    return canMove;
}

void SimpleDungeonAssistAction::AddNaturalVariation(float& x, float& y, float& z)
{
    // Añadir variación aleatoria pequeña para evitar movimiento robótico
    float variation = 2.0f; // 2 yardas de variación máxima
    
    x += frand(-variation, variation);
    y += frand(-variation, variation);
    // No variar Z para evitar problemas de altura
}

void SimpleDungeonAssistAction::SetNaturalMovementSpeed()
{
    // Variar la velocidad de movimiento para hacerlo más natural
    float baseSpeed = 1.0f;
    float variation = 0.2f; // ±20% de variación
    
    naturalMovementSpeed = baseSpeed + frand(-variation, variation);
    
    // Aplicar la velocidad al bot (si es posible)
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
    {
        // El bot ya está en movimiento, no interrumpir
        return;
    }
}

// NUEVO: Movimiento directo mejorado (fallback cuando waypoints fallan)
bool SimpleDungeonAssistAction::MoveToBossDirect(Creature* boss)
{
    if (!boss)
        return false;
    
    WorldLocation targetLocation(boss->GetMapId(), boss->GetPositionX(), boss->GetPositionY(), boss->GetPositionZ());
    float distanceToBoss = bot->GetDistance(boss);
    
    // Debug técnico detallado
    float botX = bot->GetPositionX();
    float botY = bot->GetPositionY();
    float botZ = bot->GetPositionZ();
    float bossX = boss->GetPositionX();
    float bossY = boss->GetPositionY();
    float bossZ = boss->GetPositionZ();
    
    LOG_ERROR("playerbots", "DEBUG: Bot pos: ({}, {}, {})", (int)botX, (int)botY, (int)botZ);
    LOG_ERROR("playerbots", "DEBUG: Boss pos: ({}, {}, {})", (int)bossX, (int)bossY, (int)bossZ);
    LOG_ERROR("playerbots", "DEBUG: Distancia: {} yardas", (int)distanceToBoss);
    
    // Verificar si el boss es accesible
    bool isBossAccessible = IsBossAccessible(boss);
    
    if (distanceToBoss <= 10.0f && bot->IsWithinLOS(bossX, bossY, bossZ))
    {
        // Muy cerca y con línea de visión - movimiento directo
        LOG_ERROR("playerbots", "DEBUG: Usando MoveNear (muy cerca)");
        return MoveNear(targetLocation.GetMapId(), targetLocation.GetPositionX(), targetLocation.GetPositionY(), targetLocation.GetPositionZ(), 0);
    }
    else if (isBossAccessible)
    {
        // Boss es accesible - usar pathfinding mejorado
        LOG_ERROR("playerbots", "DEBUG: Usando MoveTo (pathfinding)");
        
        // Usar pathfinding con verificación de altura más flexible
        bool losBlocked = !bot->IsWithinLOSInMap(boss) || fabs(botZ - bossZ) > 15.0f; // Aumentado de 5 a 15
        LOG_ERROR("playerbots", "DEBUG: LOS bloqueado: {}, diferencia altura: {}", losBlocked, (int)fabs(botZ - bossZ));
        
        if (losBlocked)
        {
            // Interrumpir hechizos si está casteando
            if (bot->IsNonMeleeSpellCast(false, true, true, false, true))
            {
                bot->InterruptNonMeleeSpells(true);
                LOG_ERROR("playerbots", "DEBUG: Interrumpido hechizo para reposicionarse");
            }
            
            // Intentar pathfinding normal primero
            PathGenerator path(bot);
            path.CalculatePath(bossX, bossY, bossZ, false);
            
            if (path.GetPathType() != PATHFIND_NOPATH)
            {
                LOG_ERROR("playerbots", "DEBUG: PathGenerator encontró camino válido");
                return MoveTo(targetLocation.GetMapId(), targetLocation.GetPositionX(), targetLocation.GetPositionY(), targetLocation.GetPositionZ(), false, false);
            }
            else
            {
                LOG_ERROR("playerbots", "DEBUG: PathGenerator falló, intentando movimiento directo forzado");
                return MoveTo(targetLocation.GetMapId(), targetLocation.GetPositionX(), targetLocation.GetPositionY(), targetLocation.GetPositionZ(), true, false);
            }
        }
        else
        {
            LOG_ERROR("playerbots", "DEBUG: LOS libre, movimiento directo");
            return MoveTo(targetLocation.GetMapId(), targetLocation.GetPositionX(), targetLocation.GetPositionY(), targetLocation.GetPositionZ(), false, false);
        }
    }
    
    return false;
}

// NUEVO: Estrategias alternativas de movimiento
bool SimpleDungeonAssistAction::TryAlternativeMovementStrategies(Creature* boss, uint32 retryCount)
{
    if (!boss)
        return false;
    
    LOG_ERROR("playerbots", "DEBUG: Intentando estrategia alternativa {} para boss {}", retryCount, boss->GetEntry());
    
    // Estrategia 1: Punto intermedio con variación
    if (retryCount % 3 == 1)
    {
        float midX = (bot->GetPositionX() + boss->GetPositionX()) / 2.0f;
        float midY = (bot->GetPositionY() + boss->GetPositionY()) / 2.0f;
        float midZ = (bot->GetPositionZ() + boss->GetPositionZ()) / 2.0f;
        
        // Añadir variación aleatoria
        midX += frand(-3.0f, 3.0f);
        midY += frand(-3.0f, 3.0f);
        
        LOG_ERROR("playerbots", "DEBUG: Estrategia 1 - Punto intermedio con variación");
        return MoveTo(boss->GetMapId(), midX, midY, midZ, false, false);
    }
    // Estrategia 2: Acercamiento gradual
    else if (retryCount % 3 == 2)
    {
        float closerX = bot->GetPositionX() + (boss->GetPositionX() - bot->GetPositionX()) * 0.4f;
        float closerY = bot->GetPositionY() + (boss->GetPositionY() - bot->GetPositionY()) * 0.4f;
        float closerZ = bot->GetPositionZ() + (boss->GetPositionZ() - bot->GetPositionZ()) * 0.4f;
        
        LOG_ERROR("playerbots", "DEBUG: Estrategia 2 - Acercamiento gradual");
        return MoveTo(boss->GetMapId(), closerX, closerY, closerZ, false, false);
    }
    // Estrategia 3: Movimiento agresivo
    else
    {
        float aggressiveX = bot->GetPositionX() + (boss->GetPositionX() - bot->GetPositionX()) * 0.8f;
        float aggressiveY = bot->GetPositionY() + (boss->GetPositionY() - bot->GetPositionY()) * 0.8f;
        float aggressiveZ = bot->GetPositionZ() + (boss->GetPositionZ() - bot->GetPositionZ()) * 0.6f;
        
        LOG_ERROR("playerbots", "DEBUG: Estrategia 3 - Movimiento agresivo");
        return MoveTo(boss->GetMapId(), aggressiveX, aggressiveY, aggressiveZ, true, false);
    }
}

