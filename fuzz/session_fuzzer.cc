#include <cstddef>
#include <cstdint>
#include <vector>

#include "telemux/config.h"
#include "telemux/dedup_cache.h"
#include "telemux/idle_sweep.h"
#include "telemux/session_manager.h"

using namespace telemux;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    TelemuxConfig config;
    config.session_id_bits = 8;  // narrow space so id reuse is reachable quickly
    config.dedup_entry_ttl_ms = 5000;

    SessionBufferArena arena;
    RecentSessionDedupCache dedup(config.dedup_entry_ttl_ms);
    SessionManager mgr(config, arena, dedup);
    IdleSessionSweeper sweeper;

    // Every id this input has touched, so we can close whatever's still
    // open before returning -- otherwise a session left open at the end
    // of one input reads as a leak to LeakSanitizer even though it's
    // ordinary in-progress library state, not an actual bug.
    std::vector<SessionId> touched_ids;

    size_t i = 0;
    while (i < size) {
        uint8_t op = data[i++] % 4;
        switch (op) {
            case 0: {  // OPEN
                SessionId id = mgr.allocate_session_id();
                mgr.open_session(id);
                touched_ids.push_back(id);
                break;
            }
            case 1: {  // DATA
                if (i + 3 > size) goto cleanup;
                SessionId id = static_cast<SessionId>((data[i] << 8) | data[i + 1]);
                i += 2;
                uint8_t len = data[i++] % 32;
                if (i + len > size) goto cleanup;
                mgr.write_session_data(id, data + i, len);
                touched_ids.push_back(id);
                i += len;
                break;
            }
            case 2: {  // CLOSE
                if (i + 2 > size) goto cleanup;
                SessionId id = static_cast<SessionId>((data[i] << 8) | data[i + 1]);
                i += 2;
                mgr.close_session(id, sweeper.now());
                break;
            }
            case 3: {  // SWEEP_TICK
                if (i + 2 > size) goto cleanup;
                uint16_t delta = static_cast<uint16_t>((data[i] << 8) | data[i + 1]);
                i += 2;
                sweeper.advance(delta);
                sweeper.sweep(dedup, mgr, arena);
                break;
            }
        }
    }

cleanup:
    for (SessionId id : touched_ids) {
        mgr.close_session(id, sweeper.now());
    }
    return 0;
}
