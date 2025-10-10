/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "SimpleDungeonAssistTrigger.h"
#include "Playerbots.h"

bool SimpleDungeonAssistTrigger::IsActive()
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
    
    return true;
}
