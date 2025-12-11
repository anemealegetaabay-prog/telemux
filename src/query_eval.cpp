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
        case QueryNodeType::kComparison:
            return compare(node.op, field_value(ctx, node.field), node.value);
        case QueryNodeType::kAnd:
            return eval_query(*node.left, ctx) && eval_query(*node.right, ctx);
        case QueryNodeType::kOr:
            return eval_query(*node.left, ctx) || eval_query(*node.right, ctx);
    }
    return false;
}

}  // namespace telemux
