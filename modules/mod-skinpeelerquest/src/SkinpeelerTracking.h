#ifndef COA_SKINPEELER_TRACKING_H
#define COA_SKINPEELER_TRACKING_H

#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_set>

namespace SkinpeelerTracking
{
    struct IconState
    {
        std::uint32_t Sent = 0;
        bool Seen = false;
    };

    struct PlayerState
    {
        std::uint32_t Timer = 0;
        std::array<IconState, 2> Icons;
    };

    class DirtyPlayers
    {
    public:
        void Mark(std::uint32_t guid)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _players.insert(guid);
        }

        bool Take(std::uint32_t guid)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            return _players.erase(guid) != 0;
        }

    private:
        std::mutex _mutex;
        std::unordered_set<std::uint32_t> _players;
    };
}

#endif
