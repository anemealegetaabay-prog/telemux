#include "telemux/peek_cache.h"

namespace telemux {

void PeekCache::record(uint8_t* data, size_t len) {
    entry_.data = data;
    entry_.len = len;
    entry_.live = true;
}

}  // namespace telemux
