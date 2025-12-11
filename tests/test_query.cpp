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
