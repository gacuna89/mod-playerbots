/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it
 * and/or modify it under version 2 of the License, or (at your option), any later version.
 */

#include "LootRollAction.h"

#include "Event.h"
#include "Group.h"
#include "ItemUsageValue.h"
#include "LootAction.h"
#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ChatHelper.h"
#include "PlayerbotAI.h"

// Weapon/shield/relic whitelist per class.
// Returns false when the item is a WEAPON / SHIELD / RELIC the class should NOT use.
static bool IsWeaponOrShieldOrRelicAllowedForClass(SpecTraits const& traits, ItemTemplate const* proto)
{
    if (!proto)
        return false;

    // Check weapon class restrictions
    if (proto->Class == ITEM_CLASS_WEAPON)
    {
        switch (proto->SubClass)
        {
            case ITEM_SUBCLASS_WEAPON_AXE:
            case ITEM_SUBCLASS_WEAPON_AXE2:
                return traits.cls == CLASS_WARRIOR || traits.cls == CLASS_HUNTER || traits.cls == CLASS_SHAMAN || traits.cls == CLASS_DEATH_KNIGHT;
            case ITEM_SUBCLASS_WEAPON_BOW:
            case ITEM_SUBCLASS_WEAPON_GUN:
            case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                return traits.cls == CLASS_HUNTER;
            case ITEM_SUBCLASS_WEAPON_DAGGER:
                return traits.cls == CLASS_ROGUE || traits.cls == CLASS_HUNTER || traits.cls == CLASS_MAGE || traits.cls == CLASS_WARLOCK || traits.cls == CLASS_PRIEST;
            case ITEM_SUBCLASS_WEAPON_FIST:
                return traits.cls == CLASS_ROGUE || traits.cls == CLASS_HUNTER || traits.cls == CLASS_SHAMAN || traits.cls == CLASS_WARRIOR;
            case ITEM_SUBCLASS_WEAPON_MACE:
            case ITEM_SUBCLASS_WEAPON_MACE2:
                return traits.cls == CLASS_WARRIOR || traits.cls == CLASS_PALADIN || traits.cls == CLASS_SHAMAN || traits.cls == CLASS_PRIEST || traits.cls == CLASS_DEATH_KNIGHT;
            case ITEM_SUBCLASS_WEAPON_POLEARM:
                return traits.cls == CLASS_WARRIOR || traits.cls == CLASS_PALADIN || traits.cls == CLASS_HUNTER || traits.cls == CLASS_DEATH_KNIGHT;
            case ITEM_SUBCLASS_WEAPON_SWORD:
            case ITEM_SUBCLASS_WEAPON_SWORD2:
                return traits.cls == CLASS_WARRIOR || traits.cls == CLASS_PALADIN || traits.cls == CLASS_ROGUE || traits.cls == CLASS_HUNTER || traits.cls == CLASS_DEATH_KNIGHT;
            case ITEM_SUBCLASS_WEAPON_STAFF:
                return traits.cls == CLASS_MAGE || traits.cls == CLASS_WARLOCK || traits.cls == CLASS_PRIEST || traits.cls == CLASS_DRUID;
            case ITEM_SUBCLASS_WEAPON_THROWN:
                return traits.cls == CLASS_ROGUE || traits.cls == CLASS_HUNTER;
            case ITEM_SUBCLASS_WEAPON_WAND:
                return traits.cls == CLASS_MAGE || traits.cls == CLASS_WARLOCK || traits.cls == CLASS_PRIEST;
            default:
                return true;
        }
    }
    else if (proto->Class == ITEM_CLASS_ARMOR)
    {
        if (proto->SubClass == ITEM_SUBCLASS_ARMOR_SHIELD)
        {
            return traits.cls == CLASS_WARRIOR || traits.cls == CLASS_PALADIN || traits.cls == CLASS_SHAMAN;
        }
    }
    else if (proto->Class == ITEM_CLASS_MISC)
    {
        // Relics are handled by class restrictions in the item template
        return true;
    }

    return true;
}

// Check if item stats align with bot's main specialization
static bool IsItemStatsAlignedWithSpec(SpecTraits const& traits, ItemTemplate const* proto)
{
    if (!proto)
        return false;

    // Physical classes (warriors, rogues, hunters, DKs, ferals, ret, enh, prot) never NEED caster gear
    bool isPhysicalClass = (traits.cls == CLASS_WARRIOR || traits.cls == CLASS_ROGUE || traits.cls == CLASS_HUNTER || 
                           traits.cls == CLASS_DEATH_KNIGHT || 
                           (traits.cls == CLASS_DRUID && (traits.spec == "feral" || traits.spec == "bear")) ||
                           (traits.cls == CLASS_PALADIN && (traits.spec == "ret" || traits.spec == "prot")) ||
                           (traits.cls == CLASS_SHAMAN && traits.spec == "enh"));

    // Casters/healers never NEED pure melee DPS gear
    bool isCasterClass = (traits.cls == CLASS_MAGE || traits.cls == CLASS_WARLOCK || traits.cls == CLASS_PRIEST ||
                         (traits.cls == CLASS_DRUID && (traits.spec == "balance" || traits.spec == "resto")) ||
                         (traits.cls == CLASS_PALADIN && traits.spec == "holy") ||
                         (traits.cls == CLASS_SHAMAN && (traits.spec == "ele" || traits.spec == "resto")));

    // Check item stats
    for (uint8 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
    {
        if (proto->ItemStat[i].ItemStatType == 0)
            continue;

        switch (proto->ItemStat[i].ItemStatType)
        {
            case ITEM_MOD_SPELL_POWER:
            case ITEM_MOD_INTELLECT:
            case ITEM_MOD_SPIRIT:
            case ITEM_MOD_MANA_REGENERATION:
                if (isPhysicalClass)
                    return false; // Physical classes don't need caster stats
                break;
            case ITEM_MOD_STRENGTH:
            case ITEM_MOD_ATTACK_POWER:
            case ITEM_MOD_ARMOR_PENETRATION_RATING:
            case ITEM_MOD_EXPERTISE_RATING:
                if (isCasterClass && proto->ItemStat[i].ItemStatType != ITEM_MOD_ATTACK_POWER)
                    return false; // Casters don't need pure melee stats
                break;
        }
    }

    return true;
}

// Check if item is a lockbox
static bool IsLockbox(ItemTemplate const* proto)
{
    if (!proto)
        return false;

    if (proto->Class == ITEM_CLASS_CONTAINER && proto->SubClass == ITEM_SUBCLASS_CONTAINER)
    {
        // Check for lockbox keywords in name
        std::string name = proto->Name1;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        
        return name.find("lockbox") != std::string::npos || 
               name.find("strongbox") != std::string::npos ||
               name.find("chest") != std::string::npos;
    }

    return false;
}

bool LootRollAction::Execute(Event event)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;

    std::vector<Roll*> rolls = group->GetRolls();
    for (Roll*& roll : rolls)
    {
        auto voteIt = roll->playerVote.find(bot->GetGUID());
        if (voteIt != roll->playerVote.end() && voteIt->second != NOT_EMITED_YET)
        {
            continue;
        }
        
        ObjectGuid guid = roll->itemGUID;
        uint32 slot = roll->itemSlot;
        uint32 itemId = roll->itemid;
        int32 randomProperty = 0;
        if (roll->itemRandomPropId)
            randomProperty = roll->itemRandomPropId;
        else if (roll->itemRandomSuffix)
            randomProperty = -((int)roll->itemRandomSuffix);

        RollVote vote = PASS;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        // Get bot's spec traits
        SpecTraits traits = GetSpecTraits(bot);
        
        // Smart loot roll logic
        vote = CalculateSmartRollVote(proto, traits, randomProperty);
        
        // Apply loot roll level restrictions
        if (sPlayerbotAIConfig->lootRollLevel == 0)
        {
            vote = PASS;
        }
        else if (sPlayerbotAIConfig->lootRollLevel == 1)
        {
            if (vote == NEED)
            {
                if (RollUniqueCheck(proto, bot))
                {
                    vote = PASS;
                }
                else 
                {
                    vote = GREED;
                }
            }
            else if (vote == GREED)
            {
                vote = PASS;
            }
        }

        // Handle different loot methods
        switch (group->GetLootMethod())
        {
            case MASTER_LOOT:
            case FREE_FOR_ALL:
                group->CountRollVote(bot->GetGUID(), guid, PASS);
                break;
            default:
                group->CountRollVote(bot->GetGUID(), guid, vote);
                break;
        }

        // Announce roll choice to master if enabled
        if (sPlayerbotAIConfig->announceToMaster && botAI->HasActivePlayerMaster())
        {
            std::string voteStr = (vote == NEED) ? "NEED" : (vote == GREED) ? "GREED" : "PASS";
            std::string itemName = proto->Name1;
            botAI->TellMaster("Rolling " + voteStr + " on " + itemName);
        }

        // One item at a time
        return true;
    }

    return false;
}

RollVote LootRollAction::CalculateSmartRollVote(ItemTemplate const* proto, SpecTraits const& traits, int32 randomProperty)
{
    if (!proto)
        return PASS;

    // Handle tokens (T7-T10)
    if (proto->Class == ITEM_CLASS_MISC && proto->SubClass == ITEM_SUBCLASS_JUNK && proto->Quality == ITEM_QUALITY_EPIC)
    {
        if (CanBotUseToken(proto, bot))
        {
            // Check if token represents a likely upgrade
            if (IsTokenLikelyUpgrade(proto))
                return NEED;
            else
                return GREED;
        }
        else
        {
            return GREED; // Not eligible, so "Greed"
        }
    }

    // Handle lockboxes - always greed
    if (IsLockbox(proto))
    {
        return GREED;
    }

    // Handle weapons, armor, and other equipment
    if (proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR)
    {
        // Check if item is allowed for class
        if (!IsWeaponOrShieldOrRelicAllowedForClass(traits, proto))
        {
            return GREED; // Not usable by class, but might be valuable
        }

        // Check binding type (1 = BIND_ON_EQUIP, 2 = BIND_ON_USE)
        if (proto->Bonding == 1 && !sPlayerbotAIConfig->allowBoENeedIfUpgrade)
        {
            return GREED; // BoE items default to greed unless configured
        }
        
        if (proto->Bonding == 2 && !sPlayerbotAIConfig->allowBoUNeedIfUpgrade)
        {
            return GREED; // BoU items default to greed unless configured
        }

        // Check if stats align with spec
        if (sPlayerbotAIConfig->smartNeedBySpec && !IsItemStatsAlignedWithSpec(traits, proto))
        {
            return GREED; // Stats don't align with spec
        }

        // Check for unique items
        if (RollUniqueCheck(proto, bot))
        {
            return PASS; // Already have unique item
        }

        // Check if item is an upgrade
        std::string itemUsageParam = std::to_string(proto->ItemId);
        if (randomProperty != 0)
            itemUsageParam += "," + std::to_string(randomProperty);
            
        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemUsageParam);
        
        if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_BAD_EQUIP)
        {
            // Check cross-armor upgrade logic
            if (IsCrossArmorUpgrade(proto, traits))
            {
                return NEED;
            }
            else
            {
                return GREED; // Cross-armor item, but not significant upgrade
            }
        }
        else if (usage != ITEM_USAGE_NONE)
        {
            return GREED;
        }
    }
    else
    {
        // Handle other item types (consumables, etc.)
        if (StoreLootAction::IsLootAllowed(proto->ItemId, botAI))
        {
            std::string itemUsageParam = std::to_string(proto->ItemId);
            if (randomProperty != 0)
                itemUsageParam += "," + std::to_string(randomProperty);
                
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemUsageParam);
            
            switch (usage)
            {
                case ITEM_USAGE_EQUIP:
                case ITEM_USAGE_REPLACE:
                case ITEM_USAGE_GUILD_TASK:
                case ITEM_USAGE_BAD_EQUIP:
                    return NEED;
                case ITEM_USAGE_SKILL:
                case ITEM_USAGE_USE:
                case ITEM_USAGE_DISENCHANT:
                case ITEM_USAGE_AH:
                case ITEM_USAGE_VENDOR:
                    return GREED;
                default:
                    break;
            }
        }
    }

    return PASS;
}

bool LootRollAction::IsCrossArmorUpgrade(ItemTemplate const* proto, SpecTraits const& traits)
{
    if (!proto || proto->Class != ITEM_CLASS_ARMOR)
        return true; // Not armor, allow normal upgrade logic

    // Get bot's armor type
    uint8 botArmorType = 0;
    switch (traits.cls)
    {
        case CLASS_WARRIOR:
        case CLASS_PALADIN:
        case CLASS_DEATH_KNIGHT:
            botArmorType = ITEM_SUBCLASS_ARMOR_PLATE;
            break;
        case CLASS_HUNTER:
        case CLASS_SHAMAN:
            botArmorType = ITEM_SUBCLASS_ARMOR_MAIL;
            break;
        case CLASS_ROGUE:
        case CLASS_DRUID:
            botArmorType = ITEM_SUBCLASS_ARMOR_LEATHER;
            break;
        case CLASS_MAGE:
        case CLASS_WARLOCK:
        case CLASS_PRIEST:
            botArmorType = ITEM_SUBCLASS_ARMOR_CLOTH;
            break;
    }

    // If item matches bot's armor type, allow normal upgrade
    if (proto->SubClass == botArmorType)
        return true;

    // Cross-armor upgrade - only allow if significant upgrade
    if (sPlayerbotAIConfig->crossArmorExtraMargin > 0.0f)
    {
        // Calculate item score (simplified)
        float itemScore = proto->ItemLevel * proto->Quality;
        
        // Find current item in same slot
        Item* currentItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, proto->InventoryType);
        if (currentItem)
        {
            ItemTemplate const* currentProto = currentItem->GetTemplate();
            float currentScore = currentProto->ItemLevel * currentProto->Quality;
            
            // Only allow cross-armor if new item is significantly better
            return itemScore >= (currentScore * sPlayerbotAIConfig->crossArmorExtraMargin);
        }
    }

    return false;
}

bool LootRollAction::IsTokenLikelyUpgrade(ItemTemplate const* proto)
{
    if (!proto || sPlayerbotAIConfig->tokenILevelMargin <= 0.0f)
        return true; // Default to allowing if no margin configured

    // Simple check: if token ilvl is higher than current gear average
    float currentGearLevel = bot->GetAverageItemLevel();
    float tokenLevel = proto->ItemLevel;
    
    return tokenLevel >= (currentGearLevel * (1.0f + sPlayerbotAIConfig->tokenILevelMargin));
}

SpecTraits LootRollAction::GetSpecTraits(Player* player)
{
    SpecTraits traits;
    traits.cls = player->getClass();
    
    // Get spec name (simplified)
    switch (traits.cls)
    {
        case CLASS_WARRIOR:
            traits.spec = "arms"; // Default, could be improved with actual spec detection
            break;
        case CLASS_PALADIN:
            traits.spec = "ret"; // Default
            break;
        case CLASS_HUNTER:
            traits.spec = "beast"; // Default
            break;
        case CLASS_ROGUE:
            traits.spec = "combat"; // Default
            break;
        case CLASS_PRIEST:
            traits.spec = "shadow"; // Default
            break;
        case CLASS_DEATH_KNIGHT:
            traits.spec = "unholy"; // Default
            break;
        case CLASS_SHAMAN:
            traits.spec = "enh"; // Default
            break;
        case CLASS_MAGE:
            traits.spec = "fire"; // Default
            break;
        case CLASS_WARLOCK:
            traits.spec = "destro"; // Default
            break;
        case CLASS_DRUID:
            traits.spec = "feral"; // Default
            break;
        default:
            traits.spec = "unknown";
            break;
    }
    
    return traits;
}

bool MasterLootRollAction::isUseful() { return !botAI->HasActivePlayerMaster(); }

bool MasterLootRollAction::Execute(Event event)
{
    Player* bot = QueryItemUsageAction::botAI->GetBot();

    WorldPacket p(event.getPacket());
    ObjectGuid creatureGuid;
    uint32 mapId;
    uint32 itemSlot;
    uint32 itemId;
    uint32 randomSuffix;
    uint32 randomPropertyId;
    uint32 count;
    uint32 timeout;

    p.rpos(0);
    p >> creatureGuid;
    p >> mapId;
    p >> itemSlot;
    p >> itemId;
    p >> randomSuffix;
    p >> randomPropertyId;
    p >> count;
    p >> timeout;

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    SpecTraits traits = LootRollAction::GetSpecTraits(bot);
    RollVote vote = LootRollAction::CalculateSmartRollVote(proto, traits, randomPropertyId);
    
    group->CountRollVote(bot->GetGUID(), creatureGuid, vote);

    return true;
}

bool CanBotUseToken(ItemTemplate const* proto, Player* bot)
{
    if (!proto)
        return false;

    uint32 botClassMask = (1 << (bot->getClass() - 1));
    return (proto->AllowableClass & botClassMask) != 0;
}

bool RollUniqueCheck(ItemTemplate const* proto, Player* bot)
{
    if (!proto)
        return false;

    uint32 totalItemCount = bot->GetItemCount(proto->ItemId, true);
    uint32 bagItemCount = bot->GetItemCount(proto->ItemId, false);
    bool isEquipped = (totalItemCount > bagItemCount);
    
    if (isEquipped && proto->HasFlag(ITEM_FLAG_UNIQUE_EQUIPPABLE))
    {
        return true; // Unique Item is already equipped
    }
    else if (proto->HasFlag(ITEM_FLAG_UNIQUE_EQUIPPABLE) && (bagItemCount > 1))
    {
        return true; // Unique item already in bag, don't roll for it
    }
    
    return false; // Item is not equipped or in bags, roll for it
}

bool RollAction::Execute(Event event)
{
    std::string link = event.getParam();
    
    if (link.empty())
    {
        bot->DoRandomRoll(0, 100);
        return false;
    }
    
    ItemIds itemIds = chat->parseItems(link);
    if (itemIds.empty())
        return false;
        
    uint32 itemId = *itemIds.begin();
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return false;
        
    std::string itemUsageParam = std::to_string(itemId);
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemUsageParam);
    
    switch (proto->Class)
    {
        case ITEM_CLASS_WEAPON:
        case ITEM_CLASS_ARMOR:
            if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_BAD_EQUIP)
            {
                bot->DoRandomRoll(0, 100);
            }
            break;
    }
    
    return true;
}