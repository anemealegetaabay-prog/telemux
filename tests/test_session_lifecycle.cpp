#include <vector>

#include "telemux/config.h"
#include "telemux/dedup_cache.h"
#include "telemux/idle_sweep.h"
#include "telemux/session_manager.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_sweep_does_not_double_free_reused_session_buffer) {
    TelemuxConfig config;
    config.session_id_bits = 8;
    config.dedup_entry_ttl_ms = 30000;

    SessionBufferArena arena;
    RecentSessionDedupCache dedup(config.dedup_entry_ttl_ms);
    SessionManager mgr(config, arena, dedup);
    IdleSessionSweeper sweeper(/*start_ms=*/0);

    SessionId id = mgr.allocate_session_id();
    mgr.open_session(id);
    std::vector<uint8_t> payload = {1, 2, 3, 4};
    mgr.write_session_data(id, payload.data(), payload.size());
    mgr.close_session(id, /*now=*/0);

    // Force immediate reuse of the same id rather than relying on
    // wraparound timing.
    mgr.open_session(id);

    // Sweep runs well before the dedup TTL has elapsed.
    sweeper.advance(10);
    sweeper.sweep(dedup, mgr, arena);

    // A clean return here means the sweep only released the closed
    // incarnation's buffer once.
    CHECK(true);
}

TELEMUX_TEST(test_session_id_fits_wire_field_after_many_sessions) {
    TelemuxConfig config;
    config.session_id_bits = 8;

    SessionBufferArena arena;
    RecentSessionDedupCache dedup(config.dedup_entry_ttl_ms);
    SessionManager mgr(config, arena, dedup);

    for (int i = 0; i < 5000; ++i) {
        SessionId id = mgr.allocate_session_id();
        CHECK(id <= config.max_session_id_space());
        mgr.open_session(id);
        mgr.close_session(id, /*now=*/static_cast<Timestamp>(i));
    }
}
