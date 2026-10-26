#ifndef COA_PORTABLE_SESSION_H
#define COA_PORTABLE_SESSION_H

#include "DatabaseEnvFwd.h"
#include "Define.h"
#include <string>

class Player;
class WorldSession;

namespace CoAPortableSession
{
    enum class State : uint8
    {
        WaitingBaseline = 0,
        BaselineReady = 1,
        Active = 2,
        Ended = 3
    };

    bool Enabled();
    void OnLoginComplete(Player* player, PreparedQueryResult const& row);
    void AppendSaveMarker(Player* player, CharacterDatabaseTransaction trans, bool logout);
    bool IsGated(WorldSession const* session);
    void OnLogout(Player* player);
    bool ValidSessionId(std::string const& text);
}

#endif
