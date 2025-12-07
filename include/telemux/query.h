#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "telemux/errors.h"

namespace telemux {

// A tiny filter language over decoded sample streams, e.g.
// `channel == 3 and value > 100`, used by the CLI's `query` subcommand.
enum class QueryTokenType {
    kIdent,
    kNumber,
    kEq,
    kNe,
    kLt,
    kGt,
    kLe,
    kGe,
    kAnd,
    kOr,
    kLParen,
    kRParen,
    kEnd,
};

struct QueryToken {
    QueryTokenType type;
    std::string text;
    int64_t number = 0;
};

std::vector<QueryToken> lex_query(const std::string& src);

enum class QueryNodeType { kComparison, kAnd, kOr };
enum class QueryCompareOp { kEq, kNe, kLt, kGt, kLe, kGe };

struct QueryNode {
    QueryNodeType type = QueryNodeType::kComparison;

    // kComparison
    std::string field;
    QueryCompareOp op = QueryCompareOp::kEq;
    int64_t value = 0;

    // kAnd / kOr
    std::unique_ptr<QueryNode> left;
    std::unique_ptr<QueryNode> right;
};

Result<std::unique_ptr<QueryNode>> parse_query(const std::vector<QueryToken>& tokens);

struct QueryContext {
    uint16_t session_id = 0;
    uint32_t channel = 0;
    int64_t value = 0;
};

bool eval_query(const QueryNode& node, const QueryContext& ctx);

}  // namespace telemux
