#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "telemux/register_file.h"
#include "telemux/sample_arena.h"
#include "telemux/transform_vm.h"
#include "telemux/undo_history.h"

using namespace telemux;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    SampleArena arena(64);
    RegisterFile regs;
    UndoRegisterFile undo;
    TransformVM vm(arena, regs, undo);

    // Seed every register with real data, as a prior decode stage would
    // have, so opcodes always have something to operate on.
    for (int r = 0; r < kNumRegisters; ++r) {
        uint8_t* p = arena.allocate(16);
        std::memset(p, static_cast<uint8_t>(0x10 * (r + 1)), 16);
        regs.set(r, p, 16);
    }

    std::vector<VMInstruction> program;
    size_t i = 0;
    while (i + 9 <= size) {
        VMInstruction instr;
        instr.op = static_cast<Opcode>(data[i] % 7);
        instr.reg_a = data[i + 1] % kNumRegisters;
        instr.reg_b = data[i + 2] % kNumRegisters;
        instr.reg_dst = data[i + 3] % kNumRegisters;
        instr.slot = data[i + 4] % kNumUndoSlots;
        int32_t param;
        std::memcpy(&param, data + i + 5, 4);
        instr.param = param;
        program.push_back(instr);
        i += 9;
    }

    vm.execute(program);
    return 0;
}
