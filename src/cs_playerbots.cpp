/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "BattleGroundTactics.h"
#include "Chat.h"
#include "GuildTaskMgr.h"
#include "PerformanceMonitor.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
<<<<<<< HEAD
#include "RaidBuilderCommand.h"
#include "Group.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

using namespace Acore::ChatCommands;

class playerbots_commandscript : public CommandScript
{
public:
    playerbots_commandscript() : CommandScript("playerbots_commandscript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable playerbotsDebugCommandTable = {
            {"bg", HandleDebugBGCommand, SEC_GAMEMASTER, Console::Yes},
        };

        static ChatCommandTable playerbotsAccountCommandTable = {
            {"setKey", HandleSetSecurityKeyCommand, SEC_PLAYER, Console::No},
            {"link", HandleLinkAccountCommand, SEC_PLAYER, Console::No},
            {"linkedAccounts", HandleViewLinkedAccountsCommand, SEC_PLAYER, Console::No},
            {"unlink", HandleUnlinkAccountCommand, SEC_PLAYER, Console::No},
        };

        static ChatCommandTable playerbotsCommandTable = {
            {"bot", HandlePlayerbotCommand, SEC_PLAYER, Console::No},
            {"assist", HandleAssistCommand, SEC_PLAYER, Console::No},
            {"gtask", HandleGuildTaskCommand, SEC_GAMEMASTER, Console::Yes},
            {"pmon", HandlePerfMonCommand, SEC_GAMEMASTER, Console::Yes},
            {"rndbot", HandleRandomPlayerbotCommand, SEC_GAMEMASTER, Console::Yes},
            {"raid", HandleRaidBuilderCommand, SEC_PLAYER, Console::No},
            {"debug", playerbotsDebugCommandTable},
            {"account", playerbotsAccountCommandTable},
        };

        static ChatCommandTable commandTable = {
            {"playerbots", playerbotsCommandTable},
        };

        return commandTable;
    }

    static bool HandlePlayerbotCommand(ChatHandler* handler, char const* args)
    {
        return PlayerbotMgr::HandlePlayerbotMgrCommand(handler, args);
    }

    static bool HandleRandomPlayerbotCommand(ChatHandler* handler, char const* args)
    {
        return RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(handler, args);
    }

    static bool HandleRaidBuilderCommand(ChatHandler* handler, char const* args)
    {
        std::vector<std::string> messages = RaidBuilderCommand::HandleRaidBuilderCommand(handler, args);
        for (const std::string& message : messages)
        {
            handler->SendSysMessage(message.c_str());
        }
        return true;
    }

    static bool HandleGuildTaskCommand(ChatHandler* handler, char const* args)
    {
        return GuildTaskMgr::HandleConsoleCommand(handler, args);
    }

    static bool HandlePerfMonCommand(ChatHandler* handler, char const* args)
    {
        if (!strcmp(args, "reset"))
        {
            sPerformanceMonitor->Reset();
            return true;
        }

        if (!strcmp(args, "tick"))
        {
            sPerformanceMonitor->PrintStats(true, false);
            return true;
        }

        if (!strcmp(args, "stack"))
        {
            sPerformanceMonitor->PrintStats(false, true);
            return true;
        }

        if (!strcmp(args, "toggle"))
        {
            sPlayerbotAIConfig->perfMonEnabled = !sPlayerbotAIConfig->perfMonEnabled;
            if (sPlayerbotAIConfig->perfMonEnabled)
                LOG_INFO("playerbots", "Performance monitor enabled");
            else
                LOG_INFO("playerbots", "Performance monitor disabled");
            return true;
        }

        sPerformanceMonitor->PrintStats();
        return true;
    }

    static bool HandleDebugBGCommand(ChatHandler* handler, char const* args)
    {
        return BGTactics::HandleConsoleCommand(handler, args);
    }

    static bool HandleSetSecurityKeyCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
        {
            handler->PSendSysMessage("Usage: .playerbots account setKey <securityKey>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();
        std::string key = args;

        PlayerbotMgr* mgr = sPlayerbotsMgr->GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleSetSecurityKeyCommand(player, key);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleLinkAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
            return false;

        char* accountName = strtok((char*)args, " ");
        char* key = strtok(nullptr, " ");

        if (!accountName || !key)
        {
            handler->PSendSysMessage("Usage: .playerbots account link <accountName> <securityKey>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = sPlayerbotsMgr->GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleLinkAccountCommand(player, accountName, key);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleViewLinkedAccountsCommand(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = sPlayerbotsMgr->GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleViewLinkedAccountsCommand(player);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleUnlinkAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
            return false;

        char* accountName = strtok((char*)args, " ");
        if (!accountName)
        {
            handler->PSendSysMessage("Usage: .playerbots account unlink <accountName>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = sPlayerbotsMgr->GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleUnlinkAccountCommand(player, accountName);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleAssistCommand(ChatHandler* handler, char const* args)
    {
        if (!sPlayerbotAIConfig->enabled)
        {
            handler->PSendSysMessage("|cffff0000Playerbot system is currently disabled!");
            return false;
        }

        WorldSession* m_session = handler->GetSession();
        if (!m_session)
        {
            handler->PSendSysMessage("You may only use this command from an active session");
            return false;
        }

        Player* player = m_session->GetPlayer();
        PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(player);
        if (!mgr)
        {
            handler->PSendSysMessage("You cannot control bots yet");
            return false;
        }

        if (!args || !*args)
        {
            handler->PSendSysMessage("Usage: .playerbots assist on/off");
            return false;
        }

        std::string action = args;
        
        if (action == "on")
        {
            // Activar modo assist para todos los bots del grupo
            if (player && player->GetGroup())
            {
                for (GroupReference* ref = player->GetGroup()->GetFirstMember(); ref; ref = ref->next())
                {
                    Player* member = ref->GetSource();
                    if (member == player)
                        continue;
                    
                    PlayerbotAI* botAI = GET_PLAYERBOT_AI(member);
                    if (botAI && botAI->GetAiObjectContext())
                    {
                        auto assistModeValue = botAI->GetAiObjectContext()->GetValue<bool>("assist mode");
                        if (assistModeValue)
                        {
                            assistModeValue->Set(true);
                            // Mensaje del bot al activar assist
                            member->Say("Te mantendré a salvo. Sígueme.", LANG_UNIVERSAL);
                            
                            // Poner marca de estrella al tanque si es tank
                            if (botAI->IsTank(member))
                            {
                                player->GetGroup()->SetTargetIcon(0, player->GetGUID(), member->GetGUID()); // 0 = estrella
                            }
                        }
                    }
                }
                handler->PSendSysMessage("Modo assist activado para todos los bots del grupo.");
            }
            else
            {
                handler->PSendSysMessage("Debes estar en un grupo para usar el comando assist.");
            }
        }
        else if (action == "off")
        {
            // Desactivar modo assist para todos los bots del grupo
            if (player && player->GetGroup())
            {
                for (GroupReference* ref = player->GetGroup()->GetFirstMember(); ref; ref = ref->next())
                {
                    Player* member = ref->GetSource();
                    if (member == player)
                        continue;
                    
                    PlayerbotAI* botAI = GET_PLAYERBOT_AI(member);
                    if (botAI && botAI->GetAiObjectContext())
                    {
                        auto assistModeValue = botAI->GetAiObjectContext()->GetValue<bool>("assist mode");
                        if (assistModeValue)
                        {
                            assistModeValue->Set(false);
                            // Mensaje del bot al desactivar assist
                            member->Say("Muy bien. Dirige tú.", LANG_UNIVERSAL);
                            
                            // Remover marca de estrella del tanque si es tank
                            if (botAI->IsTank(member))
                            {
                                player->GetGroup()->SetTargetIcon(0, player->GetGUID(), ObjectGuid::Empty); // 0 = estrella, Empty = remover
                            }
                        }
                    }
                }
                handler->PSendSysMessage("Modo assist desactivado para todos los bots del grupo.");
            }
            else
            {
                handler->PSendSysMessage("Debes estar en un grupo para usar el comando assist.");
            }
        }
        else
        {
            handler->PSendSysMessage("Usage: .playerbots assist on/off");
        }
        
        return true;
    }
};

void AddSC_playerbots_commandscript() { new playerbots_commandscript(); }
