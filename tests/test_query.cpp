#include <vector>

#include "telemux/query.h"
#include "test_util.h"

using namespace telemux;

TELEMUX_TEST(test_query_simple_comparison) {
    auto tokens = lex_query("channel == 3");
    auto parsed = parse_query(tokens);
    CHECK(parsed.ok());

    QueryContext ctx{1, 3, 0};
    CHECK(eval_query(*parsed.value(), ctx));

    QueryContext ctx2{1, 4, 0};
    CHECK(!eval_query(*parsed.value(), ctx2));
}

TELEMUX_TEST(test_query_and_or_precedence) {
    auto tokens = lex_query("channel == 3 and value > 100");
    auto parsed = parse_query(tokens);
    CHECK(parsed.ok());

    QueryContext match{1, 3, 150};
    CHECK(eval_query(*parsed.value(), match));

    QueryContext no_match{1, 3, 50};
    CHECK(!eval_query(*parsed.value(), no_match));
}

TELEMUX_TEST(test_query_parenthesized_expression) {
    auto tokens = lex_query("(channel == 3 or channel == 5) and value >= 10");
    auto parsed = parse_query(tokens);
    CHECK(parsed.ok());

    QueryContext ctx{1, 5, 10};
    CHECK(eval_query(*parsed.value(), ctx));
}

TELEMUX_TEST(test_query_syntax_error_reported) {
    auto tokens = lex_query("channel ==");
    auto parsed = parse_query(tokens);
    CHECK(!parsed.ok());
}

namespace {
bool eval_with_samples(const char* expr, const std::vector<int64_t>& samples) {
    auto parsed = parse_query(lex_query(expr));
    CHECK(parsed.ok());
    if (!parsed.ok()) return false;
    QueryContext ctx{1, 3, 150};
    ctx.samples = samples.empty() ? nullptr : samples.data();
    ctx.sample_count = samples.size();
    return eval_query(*parsed.value(), ctx);
}
}  // namespace

TELEMUX_TEST(test_query_indexed_sample_in_range) {
    const std::vector<int64_t> samples = {10, 20, 30, 40};
    CHECK(eval_with_samples("sample[0] == 10", samples));
    CHECK(eval_with_samples("sample[3] > 30", samples));
    CHECK(!eval_with_samples("sample[3] < 30", samples));
}

// An index past ctx.sample_count used to read beyond the samples buffer.
// Such a comparison cannot be evaluated and does not match.
TELEMUX_TEST(test_query_indexed_sample_out_of_range_does_not_match) {
    const std::vector<int64_t> samples = {10, 20, 30, 40};
    CHECK(!eval_with_samples("sample[4] == 0", samples));
    CHECK(!eval_with_samples("sample[4] != 0", samples));
    CHECK(!eval_with_samples("sample[9999999999] > 0", samples));
    // The rest of the query is still evaluated normally.
    CHECK(eval_with_samples("sample[4] > 0 or channel == 3", samples));
}

// The CLI evaluates queries without any samples (samples == nullptr).
TELEMUX_TEST(test_query_indexed_sample_without_samples_does_not_match) {
    CHECK(!eval_with_samples("sample[0] == 0", {}));
}
