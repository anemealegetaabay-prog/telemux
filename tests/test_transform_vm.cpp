#include <cstring>
#include <vector>

#include "telemux/register_file.h"
#include "telemux/sample_arena.h"
#include "telemux/transform_vm.h"
#include "telemux/undo_history.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_rollback_restores_after_unrelated_merge_growth) {
    SampleArena arena(64);  // small, so a handful of merges forces growth
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

    // Repeated MERGE_CHANNEL on unrelated registers, forcing several arena
    // reallocations before the register we snapshotted is ever touched.
    for (int i = 0; i < 20; ++i) {
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
