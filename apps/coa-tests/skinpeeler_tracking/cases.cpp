#include "SkinpeelerTracking.h"
#include <cassert>
#include <thread>
#include <vector>

int main()
{
    SkinpeelerTracking::DirtyPlayers dirty;
    std::vector<std::thread> workers;
    for (std::uint32_t worker = 0; worker < 16; ++worker)
        workers.emplace_back([worker, &dirty]()
        {
            SkinpeelerTracking::PlayerState state;
            for (std::uint32_t index = 0; index < 10000; ++index)
            {
                auto guid = worker * 10000 + index;
                dirty.Mark(guid);
                assert(dirty.Take(guid));
                assert(!dirty.Take(guid));
                ++state.Timer;
                state.Icons[index % 2].Sent = guid;
                state.Icons[index % 2].Seen = true;
            }
            assert(state.Timer == 10000);
            assert(state.Icons[0].Sent == worker * 10000 + 9998);
            assert(state.Icons[1].Sent == worker * 10000 + 9999);
        });
    for (auto& worker : workers)
        worker.join();
    dirty.Mark(42);
    assert(dirty.Take(42));
    dirty.Mark(42);
    assert(dirty.Take(42));
}
