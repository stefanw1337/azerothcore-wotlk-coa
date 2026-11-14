#include "Config.h"
#include "CoARaceAvailability.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ScriptMgr.h"
#include "StringFormat.h"

namespace
{
    std::string JoinRaces(std::set<uint8> const& races)
    {
        std::string text;
        for (uint8 race : races)
        {
            if (!text.empty())
                text += ',';
            text += std::to_string(race);
        }
        return text;
    }
}

class CoAMaleOnlyRaces : public WorldScript
{
public:
    CoAMaleOnlyRaces() : WorldScript("CoAMaleOnlyRaces", { WORLDHOOK_ON_STARTUP }) { }

    void OnStartup() override
    {
        auto races = CoARaceAvailability::Parse(
            sConfigMgr->GetOption<std::string>("CoACustomRaces.MaleOnlyRaceIds", "98,100,103"));
        if (!races)
        {
            LOG_ERROR("server.loading",
                "CoA Custom Races: invalid MaleOnlyRaceIds; female characters are left as they are");
            return;
        }
        if (races->empty())
            return;
        CharacterDatabase.DirectExecute(Acore::StringFormat(
            "UPDATE characters SET gender = 0 WHERE gender = 1 AND race IN ({})", JoinRaces(*races)));
        LOG_INFO("server.loading", "CoA Custom Races: characters of the male-only races {} are male",
            JoinRaces(*races));
    }
};

void AddCoAMaleOnlyRacesScripts()
{
    new CoAMaleOnlyRaces();
}
