#ifndef _PLAYERBOT_WOTLKDUNGEONUKSTRATEGY_H
#define _PLAYERBOT_WOTLKDUNGEONUKSTRATEGY_H

#include "Multiplier.h"
#include "AiObjectContext.h"
#include "Strategy.h"
#include "../../BaseDungeonStrategy.h"


class WotlkDungeonUKStrategy : public BaseDungeonStrategy
{
public:
    WotlkDungeonUKStrategy(PlayerbotAI* ai) : BaseDungeonStrategy(ai) {}
    virtual std::string const getName() override { return "utgarde keep"; }
    virtual void InitTriggers(std::vector<TriggerNode*> &triggers) override;
    virtual void InitMultipliers(std::vector<Multiplier*> &multipliers) override;
};

#endif
