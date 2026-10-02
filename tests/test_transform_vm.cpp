#include <cstring>
#include <vector>

#include "telemux/register_file.h"
#include "telemux/sample_arena.h"
#include "telemux/transform_vm.h"
#include "telemux/undo_history.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_rollback_after_interleaved_merge_sequence) {
    SampleArena arena(4096);  // ample headroom for the merges below
    RegisterFile regs;
    UndoRegisterFile undo;
    TransformVM vm(arena, regs, undo);

    uint8_t seed[16];
    for (int i = 0; i < 16; ++i) seed[i] = static_cast<uint8_t>(i);
    uint8_t* p0 = arena.allocate(16);
    std::memcpy(p0, seed, 16);
    regs.set(0, p0, 16);

    uint8_t* p5 = arena.allocate(8);
    std::memset(p5, 0xAA, 8);
    regs.set(5, p5, 8);
    uint8_t* p6 = arena.allocate(8);
    std::memset(p6, 0xBB, 8);
    regs.set(6, p6, 8);

    std::vector<VMInstruction> program;
    VMInstruction snap;
    snap.op = Opcode::kSnapshot;
    snap.slot = 0;
    snap.reg_a = 0;
    program.push_back(snap);

    // A couple of MERGE_CHANNEL ops on unrelated registers between the
    // snapshot and the rollback, to exercise the interleaved case.
    for (int i = 0; i < 2; ++i) {
        VMInstruction merge;
        merge.op = Opcode::kMergeChannel;
        merge.reg_a = 5;
        merge.reg_b = 6;
        merge.reg_dst = 7;
        program.push_back(merge);

        VMInstruction fold_back;
        fold_back.op = Opcode::kMergeChannel;
        fold_back.reg_a = 7;
        fold_back.reg_b = 6;
        fold_back.reg_dst = 5;
        program.push_back(fold_back);
    }

    VMInstruction rollback;
    rollback.op = Opcode::kRollback;
    rollback.slot = 0;
    program.push_back(rollback);

    vm.execute(program);

    CHECK(regs.regs[0].live);
    CHECK(std::memcmp(regs.regs[0].data, seed, 16) == 0);
}

TELEMUX_TEST(test_repeat_peek_after_interleaved_merge_sequence) {
    SampleArena arena(4096);  // ample headroom for the merges below
    RegisterFile regs;
    UndoRegisterFile undo;
    TransformVM vm(arena, regs, undo);

    uint8_t seed[16];
    for (int i = 0; i < 16; ++i) seed[i] = static_cast<uint8_t>(0x40 + i);
    uint8_t* p0 = arena.allocate(16);
    std::memcpy(p0, seed, 16);
    regs.set(0, p0, 16);

    uint8_t* p1 = arena.allocate(16);
    std::memset(p1, 0, 16);
    regs.set(1, p1, 16);

    uint8_t* p5 = arena.allocate(8);
    std::memset(p5, 0xCC, 8);
    regs.set(5, p5, 8);
    uint8_t* p6 = arena.allocate(8);
    std::memset(p6, 0xDD, 8);
    regs.set(6, p6, 8);

    std::vector<VMInstruction> program;
    VMInstruction peek;
    peek.op = Opcode::kPeek;
    peek.reg_a = 0;
    program.push_back(peek);

    for (int i = 0; i < 2; ++i) {
        VMInstruction merge;
        merge.op = Opcode::kMergeChannel;
        merge.reg_a = 5;
        merge.reg_b = 6;
        merge.reg_dst = 7;
        program.push_back(merge);

        VMInstruction fold_back;
        fold_back.op = Opcode::kMergeChannel;
        fold_back.reg_a = 7;
        fold_back.reg_b = 6;
        fold_back.reg_dst = 5;
        program.push_back(fold_back);
    }

    VMInstruction repeat_peek;
    repeat_peek.op = Opcode::kRepeatPeek;
    repeat_peek.reg_dst = 1;
    program.push_back(repeat_peek);

    vm.execute(program);

    CHECK(regs.regs[1].live);
    CHECK(std::memcmp(regs.regs[1].data, seed, 16) == 0);
}

TELEMUX_TEST(test_snapshot_zero_extra_allocation) {
    SampleArena arena(4096);
    RegisterFile regs;
    UndoRegisterFile undo;
    TransformVM vm(arena, regs, undo);

    uint8_t* p = arena.allocate(1024);
    regs.set(0, p, 1024);

    std::vector<VMInstruction> program;
    for (int i = 0; i < kNumUndoSlots; ++i) {
        VMInstruction snap;
        snap.op = Opcode::kSnapshot;
        snap.slot = i;
        snap.reg_a = 0;
        program.push_back(snap);
    }

    telemux_test::ScopedAllocCounter guard;
    vm.execute(program);
    CHECK(guard.allocation_count() == 0);
}

namespace {
// Runs a single RESERVE of `count` entries into register 3, which starts out
// holding 8 bytes of 0x5A.
void run_reserve(int32_t count, SampleArena& arena, RegisterFile& regs) {
    UndoRegisterFile undo;
    TransformVM vm(arena, regs, undo);
    uint8_t* p = arena.allocate(8);
    std::memset(p, 0x5A, 8);
    regs.set(3, p, 8);

    VMInstruction reserve;
    reserve.op = Opcode::kReserve;
    reserve.reg_dst = 3;
    reserve.param = count;
    vm.execute({reserve});
}

bool reg3_untouched(const RegisterFile& regs) {
    const Register& r = regs.regs[3];
    if (!r.live || r.len != 8) return false;
    for (size_t i = 0; i < 8; ++i)
        if (r.data[i] != 0x5A) return false;
    return true;
}
}  // namespace

TELEMUX_TEST(test_reserve_stages_requested_entries) {
    SampleArena arena(64);
    RegisterFile regs;
    run_reserve(3, arena, regs);
    const Register& r = regs.regs[3];
    CHECK(r.live);
    CHECK(r.len == 3 * 24);
    // Entry i is 24 bytes filled with i.
    for (size_t i = 0; i < r.len; ++i) CHECK(r.data[i] == i / 24);
}

// 0x0AAAAAAB * 24 wraps to 8 in 32-bit arithmetic: the old code allocated an
// 8-byte staging buffer and wrote ~4 GiB of records into it.
TELEMUX_TEST(test_reserve_rejects_count_whose_size_overflows_32_bits) {
    SampleArena arena(64);
    RegisterFile regs;
    run_reserve(0x0AAAAAAB, arena, regs);
    CHECK(reg3_untouched(regs));
}

TELEMUX_TEST(test_reserve_enforces_max_entry_count) {
    {
        SampleArena arena(64);
        RegisterFile regs;
        run_reserve(kMaxReserveEntries + 1, arena, regs);
        CHECK(reg3_untouched(regs));
    }
    {
        SampleArena arena(64);
        RegisterFile regs;
        run_reserve(kMaxReserveEntries, arena, regs);
        CHECK(regs.regs[3].len == static_cast<size_t>(kMaxReserveEntries) * 24);
    }
    {
        SampleArena arena(64);
        RegisterFile regs;
        run_reserve(-1, arena, regs);
        CHECK(reg3_untouched(regs));
    }
}
