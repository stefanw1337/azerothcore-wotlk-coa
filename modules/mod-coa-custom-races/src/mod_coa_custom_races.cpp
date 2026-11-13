/*
 * CoA Custom Races - server master switch (coa_custom_races.conf: CoACustomRaces.Enable).
 *
 * The races themselves (ChrRaces / CharSections ... DBC rows, world SQL, client MPQs) come with the CoA Custom
 * package; the core patches it needs live in the core branch. This module is the switch a server manager toggles:
 * with CoACustomRaces.Enable = 0 no character of an added race can be created. Existing characters keep their data.
 */

#include "Config.h"
#include "CoARaceAvailability.h"
#include "CryptoHash.h"
#include "DBCStores.h"
#include "Log.h"
#include "ScriptMgr.h"
#include <filesystem>
#include <fstream>
#include <mutex>

namespace
{
    bool CustomRacesEnabled = false;
    std::set<uint8> DisabledRaceIds;
    struct PendingPolicy
    {
        bool Enabled;
        std::set<uint8> Disabled;
        std::string CatalogHash;
        std::string DataDirectory;
    };
    std::optional<PendingPolicy> Pending;
    bool CatalogLoaded = false;
    std::mutex PolicyMutex;

    bool CatalogMatches(PendingPolicy const& policy)
    {
        if (policy.Disabled.empty() || policy.CatalogHash.empty())
            return true;
        auto const path = std::filesystem::path(policy.DataDirectory) / "dbc_races" / "ChrRaces.dbc";
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return false;
        Acore::Crypto::SHA256 hash;
        char buffer[4096];
        while (input.read(buffer, sizeof(buffer)) || input.gcount())
            hash.UpdateData(std::string_view(buffer, static_cast<size_t>(input.gcount())));
        if (!input.eof())
            return false;
        hash.Finalize();
        std::string hex;
        char const* digits = "0123456789abcdef";
        for (auto byte : hash.GetDigest())
        {
            hex.push_back(digits[byte >> 4]);
            hex.push_back(digits[byte & 15]);
        }
        return hex == policy.CatalogHash;
    }

    void ApplyPendingPolicy()
    {
        if (!Pending || !sChrRacesStore.GetNumRows())
            return;
        CatalogLoaded = true;
        if (!CatalogMatches(*Pending))
        {
            LOG_ERROR("server.loading", "CoA Custom Races: race catalog changed; review DisabledRaceIds before applying a new policy");
            Pending.reset();
            return;
        }
        if (!CoARaceAvailability::Valid(Pending->Disabled, [](uint8 race)
            {
                return sChrRacesStore.LookupEntry(race) != nullptr;
            }))
        {
            LOG_ERROR("server.loading", "CoA Custom Races: DisabledRaceIds contains an unknown race; keeping the previous creation policy");
            Pending.reset();
            return;
        }
        CustomRacesEnabled = Pending->Enabled;
        DisabledRaceIds = std::move(Pending->Disabled);
        Pending.reset();
        LOG_INFO("server.loading", "CoA Custom Races: {}, {} races disabled for new characters",
            CustomRacesEnabled ? "enabled" : "disabled", DisabledRaceIds.size());
    }
}

class CoACustomRacesConfig : public WorldScript
{
public:
    CoACustomRacesConfig() : WorldScript("CoACustomRacesConfig",
        { WORLDHOOK_ON_BEFORE_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) { }

    void OnBeforeConfigLoad(bool /*reload*/) override
    {
        std::lock_guard<std::mutex> guard(PolicyMutex);
        auto disabled = CoARaceAvailability::Parse(
            sConfigMgr->GetOption<std::string>("CoACustomRaces.DisabledRaceIds", ""));
        auto catalogHash = CoARaceAvailability::NormalizeCatalogHash(
            sConfigMgr->GetOption<std::string>("CoACustomRaces.DisabledRaceCatalogSha256", ""));
        Pending.reset();
        if (!disabled || !catalogHash)
        {
            LOG_ERROR("server.loading", "CoA Custom Races: invalid DisabledRaceIds; keeping the previous creation policy");
            return;
        }
        Pending = PendingPolicy{sConfigMgr->GetOption<bool>("CoACustomRaces.Enable", false), std::move(*disabled),
            std::move(*catalogHash), sConfigMgr->GetOption<std::string>("DataDir", ".")};
        if (CatalogLoaded)
            ApplyPendingPolicy();
    }

    void OnStartup() override
    {
        std::lock_guard<std::mutex> guard(PolicyMutex);
        ApplyPendingPolicy();
    }
};

class CoACustomRacesAccount : public AccountScript
{
public:
    CoACustomRacesAccount() : AccountScript("CoACustomRacesAccount", { ACCOUNTHOOK_CAN_ACCOUNT_CREATE_CHARACTER }) { }

    bool CanAccountCreateCharacter(uint32 /*accountId*/, uint8 race, uint8 /*charClass*/) override
    {
        std::lock_guard<std::mutex> guard(PolicyMutex);
        ApplyPendingPolicy();
        return CoARaceAvailability::CanCreate(CustomRacesEnabled, DisabledRaceIds, race);
    }
};

void AddCoAHideArmorScripts();

void Addmod_coa_custom_racesScripts()
{
    AddCoAHideArmorScripts();
    new CoACustomRacesConfig();
    new CoACustomRacesAccount();
}
