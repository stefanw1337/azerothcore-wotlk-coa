#include "CoAPortableSession.h"
#include "CoAPortableImport.h"
#include "CoAPortableProjection.h"
#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "CryptoHash.h"
#include "DBCStores.h"
#include "GitRevision.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Tokenize.h"
#include "Util.h"
#include "WorldSession.h"
#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

using namespace Acore::ChatCommands;

namespace
{
    using CoAPortableSession::State;

    enum class Pending : uint8
    {
        None,
        Baseline,
        Checkpoint
    };

    struct Entry
    {
        std::string SessionId;
        std::string CharacterId;
        uint32 Generation = 1;
        State Phase = State::Active;
        uint32 CheckpointSeq = 0;
        bool Gated = false;
        Milliseconds GateDeadline = Milliseconds::zero();
        Pending Kind = Pending::None;
        uint32 PendingSeq = 0;
        bool Written = false;
        bool Kicked = false;
    };

    std::mutex RegistryLock;
    std::unordered_map<ObjectGuid::LowType, Entry> Registry;
    std::atomic<uint32> RegisteredCount{0};
    std::atomic<uint32> GatedCount{0};
    std::atomic<bool> StartupChecked{false};
    std::atomic<bool> StartupAllowed{false};

    constexpr uint32 DefaultGateSeconds = 30;
    constexpr uint32 MaxGateSeconds = 600;

    bool CheckStartup()
    {
        if (StartupChecked.load())
            return StartupAllowed.load();
        bool allowed = sConfigMgr->GetOption<bool>("PortableSession.Enable", false);
        if (allowed && sConfigMgr->GetOption<uint32>("CharacterDatabase.WorkerThreads", 1) != 1)
        {
            LOG_ERROR("coa.portable", "PortableSession needs CharacterDatabase.WorkerThreads = 1 so that the transactions of one "
                "character commit in order; portable sessions are disabled.");
            allowed = false;
        }
        StartupAllowed = allowed;
        StartupChecked = true;
        return allowed;
    }

    Milliseconds GateTimeout()
    {
        uint32 seconds = sConfigMgr->GetOption<uint32>("PortableSession.GateTimeoutSeconds", DefaultGateSeconds);
        return Seconds(std::clamp<uint32>(seconds, 1, MaxGateSeconds));
    }

    void Hold(Player* player)
    {
        player->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
        player->SetControlled(true, UNIT_STATE_ROOT);
    }

    void Free(Player* player)
    {
        player->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
        player->SetControlled(false, UNIT_STATE_ROOT);
    }

    Entry* Find(ObjectGuid::LowType guid)
    {
        auto it = Registry.find(guid);
        return it == Registry.end() ? nullptr : &it->second;
    }

    ObjectGuid::LowType FindBySession(std::string const& sessionId)
    {
        for (auto const& [guid, entry] : Registry)
            if (entry.SessionId == sessionId)
                return guid;
        return 0;
    }

    void Forget(ObjectGuid::LowType guid)
    {
        auto it = Registry.find(guid);
        if (it == Registry.end())
            return;
        if (it->second.Gated)
            --GatedCount;
        Registry.erase(it);
        --RegisteredCount;
    }

    std::string Printable(std::string_view text)
    {
        std::string out(text);
        for (char& c : out)
            if (!std::isalnum(static_cast<unsigned char>(c)) && std::string_view("._/+:- ").find(c) == std::string_view::npos)
                c = '_';
        return out;
    }

    std::string CatalogEntry(char const* name)
    {
        std::ifstream in(GetClientDBCPath(name), std::ios::binary);
        if (!in)
            return Acore::StringFormat("\"{}\":null", name);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        uint32 records = 0;
        if (bytes.size() >= 20 && std::memcmp(bytes.data(), "WDBC", 4) == 0)
            std::memcpy(&records, bytes.data() + 4, sizeof(records));
        auto const digest = Acore::Crypto::SHA256::GetDigestOf(std::string_view(bytes));
        std::string hex = Acore::Impl::ByteArrayToHexStr(digest.data(), digest.size(), false);
        std::transform(hex.begin(), hex.end(), hex.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return Acore::StringFormat("\"{}\":{{\"sha256\":\"{}\",\"records\":{}}}", name, hex, records);
    }

    std::string CapabilitiesJson()
    {
        static std::mutex lock;
        static std::string catalog;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (catalog.empty())
                catalog = Acore::StringFormat("{{{},{},{},{}}}", CatalogEntry("Appearances.dbc"), CatalogEntry("ItemAppearances.dbc"), CatalogEntry("VanityCollection.dbc"), CatalogEntry("ItemSet.dbc"));
        }
        std::string features = "\"wardrobe\",\"level_projection\"";
        if (CoAPortableSession::Enabled())
            features = "\"runtime_sessions\"," + features;
        std::lock_guard<std::mutex> guard(lock);
        return Acore::StringFormat(
            "{{\"report_version\":2,\"ruleset\":\"coa\",\"core\":{{\"commit\":\"{}\",\"branch\":\"{}\",\"date\":\"{}\"}},"
            "\"portable\":{{\"job_formats\":[{}],\"character_formats\":[{}],\"session_marker_version\":1,\"features\":[{}]}},"
            "\"progression\":{},\"extension_namespaces\":[],\"client_data\":{}}}",
            Printable(GitRevision::GetHash()), Printable(GitRevision::GetBranch()), Printable(GitRevision::GetDate()),
            CoAPortableImport::JobFormat, CoAPortableImport::CharacterFormat, features, CoAPortableProjection::ProgressionJson(), catalog);
    }

    class CoAPortableSessionWorld final : public WorldScript
    {
    public:
        CoAPortableSessionWorld() : WorldScript("CoAPortableSessionWorld", { WORLDHOOK_ON_UPDATE }) { }

        void OnUpdate(uint32 diff) override
        {
            _elapsed += diff;
            if (_elapsed < 1000)
                return;
            _elapsed = 0;
            if (!GatedCount.load())
                return;
            std::vector<ObjectGuid::LowType> expired;
            {
                std::lock_guard<std::mutex> lock(RegistryLock);
                Milliseconds const now = GameTime::GetGameTimeMS();
                for (auto& [guid, entry] : Registry)
                    if (entry.Gated && !entry.Kicked && now >= entry.GateDeadline)
                    {
                        entry.Kicked = true;
                        expired.push_back(guid);
                    }
            }
            for (ObjectGuid::LowType guid : expired)
                if (Player* player = ObjectAccessor::FindPlayerByLowGUID(guid))
                {
                    LOG_ERROR("coa.portable", "the baseline of character {} was not confirmed in time; the character is disconnected", guid);
                    player->GetSession()->KickPlayer("The portable baseline was not confirmed in time");
                }
        }

    private:
        uint32 _elapsed = 0;
    };

    class CoAPortableSessionCommands final : public CommandScript
    {
    public:
        CoAPortableSessionCommands() : CommandScript("CoAPortableSessionCommands") { }

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable const portableCommands = {
                { "checkpoint", HandleCheckpoint, SEC_ADMINISTRATOR, Console::Yes },
                { "release", HandleRelease, SEC_ADMINISTRATOR, Console::Yes },
                { "status", HandleStatus, SEC_ADMINISTRATOR, Console::Yes },
                { "import", HandleImport, SEC_ADMINISTRATOR, Console::Yes },
                { "capabilities", HandleCapabilities, SEC_ADMINISTRATOR, Console::Yes },
                { "project", HandleProject, SEC_ADMINISTRATOR, Console::Yes },
            };
            static ChatCommandTable const commands = {
                { "portable", portableCommands },
            };
            return commands;
        }

        static bool Refuse(ChatHandler* handler, std::string_view why)
        {
            handler->PSendSysMessage("REFUSED {}", why);
            return true;
        }

        static bool HandleCheckpoint(ChatHandler* handler, uint32 guid, std::string sessionId, uint32 sequence)
        {
            if (handler->GetSession())
                return Refuse(handler, "console only");
            if (!CoAPortableSession::Enabled())
                return Refuse(handler, "portable sessions are disabled");
            if (!CoAPortableSession::ValidSessionId(sessionId))
                return Refuse(handler, "bad session id");
            Player* player = ObjectAccessor::FindPlayerByLowGUID(guid);
            if (!player || !player->IsInWorld())
            {
                handler->SendSysMessage("NOT_ONLINE");
                return true;
            }
            {
                std::lock_guard<std::mutex> lock(RegistryLock);
                Entry* entry = Find(guid);
                if (!entry || entry->SessionId != sessionId)
                    return Refuse(handler, "not this session");
                if (entry->Gated || entry->Phase != State::Active)
                    return Refuse(handler, "the session is not running");
                if (sequence <= entry->CheckpointSeq)
                    return Refuse(handler, "stale sequence");
                if (player->IsBeingTeleportedFar())
                {
                    handler->SendSysMessage("BUSY");
                    return true;
                }
                entry->Kind = Pending::Checkpoint;
                entry->PendingSeq = sequence;
                entry->Written = false;
            }
            uint32 const start = GameTime::GetGameTimeMS().count();
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            player->SaveToDB(trans, false, false);
            bool written;
            {
                std::lock_guard<std::mutex> lock(RegistryLock);
                Entry* entry = Find(guid);
                written = entry && entry->Written;
                if (entry)
                    entry->Kind = Pending::None;
            }
            if (!written)
            {
                handler->SendSysMessage("BUSY");
                return true;
            }
            CharacterDatabase.CommitTransaction(trans);
            LOG_INFO("coa.portable", "checkpoint {} of session {} queued for character {} in {} ms", sequence, sessionId, guid,
                uint32(GameTime::GetGameTimeMS().count()) - start);
            handler->PSendSysMessage("QUEUED {}", sequence);
            return true;
        }

        static bool HandleRelease(ChatHandler* handler, std::string sessionId)
        {
            if (handler->GetSession())
                return Refuse(handler, "console only");
            if (!CoAPortableSession::ValidSessionId(sessionId))
                return Refuse(handler, "bad session id");
            Player* player = nullptr;
            {
                std::lock_guard<std::mutex> lock(RegistryLock);
                ObjectGuid::LowType guid = FindBySession(sessionId);
                Entry* entry = guid ? Find(guid) : nullptr;
                if (!entry)
                    return Refuse(handler, "unknown session");
                if (!entry->Gated)
                {
                    handler->SendSysMessage("OK already running");
                    return true;
                }
                player = ObjectAccessor::FindPlayerByLowGUID(guid);
                entry->Gated = false;
                entry->Phase = State::Active;
                --GatedCount;
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_PORTABLE_SESSION_ACTIVE);
                stmt->SetData(0, guid);
                stmt->SetData(1, sessionId);
                CharacterDatabase.Execute(stmt);
            }
            if (player)
                Free(player);
            LOG_INFO("coa.portable", "session {} released", sessionId);
            handler->PSendSysMessage("OK {}", sessionId);
            return true;
        }

        static bool HandleImport(ChatHandler* handler, std::string jobId)
        {
            if (handler->GetSession())
                return Refuse(handler, "console only");
            handler->SendSysMessage(CoAPortableImport::Run(jobId));
            return true;
        }

        static bool HandleProject(ChatHandler* handler, std::string jobId)
        {
            if (handler->GetSession())
                return Refuse(handler, "console only");
            handler->SendSysMessage(CoAPortableImport::Project(jobId));
            return true;
        }

        static bool HandleCapabilities(ChatHandler* handler)
        {
            if (handler->GetSession())
                return Refuse(handler, "console only");
            handler->SendSysMessage(CapabilitiesJson());
            return true;
        }

        static bool HandleStatus(ChatHandler* handler, uint32 guid)
        {
            if (handler->GetSession())
                return Refuse(handler, "console only");
            std::lock_guard<std::mutex> lock(RegistryLock);
            Entry const* entry = Find(guid);
            if (!entry)
            {
                handler->SendSysMessage("NONE");
                return true;
            }
            handler->PSendSysMessage("SESSION {} state {} checkpoint {} gated {}", entry->SessionId, uint32(entry->Phase),
                entry->CheckpointSeq, entry->Gated ? 1 : 0);
            return true;
        }
    };
}

namespace CoAPortableSession
{
    bool Enabled()
    {
        return CheckStartup();
    }

    bool ValidSessionId(std::string const& text)
    {
        if (text.size() != 36)
            return false;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            char const c = text[i];
            if (i == 8 || i == 13 || i == 18 || i == 23)
            {
                if (c != '-')
                    return false;
            }
            else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return false;
        }
        return text[14] == '7' && (text[19] == '8' || text[19] == '9' || text[19] == 'a' || text[19] == 'b');
    }

    void OnLoginComplete(Player* player, PreparedQueryResult const& row)
    {
        if (!Enabled() || !row)
            return;
        Field* fields = row->Fetch();
        ObjectGuid::LowType const guid = player->GetGUID().GetCounter();
        Entry entry;
        entry.SessionId = fields[1].Get<std::string>();
        entry.CharacterId = fields[2].Get<std::string>();
        entry.Generation = fields[4].Get<uint32>();
        uint8 const stored = fields[5].Get<uint8>();
        entry.CheckpointSeq = fields[6].Get<uint32>();
        if (stored > uint8(State::Ended) || !ValidSessionId(entry.SessionId))
        {
            LOG_ERROR("coa.portable", "character {} has an unreadable portable session row; it is not tracked", guid);
            return;
        }
        entry.Phase = State(stored);

        if (entry.Phase == State::Ended)
        {
            LOG_INFO("coa.portable", "character {} logged in before its session {} was closed by the Manager", guid, entry.SessionId);
            player->GetSession()->KickPlayer("The portable session is still closing, try again in a moment");
            return;
        }

        if (QueryResult pin = CharacterDatabase.Query(Acore::StringFormat("SELECT `data` FROM `character_settings` WHERE `guid` = {} AND `source` = 'coa.portable.pin'", guid)))
        {
            std::vector<uint32> words;
            for (std::string_view word : Acore::Tokenize(pin->Fetch()[0].Get<std::string_view>(), ' ', false))
                if (Optional<uint32> value = Acore::StringTo<uint32>(word))
                    words.push_back(*value);
            if (!CoAPortableProjection::PinMatches(words))
            {
                LOG_ERROR("coa.portable", "character {} of session {} was prepared for another progression profile; it is not let in", guid, entry.SessionId);
                player->GetSession()->KickPlayer("This character was prepared for another progression profile of the realm; the Manager must update it first");
                return;
            }
        }

        bool baseline = entry.Phase == State::WaitingBaseline || entry.Phase == State::BaselineReady;
        std::string const sessionId = entry.SessionId;
        {
            std::lock_guard<std::mutex> lock(RegistryLock);
            Forget(guid);
            if (baseline)
                entry.Kind = Pending::Baseline;
            Registry.emplace(guid, std::move(entry));
            ++RegisteredCount;
        }
        if (!baseline)
        {
            LOG_INFO("coa.portable", "character {} continues portable session {}", guid, sessionId);
            return;
        }

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        player->SaveToDB(trans, false, false);
        bool written;
        {
            std::lock_guard<std::mutex> lock(RegistryLock);
            Entry* tracked = Find(guid);
            written = tracked && tracked->Written;
            if (tracked)
            {
                tracked->Kind = Pending::None;
                if (written)
                {
                    tracked->Phase = State::BaselineReady;
                    tracked->Gated = true;
                    tracked->GateDeadline = GameTime::GetGameTimeMS() + GateTimeout();
                    ++GatedCount;
                }
            }
        }
        if (!written)
        {
            LOG_ERROR("coa.portable", "the baseline of character {} could not be saved; the character is disconnected", guid);
            player->GetSession()->KickPlayer("The portable baseline could not be saved");
            return;
        }
        CharacterDatabase.CommitTransaction(trans);
        Hold(player);
        LOG_INFO("coa.portable", "baseline of session {} written for character {}; the character is held until it is released",
            sessionId, guid);
    }

    void AppendSaveMarker(Player* player, CharacterDatabaseTransaction trans, bool logout)
    {
        if (!RegisteredCount.load())
            return;
        ObjectGuid::LowType const guid = player->GetGUID().GetCounter();
        std::lock_guard<std::mutex> lock(RegistryLock);
        Entry* entry = Find(guid);
        if (!entry)
            return;
        CharacterDatabasePreparedStatement* stmt = nullptr;
        if (entry->Kind == Pending::Baseline)
        {
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_PORTABLE_SESSION_BASELINE);
            entry->Written = true;
        }
        else if (entry->Kind == Pending::Checkpoint)
        {
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_PORTABLE_SESSION_CHECKPOINT);
            stmt->SetData(0, entry->PendingSeq);
            entry->CheckpointSeq = entry->PendingSeq;
            entry->Written = true;
        }
        else if (logout && !entry->Gated && entry->Phase == State::Active)
        {
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_PORTABLE_SESSION_ENDED);
            entry->Phase = State::Ended;
        }
        else
            stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_PORTABLE_SESSION_SAVE);
        uint8 const first = entry->Kind == Pending::Checkpoint ? 1 : 0;
        stmt->SetData(first, uint32(guid));
        stmt->SetData(first + 1, entry->SessionId);
        trans->Append(stmt);
    }

    bool IsGated(WorldSession const* session)
    {
        if (!GatedCount.load())
            return false;
        Player const* player = session->GetPlayer();
        if (!player)
            return false;
        std::lock_guard<std::mutex> lock(RegistryLock);
        Entry const* entry = Find(player->GetGUID().GetCounter());
        return entry && entry->Gated;
    }

    void OnLogout(Player* player)
    {
        if (!RegisteredCount.load())
            return;
        std::lock_guard<std::mutex> lock(RegistryLock);
        Forget(player->GetGUID().GetCounter());
    }
}

void AddCoAPortableSessionScripts()
{
    new CoAPortableSessionWorld();
    new CoAPortableSessionCommands();
}
