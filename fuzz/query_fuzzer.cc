#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "telemux/query.h"

using namespace telemux;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string src(reinterpret_cast<const char*>(data), size);
    auto tokens = lex_query(src);
    auto parsed = parse_query(tokens);
    if (!parsed.ok()) return 0;

    // A handful of recently-decoded samples the query may address.
    std::vector<int64_t> samples = {10, 20, 30, 40};
    QueryContext ctx;
    ctx.session_id = 1;
    ctx.channel = 2;
    ctx.value = 100;
    ctx.samples = samples.data();
    ctx.sample_count = samples.size();

    eval_query(*parsed.value(), ctx);
    return 0;
}
