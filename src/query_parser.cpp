#include "telemux/query.h"

namespace telemux {

namespace {

class Parser {
public:
    explicit Parser(const std::vector<QueryToken>& tokens) : tokens_(tokens) {}

    Result<std::unique_ptr<QueryNode>> parse_expr() {
        auto left = parse_term();
        if (!left.ok()) return left;
        auto node = std::move(left.value());
        while (peek().type == QueryTokenType::kAnd || peek().type == QueryTokenType::kOr) {
            bool is_and = peek().type == QueryTokenType::kAnd;
            advance();
            auto right = parse_term();
            if (!right.ok()) return right;
            auto combined = std::make_unique<QueryNode>();
            combined->type = is_and ? QueryNodeType::kAnd : QueryNodeType::kOr;
            combined->left = std::move(node);
            combined->right = std::move(right.value());
            node = std::move(combined);
        }
        return Result<std::unique_ptr<QueryNode>>(std::move(node));
    }

private:
    const QueryToken& peek() const { return tokens_[pos_]; }
    void advance() {
        if (pos_ + 1 < tokens_.size()) pos_++;
    }

    Result<std::unique_ptr<QueryNode>> parse_term() {
        if (peek().type == QueryTokenType::kLParen) {
            advance();
            auto inner = parse_expr();
            if (!inner.ok()) return inner;
            if (peek().type != QueryTokenType::kRParen) {
                return make_error(ErrorCode::kQuerySyntaxError, "expected )");
            }
            advance();
            return inner;
        }

        if (peek().type != QueryTokenType::kIdent) {
            return make_error(ErrorCode::kQuerySyntaxError, "expected field name");
        }
        std::string field = peek().text;
        advance();

        QueryCompareOp op;
        switch (peek().type) {
            case QueryTokenType::kEq: op = QueryCompareOp::kEq; break;
            case QueryTokenType::kNe: op = QueryCompareOp::kNe; break;
            case QueryTokenType::kLt: op = QueryCompareOp::kLt; break;
            case QueryTokenType::kGt: op = QueryCompareOp::kGt; break;
            case QueryTokenType::kLe: op = QueryCompareOp::kLe; break;
            case QueryTokenType::kGe: op = QueryCompareOp::kGe; break;
            default:
                return make_error(ErrorCode::kQuerySyntaxError, "expected comparison operator");
        }
        advance();

        if (peek().type != QueryTokenType::kNumber) {
            return make_error(ErrorCode::kQuerySyntaxError, "expected number");
        }
        int64_t value = peek().number;
        advance();

        auto node = std::make_unique<QueryNode>();
        node->type = QueryNodeType::kComparison;
        node->field = field;
        node->op = op;
        node->value = value;
        return Result<std::unique_ptr<QueryNode>>(std::move(node));
    }

    const std::vector<QueryToken>& tokens_;
    size_t pos_ = 0;
};

}  // namespace

Result<std::unique_ptr<QueryNode>> parse_query(const std::vector<QueryToken>& tokens) {
    Parser parser(tokens);
    return parser.parse_expr();
}

}  // namespace telemux
