#include "telemux/query.h"

#include <cctype>

namespace telemux {

std::vector<QueryToken> lex_query(const std::string& src) {
    std::vector<QueryToken> tokens;
    size_t i = 0;
    while (i < src.size()) {
        char c = src[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            i++;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t start = i;
            while (i < src.size() &&
                   (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_')) {
                i++;
            }
            std::string word = src.substr(start, i - start);
            if (word == "and") {
                tokens.push_back({QueryTokenType::kAnd, word, 0});
            } else if (word == "or") {
                tokens.push_back({QueryTokenType::kOr, word, 0});
            } else {
                tokens.push_back({QueryTokenType::kIdent, word, 0});
            }
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t start = i;
            while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]))) i++;
            std::string num = src.substr(start, i - start);
            QueryToken tok{QueryTokenType::kNumber, num, std::stoll(num)};
            tokens.push_back(tok);
            continue;
        }
        if (c == '=' && i + 1 < src.size() && src[i + 1] == '=') {
            tokens.push_back({QueryTokenType::kEq, "==", 0});
            i += 2;
            continue;
        }
        if (c == '!' && i + 1 < src.size() && src[i + 1] == '=') {
            tokens.push_back({QueryTokenType::kNe, "!=", 0});
            i += 2;
            continue;
        }
        if (c == '<' && i + 1 < src.size() && src[i + 1] == '=') {
            tokens.push_back({QueryTokenType::kLe, "<=", 0});
            i += 2;
            continue;
        }
        if (c == '>' && i + 1 < src.size() && src[i + 1] == '=') {
            tokens.push_back({QueryTokenType::kGe, ">=", 0});
            i += 2;
            continue;
        }
        if (c == '<') {
            tokens.push_back({QueryTokenType::kLt, "<", 0});
            i++;
            continue;
        }
        if (c == '>') {
            tokens.push_back({QueryTokenType::kGt, ">", 0});
            i++;
            continue;
        }
        if (c == '(') {
            tokens.push_back({QueryTokenType::kLParen, "(", 0});
            i++;
            continue;
        }
        if (c == ')') {
            tokens.push_back({QueryTokenType::kRParen, ")", 0});
            i++;
            continue;
        }
        if (c == '[') {
            tokens.push_back({QueryTokenType::kLBracket, "[", 0});
            i++;
            continue;
        }
        if (c == ']') {
            tokens.push_back({QueryTokenType::kRBracket, "]", 0});
            i++;
            continue;
        }
        // Unrecognized character: skip it rather than failing the whole lex.
        i++;
    }
    tokens.push_back({QueryTokenType::kEnd, "", 0});
    return tokens;
}

}  // namespace telemux
