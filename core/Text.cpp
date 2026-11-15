#include "core/Text.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <optional>

#include "core/internal/ExprNode.h"

namespace clothcad {

namespace {

// ---- Parsing ------------------------------------------------------------------

// Recursive descent. Each level returns nothing after recording an error.
class Parser {
public:
    Parser(std::string_view text, const Document& doc) : text_(text), doc_(doc) {}

    std::variant<Expr, TextError> run() {
        std::optional<Expr> result = sum();
        skipSpaces();
        if (result && at_ < text_.size()) fail("unexpected '" + std::string(1, text_[at_]) + "'");
        if (error_) return *error_;
        return *result;
    }

private:
    std::optional<Expr> sum() {
        std::optional<Expr> left = product();
        while (left) {
            if (take('+')) {
                std::optional<Expr> right = product();
                if (!right) return std::nullopt;
                left = *left + *right;
            } else if (take('-')) {
                std::optional<Expr> right = product();
                if (!right) return std::nullopt;
                left = *left - *right;
            } else break;
        }
        return left;
    }

    std::optional<Expr> product() {
        std::optional<Expr> left = unary();
        while (left) {
            if (take('*')) {
                std::optional<Expr> right = unary();
                if (!right) return std::nullopt;
                left = *left * *right;
            } else if (take('/')) {
                std::optional<Expr> right = unary();
                if (!right) return std::nullopt;
                left = *left / *right;
            } else break;
        }
        return left;
    }

    std::optional<Expr> unary() {
        if (take('-')) {
            std::optional<Expr> operand = unary();
            if (!operand) return std::nullopt;
            return -*operand;
        }
        return primary();
    }

    std::optional<Expr> primary() {
        skipSpaces();
        if (at_ >= text_.size()) return fail("the formula is incomplete");
        const char c = text_[at_];

        if (take('(')) {
            std::optional<Expr> inner = sum();
            if (!inner) return std::nullopt;
            if (!take(')')) return fail("missing ')'");
            return inner;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return number();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return named();
        return fail("unexpected '" + std::string(1, c) + "'");
    }

    std::optional<Expr> number() {
        const std::size_t start = at_;
        while (at_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[at_])) || text_[at_] == '.')) ++at_;
        const std::string digits(text_.substr(start, at_ - start));
        char* end = nullptr;
        double value = std::strtod(digits.c_str(), &end);
        if (end != digits.c_str() + digits.size()) {
            at_ = start;
            return fail("'" + digits + "' is not a number");
        }
        // An optional unit directly after the number.
        const std::size_t beforeUnit = at_;
        skipSpaces();
        const std::string_view unit = peekWord();
        if (unit == "cm" || unit == "deg") at_ += unit.size();
        else if (unit == "mm") {
            at_ += unit.size();
            value /= 10.0;
        } else at_ = beforeUnit;
        return Expr(value);
    }

    std::optional<Expr> named() {
        const std::size_t start = at_;
        const std::string_view word = peekWord();
        at_ += word.size();

        if (take('(')) {
            const std::size_t expected = word == "abs" ? 1 : word == "min" || word == "max" ? 2 : word == "clamp" ? 3 : 0;
            if (expected == 0) {
                at_ = start;
                return fail("'" + std::string(word) + "' is not a function; use min, max, abs or clamp");
            }
            std::vector<Expr> arguments;
            for (std::size_t i = 0; i < expected; ++i) {
                if (i > 0 && !take(',')) return fail(std::string(word) + " needs " + std::to_string(expected) + " values");
                std::optional<Expr> argument = sum();
                if (!argument) return std::nullopt;
                arguments.push_back(*argument);
            }
            if (!take(')')) return fail("missing ')'");
            if (word == "abs") return abs(arguments[0]);
            if (word == "min") return min(arguments[0], arguments[1]);
            if (word == "max") return max(arguments[0], arguments[1]);
            return clamp(arguments[0], arguments[1], arguments[2]);
        }

        if (word == "pi") return Expr(pi);
        for (Param p : doc_.parameters())
            if (doc_.get(p).name == word) return Expr(p);
        at_ = start;
        return fail("there is no parameter named '" + std::string(word) + "'");
    }

    std::string_view peekWord() const {
        std::size_t end = at_;
        while (end < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[end])) || text_[end] == '_')) ++end;
        return text_.substr(at_, end - at_);
    }

    void skipSpaces() {
        while (at_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[at_]))) ++at_;
    }

    bool take(char c) {
        skipSpaces();
        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }

    std::nullopt_t fail(std::string message) {
        if (!error_) error_ = TextError{at_, std::move(message)};
        return std::nullopt;
    }

    std::string_view text_;
    const Document& doc_;
    std::size_t at_ = 0;
    std::optional<TextError> error_;
};

// ---- Formatting ---------------------------------------------------------------

using Kind = ExprNode::Kind;

// How tightly a node binds: a child that binds looser than its parent needs
// parentheses.
int binding(const ExprNode& n) {
    switch (n.kind) {
    case Kind::Add:
    case Kind::Subtract: return 1;
    case Kind::Multiply:
    case Kind::Divide: return 2;
    case Kind::Negate: return 3;
    case Kind::Number: return n.number < 0.0 ? 3 : 4;
    default: return 4;
    }
}

std::string format(const ExprNode& n, const Document& doc);

std::string formatChild(const ExprNode& child, int needed, const Document& doc) {
    const std::string text = format(child, doc);
    return binding(child) < needed ? "(" + text + ")" : text;
}

std::string format(const ExprNode& n, const Document& doc) {
    switch (n.kind) {
    case Kind::Number: {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.10g", n.number);
        return buffer;
    }
    case Kind::Parameter: return doc.contains(n.parameter) ? doc.get(n.parameter).name : "<removed>";
    // The right side of - and / needs parentheses even at the same level: a - (b - c).
    case Kind::Add: return formatChild(*n.a, 1, doc) + " + " + formatChild(*n.b, 1, doc);
    case Kind::Subtract: return formatChild(*n.a, 1, doc) + " - " + formatChild(*n.b, 2, doc);
    case Kind::Multiply: return formatChild(*n.a, 2, doc) + " * " + formatChild(*n.b, 2, doc);
    case Kind::Divide: return formatChild(*n.a, 2, doc) + " / " + formatChild(*n.b, 3, doc);
    case Kind::Negate: return "-" + formatChild(*n.a, 4, doc);
    case Kind::Min: return "min(" + format(*n.a, doc) + ", " + format(*n.b, doc) + ")";
    case Kind::Max: return "max(" + format(*n.a, doc) + ", " + format(*n.b, doc) + ")";
    case Kind::Abs: return "abs(" + format(*n.a, doc) + ")";
    }
    return "?";
}

} // namespace

bool canBeTypedInFormulas(std::string_view name) {
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front()))) return false;
    for (char c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    for (std::string_view reserved : {"pi", "min", "max", "abs", "clamp", "cm", "mm", "deg"})
        if (name == reserved) return false;
    return true;
}

std::variant<Expr, TextError> parseExpr(std::string_view text, const Document& doc) { return Parser(text, doc).run(); }

std::string formatExpr(const Expr& expr, const Document& doc) { return format(ExprAccess::node(expr), doc); }

} // namespace clothcad
