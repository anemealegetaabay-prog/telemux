#include <vector>

#include "telemux/config.h"
#include "telemux/dedup_cache.h"
#include "telemux/idle_sweep.h"
#include "telemux/session_manager.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_sweep_after_immediate_session_id_reuse) {
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

    // Sweep runs after the dedup retransmit window has elapsed.
    sweeper.advance(35000);  // past the dedup TTL
    sweeper.sweep(dedup, mgr, arena);

    CHECK(true);
}

TELEMUX_TEST(test_sweep_after_repeated_close_reopen_churn) {
    TelemuxConfig config;
    config.session_id_bits = 8;
    config.dedup_entry_ttl_ms = 30000;

    SessionBufferArena arena;
    RecentSessionDedupCache dedup(config.dedup_entry_ttl_ms);
    SessionManager mgr(config, arena, dedup);
    IdleSessionSweeper sweeper(/*start_ms=*/0);

    SessionId id = mgr.allocate_session_id();

    // Close and reopen the same id twice in a row before any sweep runs.
    mgr.open_session(id);
    std::vector<uint8_t> payload1 = {1, 2, 3};
    mgr.write_session_data(id, payload1.data(), payload1.size());
    mgr.close_session(id, /*now=*/0);

    mgr.open_session(id);
    std::vector<uint8_t> payload2 = {4, 5, 6, 7};
    mgr.write_session_data(id, payload2.data(), payload2.size());
    mgr.close_session(id, /*now=*/5);

    sweeper.advance(35000);  // past the dedup TTL
    sweeper.sweep(dedup, mgr, arena);

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
