#include "telemux/undo_history.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_undo_slot_starts_not_live) {
    UndoRegisterFile undo;
    CHECK(!undo.slot(0).live);
}

TELEMUX_TEST(test_undo_snapshot_records_fields) {
    UndoRegisterFile undo;
    uint8_t buf[4] = {1, 2, 3, 4};
    undo.snapshot(/*slot=*/2, /*reg_index=*/5, buf, sizeof(buf));

    const UndoSlot& slot = undo.slot(2);
    CHECK(slot.live);
    CHECK(slot.reg_index == 5);
    CHECK(slot.len == 4);
    CHECK(slot.data == buf);
}
