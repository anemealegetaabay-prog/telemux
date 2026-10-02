#include "telemux/query.h"

namespace telemux {

namespace {

int64_t field_value(const QueryContext& ctx, const std::string& field) {
    if (field == "session") return ctx.session_id;
    if (field == "channel") return ctx.channel;
    if (field == "value") return ctx.value;
    return 0;
}

bool compare(QueryCompareOp op, int64_t lhs, int64_t rhs) {
    switch (op) {
        case QueryCompareOp::kEq: return lhs == rhs;
        case QueryCompareOp::kNe: return lhs != rhs;
        case QueryCompareOp::kLt: return lhs < rhs;
        case QueryCompareOp::kGt: return lhs > rhs;
        case QueryCompareOp::kLe: return lhs <= rhs;
        case QueryCompareOp::kGe: return lhs >= rhs;
    }
    return false;
}

}  // namespace

bool eval_query(const QueryNode& node, const QueryContext& ctx) {
    switch (node.type) {
        case QueryNodeType::kComparison: {
            int64_t lhs;
            if (node.index >= 0 && node.field == "sample") {
                // Indexed sample lookup, e.g. `sample[3] > 100`. An index past
                // the available samples cannot be evaluated, so the comparison
                // does not match, like the evaluator's other invalid cases.
                if (ctx.samples == nullptr ||
                    static_cast<uint64_t>(node.index) >= ctx.sample_count) {
                    return false;
                }
                lhs = ctx.samples[node.index];
            } else {
                lhs = field_value(ctx, node.field);
            }
            return compare(node.op, lhs, node.value);
        }
        case QueryNodeType::kAnd:
            return eval_query(*node.left, ctx) && eval_query(*node.right, ctx);
        case QueryNodeType::kOr:
            return eval_query(*node.left, ctx) || eval_query(*node.right, ctx);
    }
    return false;
}

}  // namespace telemux
