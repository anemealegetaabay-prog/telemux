#include "telemux/config.h"

namespace telemux {

// TelemuxConfig is a plain aggregate; defaults live in the header. This
// translation unit exists so the type has a stable place to grow
// validation/loading logic (e.g. from a config file) without every
// dependent header needing to change.

}  // namespace telemux
