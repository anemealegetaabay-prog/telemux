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
    sweeper.sweep(dedup, mgr);

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
    sweeper.sweep(dedup, mgr);

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

// Every session buffer is released when its session closes, including a
// buffer replaced by a later write.
TELEMUX_TEST(test_session_buffer_released_on_close) {
    TelemuxConfig config;
    SessionBufferArena arena;
    RecentSessionDedupCache dedup(config.dedup_entry_ttl_ms);
    SessionManager mgr(config, arena, dedup);

    std::vector<uint8_t> a = {1, 2, 3}, b = {4, 5, 6, 7};
    mgr.open_session(9);
    mgr.write_session_data(9, a.data(), a.size());
    mgr.write_session_data(9, b.data(), b.size());
    CHECK(arena.live_buffers() == 1);
    mgr.close_session(9, /*now=*/0);
    CHECK(arena.live_buffers() == 0);
}

// Reopening an id that is still open used to overwrite its record and leak
// the buffer (LeakSanitizer via fuzz/session_fuzzer).
TELEMUX_TEST(test_reopening_open_session_releases_its_buffer) {
    TelemuxConfig config;
    SessionBufferArena arena;
    RecentSessionDedupCache dedup(config.dedup_entry_ttl_ms);
    SessionManager mgr(config, arena, dedup);

    std::vector<uint8_t> payload = {1, 2, 3, 4};
    mgr.open_session(9);
    mgr.write_session_data(9, payload.data(), payload.size());
    mgr.open_session(9);
    CHECK(arena.live_buffers() == 0);
    mgr.close_session(9, /*now=*/0);
    CHECK(arena.live_buffers() == 0);
}

// Sessions still open when the manager is destroyed must not leak.
TELEMUX_TEST(test_session_buffers_released_when_manager_destroyed) {
    TelemuxConfig config;
    SessionBufferArena arena;
    RecentSessionDedupCache dedup(config.dedup_entry_ttl_ms);
    {
        SessionManager mgr(config, arena, dedup);
        std::vector<uint8_t> payload = {1, 2, 3, 4};
        for (SessionId id = 1; id <= 3; ++id) {
            mgr.open_session(id);
            mgr.write_session_data(id, payload.data(), payload.size());
        }
        CHECK(arena.live_buffers() == 3);
    }
    CHECK(arena.live_buffers() == 0);
}

// A zero-length write must not pass a null source pointer to memcpy.
TELEMUX_TEST(test_session_zero_length_write) {
    TelemuxConfig config;
    SessionBufferArena arena;
    RecentSessionDedupCache dedup(config.dedup_entry_ttl_ms);
    SessionManager mgr(config, arena, dedup);
    mgr.open_session(9);
    mgr.write_session_data(9, nullptr, 0);
    mgr.close_session(9, /*now=*/0);
    CHECK(arena.live_buffers() == 0);
}

// The dedup cache used to keep a pointer to the session buffer that
// close_session() had just released, so this check read freed memory.
TELEMUX_TEST(test_dedup_recognizes_retransmit_after_close) {
    TelemuxConfig config;
    SessionBufferArena arena;
    RecentSessionDedupCache dedup(config.dedup_entry_ttl_ms);
    SessionManager mgr(config, arena, dedup);

    std::vector<uint8_t> payload = {1, 2, 3, 4};
    mgr.open_session(9);
    mgr.write_session_data(9, payload.data(), payload.size());
    mgr.close_session(9, /*now=*/0);

    CHECK(dedup.is_duplicate(9, payload.data(), payload.size()));
    std::vector<uint8_t> other = {1, 2, 3, 5};
    CHECK(!dedup.is_duplicate(9, other.data(), other.size()));
    CHECK(!dedup.is_duplicate(8, payload.data(), payload.size()));
}

// Sweeping after a closed id is reopened used to release the closed
// session's buffer a second time (double free).
TELEMUX_TEST(test_sweep_after_id_reuse_drops_dedup_entries_without_double_free) {
    TelemuxConfig config;
    config.dedup_entry_ttl_ms = 30000;
    SessionBufferArena arena;
    RecentSessionDedupCache dedup(config.dedup_entry_ttl_ms);
    SessionManager mgr(config, arena, dedup);
    IdleSessionSweeper sweeper(/*start_ms=*/0);

    std::vector<uint8_t> payload = {1, 2, 3, 4};
    mgr.open_session(9);
    mgr.write_session_data(9, payload.data(), payload.size());
    mgr.close_session(9, /*now=*/0);
    mgr.open_session(9);  // id reused within the dedup window

    sweeper.advance(10);
    sweeper.sweep(dedup, mgr);
    CHECK(dedup.entries().count(9) == 0);
    CHECK(!dedup.is_duplicate(9, payload.data(), payload.size()));

    mgr.close_session(9, /*now=*/10);
    CHECK(arena.live_buffers() == 0);
}
