#include "telemux/transform_vm.h"

#include <cstring>

#include "telemux/transform_ops_filter.h"
#include "telemux/transform_ops_merge.h"

namespace telemux {

TransformVM::TransformVM(SampleArena& arena, RegisterFile& regs, UndoRegisterFile& undo)
    : arena_(arena), regs_(regs), undo_(undo) {
    // The arena only needs to know about live sample registers to keep
    // them valid across a reallocation.
    arena_.bind_registers(&regs_);
}

void TransformVM::execute(const std::vector<VMInstruction>& program) {
    for (const auto& instr : program) {
        switch (instr.op) {
            case Opcode::kFilter:
                execute_filter(regs_, instr.reg_a, instr.param);
                break;
            case Opcode::kResample:
                execute_resample(regs_, instr.reg_a, instr.param);
                break;
            case Opcode::kMergeChannel:
                execute_merge_channel(arena_, regs_, instr.reg_a, instr.reg_b, instr.reg_dst);
                break;
            case Opcode::kSnapshot:
                execute_snapshot(instr.slot, instr.reg_a);
                break;
            case Opcode::kRollback:
                execute_rollback(instr.slot);
                break;
            case Opcode::kPeek:
                execute_peek(instr.reg_a);
                break;
            case Opcode::kRepeatPeek:
                execute_repeat_peek(instr.reg_dst);
                break;
        }
    }
}

void TransformVM::execute_snapshot(int slot, int reg_index) {
    const Register& r = regs_.regs[reg_index];
    if (!r.live) return;
    // Zero-copy: hang on to the register's current pointer directly rather
    // than duplicating its bytes, since a program may snapshot large
    // registers many times before ever rolling one back.
    undo_.snapshot(slot, reg_index, r.data, r.len);
}

void TransformVM::execute_rollback(int slot) {
    const UndoSlot& u = undo_.slot(slot);
    if (!u.live) return;
    Register& r = regs_.regs[u.reg_index];
    if (!r.live || r.len < u.len) return;
    // Restore the register's bytes to what they were at snapshot time.
    std::memcpy(r.data, u.data, u.len);
}

void TransformVM::execute_peek(int reg_index) {
    const Register& r = regs_.regs[reg_index];
    if (!r.live) return;
    // Zero-copy, same rationale as SNAPSHOT: a program may PEEK the same
    // register many times while deciding what to do next.
    peek_.record(r.data, r.len);
}

void TransformVM::execute_repeat_peek(int dst_reg) {
    const PeekEntry& p = peek_.entry();
    if (!p.live) return;
    Register& r = regs_.regs[dst_reg];
    if (!r.live || r.len < p.len) return;
    // Re-emit the previously peeked bytes without touching the source
    // register again.
    std::memcpy(r.data, p.data, p.len);
}

}  // namespace telemux
