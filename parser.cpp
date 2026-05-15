#include "parser.h"
#include <iostream>
#include <cstdlib>

Parser::Parser(std::vector<ParserToken> t) : tokens(std::move(t)), pos(0) {}

const ParserToken& Parser::peek(int offset) const {
    static ParserToken eof{TokenKind::END_OF_FILE, "", "", 0, 0};
    if (pos + offset >= tokens.size()) return eof;
    return tokens[pos + offset];
}

ParserToken Parser::consume() {
    if (pos >= tokens.size()) return {TokenKind::END_OF_FILE, "", "", 0, 0};
    return tokens[pos++];
}

ParserToken Parser::expect(TokenKind kind, const std::string& msg) {
    auto t = peek();
    if (t.kind != kind) {
        std::string m = msg.empty() ? ("expected token, got " + token_name(t)) : msg;
        throw std::runtime_error(m + " at " + t.filename + ":" + std::to_string(t.line));
    }
    return consume();
}

bool Parser::match(TokenKind kind) {
    if (peek().kind == kind) { consume(); return true; }
    return false;
}

bool Parser::match(const std::string& val) {
    if (peek().value == val) { consume(); return true; }
    return false;
}

void Parser::sync() {
    while (pos < tokens.size()) {
        auto k = peek().kind;
        if (k == TokenKind::SEMICOLON || k == TokenKind::RBRACE || k == TokenKind::END_OF_FILE) break;
        consume();
    }
    if (peek().kind == TokenKind::SEMICOLON) consume();
}

std::string Parser::token_name(const ParserToken& t) const {
    switch (t.kind) {
        case TokenKind::IDENTIFIER: return "identifier '" + t.value + "'";
        case TokenKind::TYPE_NAME: return "type name '" + t.value + "'";
        case TokenKind::NUMBER: return "number '" + t.value + "'";
        case TokenKind::STRING_LITERAL: return "string '" + t.value + "'";
        case TokenKind::CHAR_LITERAL: return "char '" + t.value + "'";
        case TokenKind::END_OF_FILE: return "end of file";
        case TokenKind::KW_INT: return "'int'";
        case TokenKind::SEMICOLON: return "';'";
        case TokenKind::LBRACE: return "'{'";
        case TokenKind::RBRACE: return "'}'";
        case TokenKind::LPAREN: return "'('";
        case TokenKind::RPAREN: return "')'";
        default: return "'" + t.value + "'";
    }
}

// ==================== Expression Parsing ====================

std::unique_ptr<Expr> Parser::parse_expression() {
    auto e = parse_assignment_expression();
    if (peek().kind == TokenKind::COMMA) {
        auto c = std::make_unique<CommaExpr>();
        c->lhs = std::move(e);
        while (peek().kind == TokenKind::COMMA) {
            consume();
            auto rhs = parse_assignment_expression();
            c->rhs = std::move(rhs);
        }
        return c;
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_assignment_expression() {
    auto e = parse_conditional_expression();
    static const std::pair<TokenKind, AssignOp> assign_ops[] = {
        {TokenKind::ASSIGN, AssignOp::ASSIGN},
        {TokenKind::ADD_ASSIGN, AssignOp::ADD},
        {TokenKind::SUB_ASSIGN, AssignOp::SUB},
        {TokenKind::MUL_ASSIGN, AssignOp::MUL},
        {TokenKind::DIV_ASSIGN, AssignOp::DIV},
        {TokenKind::MOD_ASSIGN, AssignOp::MOD},
        {TokenKind::AND_ASSIGN, AssignOp::AND},
        {TokenKind::OR_ASSIGN, AssignOp::OR},
        {TokenKind::XOR_ASSIGN, AssignOp::XOR},
        {TokenKind::LSHIFT_ASSIGN, AssignOp::LSHIFT},
        {TokenKind::RSHIFT_ASSIGN, AssignOp::RSHIFT},
    };
    for (const auto& [kind, op] : assign_ops) {
        if (peek().kind == kind) {
            consume();
            auto a = std::make_unique<AssignExpr>();
            a->lhs = std::move(e);
            a->op = op;
            a->rhs = parse_assignment_expression();
            return a;
        }
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_conditional_expression() {
    auto e = parse_logical_or_expression();
    if (peek().kind == TokenKind::QUESTION) {
        consume();
        auto c = std::make_unique<ConditionalExpr>();
        c->cond = std::move(e);
        c->then_expr = parse_expression();
        expect(TokenKind::COLON, "expected ':' in conditional expression");
        c->else_expr = parse_conditional_expression();
        return c;
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_logical_or_expression() {
    auto e = parse_logical_and_expression();
    while (peek().kind == TokenKind::OR) {
        consume();
        auto b = std::make_unique<BinaryExpr>();
        b->op = BinaryOp::OR;
        b->lhs = std::move(e);
        b->rhs = parse_logical_and_expression();
        e = std::move(b);
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_logical_and_expression() {
    auto e = parse_inclusive_or_expression();
    while (peek().kind == TokenKind::AND) {
        consume();
        auto b = std::make_unique<BinaryExpr>();
        b->op = BinaryOp::AND;
        b->lhs = std::move(e);
        b->rhs = parse_inclusive_or_expression();
        e = std::move(b);
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_inclusive_or_expression() {
    auto e = parse_exclusive_or_expression();
    while (peek().kind == TokenKind::PIPE) {
        consume();
        auto b = std::make_unique<BinaryExpr>();
        b->op = BinaryOp::BIT_OR;
        b->lhs = std::move(e);
        b->rhs = parse_exclusive_or_expression();
        e = std::move(b);
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_exclusive_or_expression() {
    auto e = parse_and_expression();
    while (peek().kind == TokenKind::CARET) {
        consume();
        auto b = std::make_unique<BinaryExpr>();
        b->op = BinaryOp::BIT_XOR;
        b->lhs = std::move(e);
        b->rhs = parse_and_expression();
        e = std::move(b);
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_and_expression() {
    auto e = parse_equality_expression();
    while (peek().kind == TokenKind::AMPER) {
        consume();
        auto b = std::make_unique<BinaryExpr>();
        b->op = BinaryOp::BIT_AND;
        b->lhs = std::move(e);
        b->rhs = parse_equality_expression();
        e = std::move(b);
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_equality_expression() {
    auto e = parse_relational_expression();
    while (peek().kind == TokenKind::EQ || peek().kind == TokenKind::NE) {
        auto op = consume().kind == TokenKind::EQ ? BinaryOp::EQ : BinaryOp::NE;
        auto b = std::make_unique<BinaryExpr>();
        b->op = op;
        b->lhs = std::move(e);
        b->rhs = parse_relational_expression();
        e = std::move(b);
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_relational_expression() {
    auto e = parse_shift_expression();
    while (peek().kind == TokenKind::LT || peek().kind == TokenKind::GT ||
           peek().kind == TokenKind::LE || peek().kind == TokenKind::GE) {
        BinaryOp op;
        switch (consume().kind) {
            case TokenKind::LT: op = BinaryOp::LT; break;
            case TokenKind::GT: op = BinaryOp::GT; break;
            case TokenKind::LE: op = BinaryOp::LE; break;
            case TokenKind::GE: op = BinaryOp::GE; break;
            default: op = BinaryOp::EQ;
        }
        auto b = std::make_unique<BinaryExpr>();
        b->op = op;
        b->lhs = std::move(e);
        b->rhs = parse_shift_expression();
        e = std::move(b);
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_shift_expression() {
    auto e = parse_additive_expression();
    while (peek().kind == TokenKind::LSHIFT || peek().kind == TokenKind::RSHIFT) {
        auto op = consume().kind == TokenKind::LSHIFT ? BinaryOp::LSHIFT : BinaryOp::RSHIFT;
        auto b = std::make_unique<BinaryExpr>();
        b->op = op;
        b->lhs = std::move(e);
        b->rhs = parse_additive_expression();
        e = std::move(b);
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_additive_expression() {
    auto e = parse_multiplicative_expression();
    while (peek().kind == TokenKind::PLUS || peek().kind == TokenKind::MINUS) {
        auto op = consume().kind == TokenKind::PLUS ? BinaryOp::ADD : BinaryOp::SUB;
        auto b = std::make_unique<BinaryExpr>();
        b->op = op;
        b->lhs = std::move(e);
        b->rhs = parse_multiplicative_expression();
        e = std::move(b);
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_multiplicative_expression() {
    auto e = parse_cast_expression();
    while (peek().kind == TokenKind::STAR || peek().kind == TokenKind::DIVIDE || peek().kind == TokenKind::MOD) {
        BinaryOp op;
        switch (consume().kind) {
            case TokenKind::STAR: op = BinaryOp::MUL; break;
            case TokenKind::DIVIDE: op = BinaryOp::DIV; break;
            case TokenKind::MOD: op = BinaryOp::MOD; break;
            default: op = BinaryOp::MUL;
        }
        auto b = std::make_unique<BinaryExpr>();
        b->op = op;
        b->lhs = std::move(e);
        b->rhs = parse_cast_expression();
        e = std::move(b);
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_cast_expression() {
    // Check for cast: (type-name) cast-expression
    if (peek().kind == TokenKind::LPAREN) {
        // Peek ahead to see if this is a type name
        size_t saved = pos;
        consume(); // (
        bool is_type = false;
        // A cast starts with a type keyword or TYPE_NAME
        auto k = peek().kind;
        if (k == TokenKind::KW_VOID || k == TokenKind::KW_CHAR || k == TokenKind::KW_SHORT ||
            k == TokenKind::KW_INT || k == TokenKind::KW_LONG || k == TokenKind::KW_FLOAT ||
            k == TokenKind::KW_DOUBLE || k == TokenKind::KW_SIGNED || k == TokenKind::KW_UNSIGNED ||
            k == TokenKind::KW__BOOL || k == TokenKind::KW__COMPLEX ||
            k == TokenKind::KW_CONST || k == TokenKind::KW_VOLATILE ||
            k == TokenKind::KW_STRUCT || k == TokenKind::KW_UNION || k == TokenKind::KW_ENUM ||
            k == TokenKind::KW_TYPEOF || k == TokenKind::KW_TYPEOF_UNQUAL ||
            k == TokenKind::TYPE_NAME) {
            is_type = true;
        }
        pos = saved;

        if (is_type) {
            consume(); // (
            Type t = parse_type_name();
            expect(TokenKind::RPAREN, "expected ')' in cast expression");
            auto c = std::make_unique<CastExpr>();
            c->cast_type = std::move(t);
            c->operand = parse_cast_expression();
            return c;
        }
    }
    return parse_unary_expression();
}

std::unique_ptr<Expr> Parser::parse_unary_expression() {
    auto& t = peek();
    switch (t.kind) {
        case TokenKind::PLUSPLUS: {
            consume();
            auto u = std::make_unique<UnaryExpr>();
            u->op = UnaryOp::PRE_INC;
            u->operand = parse_unary_expression();
            return u;
        }
        case TokenKind::MINUSMINUS: {
            consume();
            auto u = std::make_unique<UnaryExpr>();
            u->op = UnaryOp::PRE_DEC;
            u->operand = parse_unary_expression();
            return u;
        }
        case TokenKind::AMPER: {
            consume();
            auto u = std::make_unique<UnaryExpr>();
            u->op = UnaryOp::ADDR_OF;
            u->operand = parse_cast_expression();
            return u;
        }
        case TokenKind::STAR: {
            consume();
            auto u = std::make_unique<UnaryExpr>();
            u->op = UnaryOp::DEREF;
            u->operand = parse_cast_expression();
            return u;
        }
        case TokenKind::PLUS: {
            consume();
            auto u = std::make_unique<UnaryExpr>();
            u->op = UnaryOp::PLUS;
            u->operand = parse_cast_expression();
            return u;
        }
        case TokenKind::MINUS: {
            consume();
            auto u = std::make_unique<UnaryExpr>();
            u->op = UnaryOp::MINUS;
            u->operand = parse_cast_expression();
            return u;
        }
        case TokenKind::TILDE: {
            consume();
            auto u = std::make_unique<UnaryExpr>();
            u->op = UnaryOp::BIT_NOT;
            u->operand = parse_cast_expression();
            return u;
        }
        case TokenKind::NOT: {
            consume();
            auto u = std::make_unique<UnaryExpr>();
            u->op = UnaryOp::NOT;
            u->operand = parse_cast_expression();
            return u;
        }
        case TokenKind::KW_SIZEOF: {
            consume();
            auto s = std::make_unique<SizeofExpr>();
            if (peek().kind == TokenKind::LPAREN) {
                // sizeof(type) or sizeof(expr)
                size_t saved = pos;
                consume();
                // Check if the token after ( is a type keyword or TYPE_NAME
                auto k = peek().kind;
                bool type_like = (k == TokenKind::KW_VOID || k == TokenKind::KW_CHAR ||
                    k == TokenKind::KW_SHORT || k == TokenKind::KW_INT ||
                    k == TokenKind::KW_LONG || k == TokenKind::KW_FLOAT ||
                    k == TokenKind::KW_DOUBLE || k == TokenKind::KW_SIGNED ||
                    k == TokenKind::KW_UNSIGNED || k == TokenKind::KW__BOOL ||
                    k == TokenKind::KW__COMPLEX || k == TokenKind::KW_CONST ||
                    k == TokenKind::KW_VOLATILE || k == TokenKind::KW_STRUCT ||
                    k == TokenKind::KW_UNION || k == TokenKind::KW_ENUM ||
                    k == TokenKind::KW_TYPEOF || k == TokenKind::KW_TYPEOF_UNQUAL ||
                    k == TokenKind::TYPE_NAME);
                pos = saved;

                if (type_like) {
                    consume(); // (
                    s->is_type = true;
                    s->sizeof_type = parse_type_name();
                    expect(TokenKind::RPAREN, "expected ')' in sizeof(type)");
                } else {
                    s->is_type = false;
                    s->operand = parse_unary_expression();
                }
            } else {
                s->operand = parse_unary_expression();
            }
            return s;
        }
        case TokenKind::KW__ALIGNOF: {
            consume();
            auto a = std::make_unique<AlignofExpr>();
            expect(TokenKind::LPAREN, "expected '(' after _Alignof");
            a->align_type = parse_type_name();
            expect(TokenKind::RPAREN, "expected ')' after _Alignof type");
            return a;
        }
        default:
            return parse_postfix_expression();
    }
}

std::unique_ptr<Expr> Parser::parse_postfix_expression() {
    auto e = parse_primary_expression();

    while (true) {
        if (peek().kind == TokenKind::LBRACKET) {
            consume();
            auto s = std::make_unique<SubscriptExpr>();
            s->base = std::move(e);
            s->index = parse_expression();
            expect(TokenKind::RBRACKET, "expected ']' in subscript expression");
            e = std::move(s);
        } else if (peek().kind == TokenKind::LPAREN) {
            consume();
            auto c = std::make_unique<CallExpr>();
            c->callee = std::move(e);
            while (peek().kind != TokenKind::RPAREN && peek().kind != TokenKind::END_OF_FILE) {
                c->args.push_back(parse_assignment_expression());
                if (peek().kind == TokenKind::COMMA) consume();
            }
            expect(TokenKind::RPAREN, "expected ')' after function call arguments");
            e = std::move(c);
        } else if (peek().kind == TokenKind::DOT) {
            consume();
            auto m = std::make_unique<MemberExpr>();
            m->object = std::move(e);
            m->member = expect(TokenKind::IDENTIFIER, "expected member name after '.'").value;
            e = std::move(m);
        } else if (peek().kind == TokenKind::ARROW) {
            consume();
            auto m = std::make_unique<MemberExpr>();
            m->object = std::move(e);
            m->is_arrow = true;
            m->member = expect(TokenKind::IDENTIFIER, "expected member name after '->'").value;
            e = std::move(m);
        } else if (peek().kind == TokenKind::PLUSPLUS) {
            consume();
            auto u = std::make_unique<UnaryExpr>();
            u->op = UnaryOp::POST_INC;
            u->operand = std::move(e);
            e = std::move(u);
        } else if (peek().kind == TokenKind::MINUSMINUS) {
            consume();
            auto u = std::make_unique<UnaryExpr>();
            u->op = UnaryOp::POST_DEC;
            u->operand = std::move(e);
            e = std::move(u);
        } else {
            break;
        }
    }
    return e;
}

std::unique_ptr<Expr> Parser::parse_primary_expression() {
    auto& t = peek();

    switch (t.kind) {
        case TokenKind::IDENTIFIER:
        case TokenKind::TYPE_NAME: {
            consume();
            auto i = std::make_unique<IdentifierExpr>();
            i->name = t.value;
            return i;
        }
        case TokenKind::NUMBER: {
            consume();
            auto c = std::make_unique<ConstantExpr>();
            c->raw_value = t.value;
            c->value = std::strtoll(t.value.c_str(), nullptr, 0);
            return c;
        }
        case TokenKind::STRING_LITERAL: {
            consume();
            auto s = std::make_unique<StringExpr>();
            // Remove surrounding quotes
            if (t.value.size() >= 2 && t.value.front() == '"' && t.value.back() == '"') {
                s->value = t.value.substr(1, t.value.size() - 2);
            } else {
                s->value = t.value;
            }
            // Handle adjacent string literals (already concatenated by preprocessor)
            return s;
        }
        case TokenKind::CHAR_LITERAL: {
            consume();
            auto c = std::make_unique<ConstantExpr>();
            c->raw_value = t.value;
            if (t.value.size() >= 3 && t.value.front() == '\'' && t.value.back() == '\'') {
                c->value = static_cast<unsigned char>(t.value[1]);
            }
            return c;
        }
        case TokenKind::KW_TRUE: {
            consume();
            auto c = std::make_unique<ConstantExpr>();
            c->raw_value = "1";
            c->value = 1;
            return c;
        }
        case TokenKind::KW_FALSE: {
            consume();
            auto c = std::make_unique<ConstantExpr>();
            c->raw_value = "0";
            c->value = 0;
            return c;
        }
        case TokenKind::KW_NULLPTR: {
            consume();
            return std::make_unique<NullptrExpr>();
        }
        case TokenKind::LPAREN: {
            consume();
            // Could be (expression) or compound literal (type){init}
            size_t saved = pos;
            // Try to parse as expression first
            try {
                auto e = parse_expression();
                if (peek().kind == TokenKind::RPAREN) {
                    consume();
                    // Check for compound literal: (type){init}
                    if (peek().kind == TokenKind::LBRACE) {
                        auto cl = std::make_unique<CompoundLiteralExpr>();
                        // Re-parse as type
                        pos = saved;
                        consume(); // (
                        cl->literal_type = parse_type_name();
                        expect(TokenKind::RPAREN, "expected ')' in compound literal");
                        cl->init = parse_initializer();
                        return cl;
                    }
                    return e;
                }
            } catch (...) {}
            // If that fails, try as compound literal
            pos = saved;
            consume(); // (
            auto cl = std::make_unique<CompoundLiteralExpr>();
            cl->literal_type = parse_type_name();
            expect(TokenKind::RPAREN, "expected ')' in compound literal");
            cl->init = parse_initializer();
            return cl;
        }
        case TokenKind::KW__GENERIC: {
            consume();
            auto g = std::make_unique<GenericExpr>();
            expect(TokenKind::LPAREN, "expected '(' after _Generic");
            g->control = parse_assignment_expression();
            while (peek().kind == TokenKind::COMMA) {
                consume();
                GenericExpr::Association assoc;
                if (peek().kind == TokenKind::KW_DEFAULT) {
                    consume();
                    assoc.is_default = true;
                } else {
                    assoc.type = parse_type_name();
                }
                expect(TokenKind::COLON, "expected ':' in _Generic association");
                assoc.expr = parse_assignment_expression();
                g->associations.push_back(std::move(assoc));
            }
            expect(TokenKind::RPAREN, "expected ')' after _Generic");
            return g;
        }
        case TokenKind::LBRACE: {
            return parse_initializer();
        }
        default:
            throw std::runtime_error("unexpected token " + token_name(t) + " at " +
                                     t.filename + ":" + std::to_string(t.line));
    }
}

std::unique_ptr<Expr> Parser::parse_initializer() {
    if (peek().kind == TokenKind::LBRACE) {
        consume();
        auto il = std::make_unique<InitListExpr>();
        while (peek().kind != TokenKind::RBRACE && peek().kind != TokenKind::END_OF_FILE) {
            il->inits.push_back(parse_initializer());
            if (peek().kind == TokenKind::COMMA) consume();
        }
        expect(TokenKind::RBRACE, "expected '}' in initializer list");
        return il;
    }
    return parse_assignment_expression();
}

// ==================== Type Parsing ====================

Type Parser::parse_type() {
    Type base = parse_declaration_specifiers();
    std::string name;
    Type result = parse_declarator(base, name);
    return result;
}

Type Parser::parse_declaration_specifiers(bool allow_typedef) {
    Type t;
    t.prim = PrimitiveKind::INT; // default
    t.is_signed = false;
    t.is_unsigned = false;

    bool has_type = false;
    bool has_signedness = false;
    saw_typedef = false;

    while (true) {
        auto k = peek().kind;
        switch (k) {
            case TokenKind::KW_VOID:
                consume(); t.prim = PrimitiveKind::VOID; has_type = true; break;
            case TokenKind::KW_CHAR:
                consume(); t.prim = PrimitiveKind::CHAR; has_type = true; break;
            case TokenKind::KW_SHORT:
                consume(); t.prim = PrimitiveKind::SHORT; has_type = true; break;
            case TokenKind::KW_INT:
                consume(); if (!has_type) { t.prim = PrimitiveKind::INT; has_type = true; } break;
            case TokenKind::KW_LONG: {
                consume();
                if (t.prim == PrimitiveKind::LONG) t.prim = PrimitiveKind::LONGLONG;
                else if (t.prim == PrimitiveKind::DOUBLE) t.prim = PrimitiveKind::LONGDOUBLE;
                else t.prim = PrimitiveKind::LONG;
                has_type = true;
                break;
            }
            case TokenKind::KW_FLOAT:
                consume(); t.prim = PrimitiveKind::FLOAT; has_type = true; break;
            case TokenKind::KW_DOUBLE:
                consume(); t.prim = PrimitiveKind::DOUBLE; has_type = true; break;
            case TokenKind::KW_SIGNED:
                consume(); t.is_signed = true; t.is_unsigned = false; has_signedness = true; break;
            case TokenKind::KW_UNSIGNED:
                consume(); t.is_unsigned = true; t.is_signed = false; has_signedness = true; break;
            case TokenKind::KW__BOOL:
                consume(); t.prim = PrimitiveKind::BOOL; has_type = true; break;
            case TokenKind::KW__COMPLEX:
                consume(); has_type = true; break;
            case TokenKind::KW_CONST:
                consume(); t.is_const = true; break;
            case TokenKind::KW_VOLATILE:
                consume(); t.is_volatile = true; break;
            case TokenKind::KW_CONSTEXPR:
                consume(); break; // handled at decl level
            case TokenKind::KW_EXTERN:
            case TokenKind::KW_STATIC:
            case TokenKind::KW__THREAD_LOCAL:
            case TokenKind::KW_AUTO:
            case TokenKind::KW_REGISTER:
                consume(); break; // storage class handled at decl level
            case TokenKind::KW_INLINE:
            case TokenKind::KW__NORETURN:
                consume(); break;
            case TokenKind::KW_TYPEDEF:
                if (allow_typedef) { consume(); saw_typedef = true; break; }
                else goto done;
            case TokenKind::KW_STRUCT:
            case TokenKind::KW_UNION:
            case TokenKind::KW_ENUM: {
                auto st = parse_struct_or_enum_specifier();
                t.prim = st.prim;
                t.is_struct = st.is_struct;
                t.is_union = st.is_union;
                t.is_enum = st.is_enum;
                t.tag_name = st.tag_name;
                t.members = std::move(st.members);
                t.enumerators = std::move(st.enumerators);
                t.is_incomplete = st.is_incomplete;
                has_type = true;
                break;
            }
            case TokenKind::KW_TYPEOF:
            case TokenKind::KW_TYPEOF_UNQUAL: {
                consume();
                expect(TokenKind::LPAREN, "expected '(' after typeof");
                t.prim = PrimitiveKind::TYPEOF_DECLTYPE;
                t.typeof_expr = std::make_unique<Type>(parse_type_name());
                expect(TokenKind::RPAREN, "expected ')' after typeof expression");
                has_type = true;
                break;
            }
            case TokenKind::TYPE_NAME: {
                consume();
                t.is_typedef = true;
                t.typedef_name = peek(-1).value;
                has_type = true;
                goto done;
            }
            default:
                goto done;
        }
    }
    done:
    // Adjust signedness defaults
    if (!has_signedness) {
        switch (t.prim) {
            case PrimitiveKind::CHAR: break; // implementation-defined
            case PrimitiveKind::SHORT: t.is_signed = true; break;
            case PrimitiveKind::INT: t.is_signed = true; break;
            case PrimitiveKind::LONG: t.is_signed = true; break;
            case PrimitiveKind::LONGLONG: t.is_signed = true; break;
            default: t.is_signed = false; t.is_unsigned = false; break;
        }
    }
    // Map signed/unsigned
    if (t.is_unsigned && !t.is_typedef && !t.is_struct && !t.is_union && !t.is_enum) {
        switch (t.prim) {
            case PrimitiveKind::CHAR: t.prim = PrimitiveKind::U_CHAR; break;
            case PrimitiveKind::SHORT: t.prim = PrimitiveKind::U_SHORT; break;
            case PrimitiveKind::INT: t.prim = PrimitiveKind::U_INT; break;
            case PrimitiveKind::LONG: t.prim = PrimitiveKind::U_LONG; break;
            case PrimitiveKind::LONGLONG: t.prim = PrimitiveKind::U_LONGLONG; break;
            default: break;
        }
    } else if (t.is_signed && !t.is_typedef && !t.is_struct && !t.is_union && !t.is_enum) {
        switch (t.prim) {
            case PrimitiveKind::CHAR: t.prim = PrimitiveKind::S_CHAR; break;
            default: break;
        }
    }
    return t;
}

Type Parser::parse_struct_or_enum_specifier() {
    auto k = peek().kind;
    if (k == TokenKind::KW_STRUCT || k == TokenKind::KW_UNION) {
        return parse_struct_or_union_specifier();
    }
    return parse_enum_specifier();
}

Type Parser::parse_struct_or_union_specifier() {
    Type t;
    t.is_struct = (peek().kind == TokenKind::KW_STRUCT);
    t.is_union = (peek().kind == TokenKind::KW_UNION);
    consume(); // struct or union

    // Check for attributes [[...]] (C23) - skip them for now
    if (peek().kind == TokenKind::LBRACKET && peek(1).kind == TokenKind::LBRACKET) {
        consume(); consume(); // [[
        while (peek().kind != TokenKind::RBRACKET && peek().kind != TokenKind::END_OF_FILE) consume();
        consume(); // ]
        consume(); // ]
    }

    if (peek().kind == TokenKind::IDENTIFIER) {
        t.tag_name = consume().value;
    }

    if (peek().kind == TokenKind::LBRACE) {
        consume();
        while (peek().kind != TokenKind::RBRACE && peek().kind != TokenKind::END_OF_FILE) {
            auto field_type = parse_declaration_specifiers(false);
            while (peek().kind != TokenKind::SEMICOLON && peek().kind != TokenKind::RBRACE &&
                   peek().kind != TokenKind::END_OF_FILE) {
                std::string fname;
                if (peek().kind == TokenKind::IDENTIFIER) {
                    fname = consume().value;
                }
                field_type = parse_declarator(field_type, fname);

                // Bitfield
                if (peek().kind == TokenKind::COLON) {
                    consume();
                    auto bits = parse_cast_expression();
                    // store bitfield size in field decl
                    (void)bits;
                }

                t.members.push_back({field_type, fname});
                if (peek().kind == TokenKind::COMMA) consume();
            }
            expect(TokenKind::SEMICOLON, "expected ';' in struct/union member");
        }
        expect(TokenKind::RBRACE, "expected '}' in struct/union definition");
    } else {
        t.is_incomplete = true;
    }
    return t;
}

Type Parser::parse_enum_specifier() {
    Type t;
    t.is_enum = true;
    consume(); // enum

    if (peek().kind == TokenKind::IDENTIFIER) {
        t.tag_name = consume().value;
    }

    if (peek().kind == TokenKind::LBRACE) {
        consume();
        while (peek().kind != TokenKind::RBRACE && peek().kind != TokenKind::END_OF_FILE) {
            std::string ename = expect(TokenKind::IDENTIFIER, "expected enumerator name").value;
            long long eval = 0;
            if (peek().kind == TokenKind::ASSIGN) {
                consume();
                auto expr = parse_cast_expression();
                if (auto* c = dynamic_cast<ConstantExpr*>(expr.get())) {
                    eval = c->value;
                }
            }
            t.enumerators.push_back({ename, eval});
            if (peek().kind == TokenKind::COMMA) consume();
        }
        expect(TokenKind::RBRACE, "expected '}' in enum definition");
    } else {
        t.is_incomplete = true;
    }
    return t;
}

Type Parser::parse_declarator(Type base, std::string& name) {
    Type t = base;

    // Pointers
    while (peek().kind == TokenKind::STAR) {
        consume();
        Type ptr;
        ptr.is_pointer = true;
        ptr.pointee = std::make_unique<Type>(std::move(t));
        // Optional qualifiers after *
        while (peek().kind == TokenKind::KW_CONST || peek().kind == TokenKind::KW_VOLATILE ||
               peek().kind == TokenKind::KW_RESTRICT) {
            if (peek().kind == TokenKind::KW_CONST) ptr.is_const = true;
            if (peek().kind == TokenKind::KW_VOLATILE) ptr.is_volatile = true;
            consume();
        }
        t = ptr;
    }

    // Direct-declarator: either (declarator) or identifier
    if (peek().kind == TokenKind::LPAREN) {
        // Could be function declarator or grouping
        // Check if this is a function with (param-list) or (*name) or (name)
        consume();
        // If next token is a type or ), it's likely a function parameter list
        // Otherwise it's grouped declarator
        auto k = peek().kind;
        if (k == TokenKind::KW_VOID || k == TokenKind::KW_CHAR || k == TokenKind::KW_SHORT ||
            k == TokenKind::KW_INT || k == TokenKind::KW_LONG || k == TokenKind::KW_FLOAT ||
            k == TokenKind::KW_DOUBLE || k == TokenKind::KW_SIGNED || k == TokenKind::KW_UNSIGNED ||
            k == TokenKind::KW__BOOL || k == TokenKind::KW__COMPLEX ||
            k == TokenKind::KW_STRUCT || k == TokenKind::KW_UNION || k == TokenKind::KW_ENUM ||
            k == TokenKind::KW_CONST || k == TokenKind::KW_VOLATILE ||
            k == TokenKind::KW_TYPEOF || k == TokenKind::KW_TYPEOF_UNQUAL ||
            k == TokenKind::TYPE_NAME || k == TokenKind::ELLIPSIS ||
            k == TokenKind::RPAREN) {
            // Function declarator: base(params)
            Type ft;
            ft.is_function = true;
            ft.return_type = std::make_unique<Type>(std::move(t));
            ft.is_variadic = false;
            while (peek().kind != TokenKind::RPAREN && peek().kind != TokenKind::END_OF_FILE) {
                auto param = parse_parameter();
                if (auto* pv = dynamic_cast<ParamVarDecl*>(param.get())) {
                    ft.params.push_back({pv->param_type, pv->name});
                }
                if (peek().kind == TokenKind::COMMA) {
                    consume();
                    if (peek().kind == TokenKind::ELLIPSIS) {
                        consume();
                        ft.is_variadic = true;
                        break;
                    }
                }
            }
            expect(TokenKind::RPAREN, "expected ')' after function parameters");
            t = ft;
            // After function type, check for more declarator suffixes
            t = parse_declarator(t, name);
            return t;
        } else {
            // Grouped declarator: (*name) or (name)
            // peek could be STAR (pointer group) or IDENTIFIER
            std::string inner_name;
            Type inner = parse_declarator(t, inner_name);
            expect(TokenKind::RPAREN, "expected ')' in grouped declarator");
            name = inner_name;
            t = inner;
            // Continue with suffixes
            t = parse_declarator(t, name);
            return t;
        }
    }

    // Identifier
    if (peek().kind == TokenKind::IDENTIFIER || peek().kind == TokenKind::TYPE_NAME) {
        name = consume().value;
    }

    // Array declarator: name[...]
    while (peek().kind == TokenKind::LBRACKET) {
        consume();
        Type at;
        at.is_array = true;
        at.element_type = std::make_unique<Type>(std::move(t));
        if (peek().kind != TokenKind::RBRACKET) {
            // Parse array size
            if (peek().kind == TokenKind::NUMBER) {
                auto n = consume();
                at.array_size.size = std::strtoll(n.value.c_str(), nullptr, 0);
            } else if (peek().kind == TokenKind::STAR) {
                consume(); // VLA parameter
                at.array_size.size = std::string("*");
            } else {
                // VLA or static size expression
                at.array_size.size = std::string("?");
                parse_cast_expression(); // consume the expression
            }
        }
        expect(TokenKind::RBRACKET, "expected ']' in array declarator");
        t = at;
    }

    // Function declarator after name: name(...)
    while (peek().kind == TokenKind::LPAREN) {
        consume();
        Type ft;
        ft.is_function = true;
        ft.return_type = std::make_unique<Type>(std::move(t));
        while (peek().kind != TokenKind::RPAREN && peek().kind != TokenKind::END_OF_FILE) {
            auto param = parse_parameter();
            if (auto* pv = dynamic_cast<ParamVarDecl*>(param.get())) {
                ft.params.push_back({pv->param_type, pv->name});
            }
            if (peek().kind == TokenKind::COMMA) {
                consume();
                if (peek().kind == TokenKind::ELLIPSIS) {
                    consume();
                    ft.is_variadic = true;
                    break;
                }
            }
        }
        expect(TokenKind::RPAREN, "expected ')' after function parameters");
        t = ft;
    }

    return t;
}

Type Parser::parse_abstract_declarator(Type base) {
    Type t = base;

    // Pointers
    while (peek().kind == TokenKind::STAR) {
        consume();
        Type ptr;
        ptr.is_pointer = true;
        ptr.pointee = std::make_unique<Type>(std::move(t));
        t = ptr;
    }

    // Direct abstract declarator: (...) or [...]
    while (true) {
        if (peek().kind == TokenKind::LPAREN) {
            // Function type
            consume();
            Type ft;
            ft.is_function = true;
            ft.return_type = std::make_unique<Type>(std::move(t));
            while (peek().kind != TokenKind::RPAREN && peek().kind != TokenKind::END_OF_FILE) {
                auto param = parse_parameter();
                if (auto* pv = dynamic_cast<ParamVarDecl*>(param.get())) {
                    ft.params.push_back({pv->param_type, pv->name});
                }
                if (peek().kind == TokenKind::COMMA) {
                    consume();
                    if (peek().kind == TokenKind::ELLIPSIS) {
                        consume();
                        ft.is_variadic = true;
                        break;
                    }
                }
            }
            expect(TokenKind::RPAREN, "expected ')' in abstract function type");
            t = ft;
        } else if (peek().kind == TokenKind::LBRACKET) {
            consume();
            Type at;
            at.is_array = true;
            at.element_type = std::make_unique<Type>(std::move(t));
            if (peek().kind != TokenKind::RBRACKET) {
                if (peek().kind == TokenKind::NUMBER) {
                    auto n = consume();
                    at.array_size.size = std::strtoll(n.value.c_str(), nullptr, 0);
                } else if (peek().kind == TokenKind::STAR) {
                    consume();
                    at.array_size.size = std::string("*");
                } else {
                    at.array_size.size = std::string("?");
                    parse_cast_expression();
                }
            }
            expect(TokenKind::RBRACKET, "expected ']' in abstract array type");
            t = at;
        } else {
            break;
        }
    }

    return t;
}

Type Parser::parse_type_name() {
    Type base = parse_declaration_specifiers(false);
    return parse_abstract_declarator(base);
}

// ==================== Declaration Parsing ====================

std::unique_ptr<Decl> Parser::parse_declaration() {
    // Handle static_assert
    if (peek().kind == TokenKind::KW__STATIC_ASSERT || peek().kind == TokenKind::KW_STATIC_ASSERT) {
        consume();
        auto sa = std::make_unique<StaticAssertDecl>();
        expect(TokenKind::LPAREN, "expected '(' after static_assert");
        sa->condition = parse_expression();
        if (peek().kind == TokenKind::COMMA) {
            consume();
            auto str = expect(TokenKind::STRING_LITERAL, "expected string in static_assert");
            if (str.value.size() >= 2) sa->message = str.value.substr(1, str.value.size() - 2);
        }
        expect(TokenKind::RPAREN, "expected ')' after static_assert");
        expect(TokenKind::SEMICOLON, "expected ';' after static_assert");
        return sa;
    }

    // Handle struct/union/enum definition without variable
    if (peek().kind == TokenKind::KW_STRUCT || peek().kind == TokenKind::KW_UNION || peek().kind == TokenKind::KW_ENUM) {
        size_t saved = pos;
        Type t = parse_struct_or_enum_specifier();
        if (peek().kind == TokenKind::SEMICOLON) {
            consume();
            auto sd = std::make_unique<StructDecl>();
            sd->name = t.tag_name;
            sd->is_union = t.is_union;
            for (auto& m : t.members) {
                auto fd = std::make_unique<FieldDecl>();
                fd->field_type = std::move(m.first);
                fd->name = std::move(m.second);
                sd->fields.push_back(std::move(fd));
            }
            return sd;
        }
        pos = saved;
    }

    // Parse declaration specifiers
    Type base = parse_declaration_specifiers();
    bool is_typedef = saw_typedef;

    // Check for empty declaration (just specifiers)
    if (peek().kind == TokenKind::SEMICOLON) {
        consume();
        return std::make_unique<EmptyDecl>();
    }

    // Parse comma-separated init-declarators
    std::vector<std::unique_ptr<Decl>> decls;
    bool is_function_def = false;
    bool first = true;
    while (true) {
        if (!first) {
            if (peek().kind == TokenKind::COMMA) consume();
            else break;
        }
        first = false;

        std::string name;
        Type dtype = parse_declarator(base, name);

        // Check for function definition (followed by compound statement)
        if (dtype.is_function && peek().kind == TokenKind::LBRACE) {
            auto fd = parse_function_definition(dtype, name);
            decls.push_back(std::move(fd));
            is_function_def = true;
            break;
        }

        // Initializer
        std::unique_ptr<Expr> init;
        if (peek().kind == TokenKind::ASSIGN) {
            consume();
            init = parse_initializer();
        }

        if (is_typedef) {
            auto td = std::make_unique<TypedefDecl>();
            td->typedef_type = std::move(dtype);
            td->name = name;
            typedef_names.insert(name);
            decls.push_back(std::move(td));
        } else {
            auto vd = std::make_unique<VariableDecl>();
            vd->var_type = std::move(dtype);
            vd->name = std::move(name);
            vd->init = std::move(init);
            decls.push_back(std::move(vd));
        }

        if (peek().kind != TokenKind::COMMA) break;
    }

    if (!is_function_def) {
        expect(TokenKind::SEMICOLON, "expected ';' at end of declaration");
    }

    if (decls.empty()) return std::make_unique<EmptyDecl>();
    auto result = std::move(decls[0]);
    return result;
}

std::unique_ptr<FunctionDecl> Parser::parse_function_definition(Type return_type, std::string name) {
    auto fd = std::make_unique<FunctionDecl>();
    fd->func_type = std::move(return_type);
    fd->name = std::move(name);

    // Extract params from function type
    if (fd->func_type.is_function) {
        for (auto& p : fd->func_type.params) {
            auto pv = std::make_unique<ParamVarDecl>();
            pv->param_type = p.first;
            pv->name = p.second;
            fd->params.push_back(std::move(pv));
        }
    }

    fd->body = parse_compound_statement();
    return fd;
}

std::unique_ptr<VariableDecl> Parser::parse_variable_declaration(Type type, std::string name) {
    auto vd = std::make_unique<VariableDecl>();
    vd->var_type = std::move(type);
    vd->name = std::move(name);
    return vd;
}

std::unique_ptr<Decl> Parser::parse_parameter() {
    Type base = parse_declaration_specifiers(false);
    std::string name;
    Type ptype = parse_declarator(base, name);
    auto pv = std::make_unique<ParamVarDecl>();
    pv->param_type = std::move(ptype);
    pv->name = std::move(name);
    return pv;
}

std::unique_ptr<Decl> Parser::parse_struct_member() {
    return nullptr;
}

void Parser::parse_struct_body(StructDecl* sd) {
    // already handled in parse_struct_or_union_specifier
}

// ==================== Statement Parsing ====================

std::unique_ptr<Stmt> Parser::parse_statement() {
    switch (peek().kind) {
        case TokenKind::LBRACE:
            return parse_compound_statement();
        case TokenKind::KW_IF:
            return parse_if_statement();
        case TokenKind::KW_WHILE:
            return parse_while_statement();
        case TokenKind::KW_DO:
            return parse_do_statement();
        case TokenKind::KW_FOR:
            return parse_for_statement();
        case TokenKind::KW_SWITCH:
            return parse_switch_statement();
        case TokenKind::KW_CASE: {
            consume();
            auto cs = std::make_unique<CaseStmt>();
            cs->value = parse_expression();
            expect(TokenKind::COLON, "expected ':' after case value");
            cs->body = parse_statement();
            return cs;
        }
        case TokenKind::KW_DEFAULT: {
            consume();
            expect(TokenKind::COLON, "expected ':' after default");
            auto ds = std::make_unique<DefaultStmt>();
            ds->body = parse_statement();
            return ds;
        }
        case TokenKind::KW_BREAK: {
            consume();
            expect(TokenKind::SEMICOLON, "expected ';' after break");
            return std::make_unique<BreakStmt>();
        }
        case TokenKind::KW_CONTINUE: {
            consume();
            expect(TokenKind::SEMICOLON, "expected ';' after continue");
            return std::make_unique<ContinueStmt>();
        }
        case TokenKind::KW_RETURN:
            return parse_return_statement();
        case TokenKind::KW_GOTO: {
            consume();
            auto gs = std::make_unique<GotoStmt>();
            gs->label = expect(TokenKind::IDENTIFIER, "expected label in goto").value;
            expect(TokenKind::SEMICOLON, "expected ';' after goto");
            return gs;
        }
        case TokenKind::SEMICOLON: {
            consume();
            return std::make_unique<NullStmt>();
        }
        default: {
            if (is_type_specifier(peek().kind)) {
                auto ds = std::make_unique<DeclStmt>();
                ds->decl = parse_declaration();
                ds->loc = ds->decl->loc;
                return ds;
            }
            // Expression statement
            if (peek().kind == TokenKind::END_OF_FILE) {
                throw std::runtime_error("unexpected end of file");
            }
            auto stmt = std::make_unique<ExprStmt>();
            stmt->expr = parse_expression();
            expect(TokenKind::SEMICOLON, "expected ';' after expression");
            return stmt;
        }
    }
}

std::unique_ptr<CompoundStmt> Parser::parse_compound_statement() {
    expect(TokenKind::LBRACE, "expected '{'");
    auto cs = std::make_unique<CompoundStmt>();
    while (peek().kind != TokenKind::RBRACE && peek().kind != TokenKind::END_OF_FILE) {
        // Could be declaration or statement
        try {
            cs->stmts.push_back(parse_statement());
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            sync();
        }
    }
    expect(TokenKind::RBRACE, "expected '}'");
    return cs;
}

std::unique_ptr<Stmt> Parser::parse_if_statement() {
    consume(); // if
    auto is = std::make_unique<IfStmt>();
    expect(TokenKind::LPAREN, "expected '(' after if");
    is->cond = parse_expression();
    expect(TokenKind::RPAREN, "expected ')' after if condition");
    is->then_body = parse_statement();
    if (peek().kind == TokenKind::KW_ELSE) {
        consume();
        is->else_body = parse_statement();
    }
    return is;
}

std::unique_ptr<Stmt> Parser::parse_while_statement() {
    consume(); // while
    auto ws = std::make_unique<WhileStmt>();
    expect(TokenKind::LPAREN, "expected '(' after while");
    ws->cond = parse_expression();
    expect(TokenKind::RPAREN, "expected ')' after while condition");
    ws->body = parse_statement();
    return ws;
}

std::unique_ptr<Stmt> Parser::parse_do_statement() {
    consume(); // do
    auto ds = std::make_unique<DoStmt>();
    ds->body = parse_statement();
    expect(TokenKind::KW_WHILE, "expected 'while' after do body");
    expect(TokenKind::LPAREN, "expected '(' after while");
    ds->cond = parse_expression();
    expect(TokenKind::RPAREN, "expected ')' after do condition");
    expect(TokenKind::SEMICOLON, "expected ';' after do-while");
    return ds;
}

std::unique_ptr<Stmt> Parser::parse_for_statement() {
    consume(); // for
    auto fs = std::make_unique<ForStmt>();
    expect(TokenKind::LPAREN, "expected '(' after for");

    // Init clause
    if (peek().kind == TokenKind::SEMICOLON) {
        consume();
    } else if (is_type_specifier(peek().kind)) {
        auto ds = std::make_unique<DeclStmt>();
        ds->decl = parse_declaration();
        fs->init = std::move(ds);
    } else {
        auto es = std::make_unique<ExprStmt>();
        es->expr = parse_expression();
        fs->init = std::move(es);
        expect(TokenKind::SEMICOLON, "expected ';' in for loop");
    }

    // Condition clause
    if (peek().kind != TokenKind::SEMICOLON) {
        fs->cond = parse_expression();
    }
    expect(TokenKind::SEMICOLON, "expected ';' in for loop");

    // Increment clause
    if (peek().kind != TokenKind::RPAREN) {
        fs->inc = parse_expression();
    }
    expect(TokenKind::RPAREN, "expected ')' after for clauses");

    fs->body = parse_statement();
    return fs;
}

std::unique_ptr<Stmt> Parser::parse_switch_statement() {
    consume(); // switch
    auto ss = std::make_unique<SwitchStmt>();
    expect(TokenKind::LPAREN, "expected '(' after switch");
    ss->cond = parse_expression();
    expect(TokenKind::RPAREN, "expected ')' after switch condition");
    ss->body = parse_statement();
    return ss;
}

std::unique_ptr<Stmt> Parser::parse_return_statement() {
    consume(); // return
    auto rs = std::make_unique<ReturnStmt>();
    if (peek().kind != TokenKind::SEMICOLON) {
        rs->value = parse_expression();
    }
    expect(TokenKind::SEMICOLON, "expected ';' after return");
    return rs;
}

bool Parser::is_type_specifier(TokenKind k) {
    return k == TokenKind::KW_VOID || k == TokenKind::KW_CHAR ||
           k == TokenKind::KW_SHORT || k == TokenKind::KW_INT ||
           k == TokenKind::KW_LONG || k == TokenKind::KW_FLOAT ||
           k == TokenKind::KW_DOUBLE || k == TokenKind::KW_SIGNED ||
           k == TokenKind::KW_UNSIGNED || k == TokenKind::KW__BOOL ||
           k == TokenKind::KW__COMPLEX || k == TokenKind::KW_CONST ||
           k == TokenKind::KW_VOLATILE || k == TokenKind::KW_STRUCT ||
           k == TokenKind::KW_UNION || k == TokenKind::KW_ENUM ||
           k == TokenKind::KW_TYPEOF || k == TokenKind::KW_TYPEOF_UNQUAL ||
           k == TokenKind::KW__STATIC_ASSERT || k == TokenKind::KW_STATIC_ASSERT ||
           k == TokenKind::KW_EXTERN || k == TokenKind::KW_STATIC ||
           k == TokenKind::KW__THREAD_LOCAL || k == TokenKind::KW_TYPEDEF ||
           k == TokenKind::KW_CONSTEXPR || k == TokenKind::TYPE_NAME;
}

// ==================== Translation Unit ====================

std::unique_ptr<TranslationUnit> Parser::parse() {
    auto tu = std::make_unique<TranslationUnit>();
    while (peek().kind != TokenKind::END_OF_FILE) {
        try {
            // Check for function definition vs declaration
            tu->decls.push_back(parse_declaration());
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            sync();
        }
    }
    return tu;
}
