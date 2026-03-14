#pragma once

#include <cstdint>
#include <vector>

#include "telemux/peek_cache.h"
#include "telemux/register_file.h"
#include "telemux/sample_arena.h"
#include "telemux/undo_history.h"

namespace telemux {

enum class Opcode : uint8_t {
    kFilter = 0,
    kResample = 1,
    kMergeChannel = 2,
    kSnapshot = 3,
    kRollback = 4,
    kPeek = 5,
    kRepeatPeek = 6,
};

struct VMInstruction {
    Opcode op = Opcode::kFilter;
    int reg_a = 0;
    int reg_b = 0;
    int reg_dst = 0;
    int slot = 0;
    int32_t param = 0;
};

// Executes a small data-driven instruction stream over sample registers.
// Registers reference bytes owned by a SampleArena; SNAPSHOT/ROLLBACK let
// a program capture and later restore a register's contents -- e.g. to
// back out a filter pass that made a channel worse without re-decoding it.
class TransformVM {
public:
    TransformVM(SampleArena& arena, RegisterFile& regs, UndoRegisterFile& undo);

    void execute(const std::vector<VMInstruction>& program);

    RegisterFile& registers() { return regs_; }
    SampleArena& arena() { return arena_; }

private:
    void execute_snapshot(int slot, int reg_index);
    void execute_rollback(int slot);
    void execute_peek(int reg_index);
    void execute_repeat_peek(int dst_reg);

    SampleArena& arena_;
    RegisterFile& regs_;
    UndoRegisterFile& undo_;
    PeekCache peek_;
};

}  // namespace telemux
