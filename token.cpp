#include "token.h"
#include <map>
#include <cstring>

static std::map<std::string, TokenKind> keyword_map = {
    {"auto", TokenKind::KW_AUTO},
    {"break", TokenKind::KW_BREAK},
    {"case", TokenKind::KW_CASE},
    {"char", TokenKind::KW_CHAR},
    {"const", TokenKind::KW_CONST},
    {"continue", TokenKind::KW_CONTINUE},
    {"default", TokenKind::KW_DEFAULT},
    {"do", TokenKind::KW_DO},
    {"double", TokenKind::KW_DOUBLE},
    {"else", TokenKind::KW_ELSE},
    {"enum", TokenKind::KW_ENUM},
    {"extern", TokenKind::KW_EXTERN},
    {"float", TokenKind::KW_FLOAT},
    {"for", TokenKind::KW_FOR},
    {"goto", TokenKind::KW_GOTO},
    {"if", TokenKind::KW_IF},
    {"inline", TokenKind::KW_INLINE},
    {"int", TokenKind::KW_INT},
    {"long", TokenKind::KW_LONG},
    {"register", TokenKind::KW_REGISTER},
    {"restrict", TokenKind::KW_RESTRICT},
    {"return", TokenKind::KW_RETURN},
    {"short", TokenKind::KW_SHORT},
    {"signed", TokenKind::KW_SIGNED},
    {"sizeof", TokenKind::KW_SIZEOF},
    {"static", TokenKind::KW_STATIC},
    {"struct", TokenKind::KW_STRUCT},
    {"switch", TokenKind::KW_SWITCH},
    {"typedef", TokenKind::KW_TYPEDEF},
    {"union", TokenKind::KW_UNION},
    {"unsigned", TokenKind::KW_UNSIGNED},
    {"void", TokenKind::KW_VOID},
    {"volatile", TokenKind::KW_VOLATILE},
    {"while", TokenKind::KW_WHILE},
    {"_Alignas", TokenKind::KW__ALIGNAS},
    {"_Alignof", TokenKind::KW__ALIGNOF},
    {"_Atomic", TokenKind::KW__ATOMIC},
    {"_Bool", TokenKind::KW__BOOL},
    {"_Complex", TokenKind::KW__COMPLEX},
    {"_Generic", TokenKind::KW__GENERIC},
    {"_Imaginary", TokenKind::KW__IMAGINARY},
    {"_Noreturn", TokenKind::KW__NORETURN},
    {"_Static_assert", TokenKind::KW__STATIC_ASSERT},
    {"_Thread_local", TokenKind::KW__THREAD_LOCAL},
    // C23
    {"bool", TokenKind::KW__BOOL},
    {"constexpr", TokenKind::KW_CONSTEXPR},
    {"typeof", TokenKind::KW_TYPEOF},
    {"typeof_unqual", TokenKind::KW_TYPEOF_UNQUAL},
    {"nullptr", TokenKind::KW_NULLPTR},
    {"true", TokenKind::KW_TRUE},
    {"false", TokenKind::KW_FALSE},
    {"static_assert", TokenKind::KW_STATIC_ASSERT},
    // C++ alternative tokens (map to operator equivalents)
    {"and", TokenKind::AND},
    {"or", TokenKind::OR},
    {"not", TokenKind::NOT},
    {"xor", TokenKind::CARET},
    {"bitand", TokenKind::AMPER},
    {"bitor", TokenKind::PIPE},
    {"compl", TokenKind::TILDE},
    {"and_eq", TokenKind::AND_ASSIGN},
    {"or_eq", TokenKind::OR_ASSIGN},
    {"xor_eq", TokenKind::XOR_ASSIGN},
    {"not_eq", TokenKind::NE},
    // C++ keywords
    {"alignas", TokenKind::KW_ALIGNAS},
    {"alignof", TokenKind::KW_ALIGNOF},
    {"class", TokenKind::KW_CLASS},
    {"namespace", TokenKind::KW_NAMESPACE},
    {"template", TokenKind::KW_TEMPLATE},
    {"typename", TokenKind::KW_TYPENAME},
    {"public", TokenKind::KW_PUBLIC},
    {"private", TokenKind::KW_PRIVATE},
    {"protected", TokenKind::KW_PROTECTED},
    {"virtual", TokenKind::KW_VIRTUAL},
    {"override", TokenKind::KW_OVERRIDE},
    {"final", TokenKind::KW_FINAL},
    {"explicit", TokenKind::KW_EXPLICIT},
    {"mutable", TokenKind::KW_MUTABLE},
    {"friend", TokenKind::KW_FRIEND},
    {"operator", TokenKind::KW_OPERATOR},
    {"this", TokenKind::KW_THIS},
    {"new", TokenKind::KW_NEW},
    {"delete", TokenKind::KW_DELETE},
    {"throw", TokenKind::KW_THROW},
    {"try", TokenKind::KW_TRY},
    {"catch", TokenKind::KW_CATCH},
    {"noexcept", TokenKind::KW_NOEXCEPT},
    {"decltype", TokenKind::KW_DECLTYPE},
    {"static_cast", TokenKind::KW_STATIC_CAST},
    {"dynamic_cast", TokenKind::KW_DYNAMIC_CAST},
    {"const_cast", TokenKind::KW_CONST_CAST},
    {"reinterpret_cast", TokenKind::KW_REINTERPRET_CAST},
    {"char16_t", TokenKind::KW_CHAR16_T},
    {"char32_t", TokenKind::KW_CHAR32_T},
    {"char8_t", TokenKind::KW_CHAR8_T},
    {"consteval", TokenKind::KW_CONSTEVAL},
    {"constinit", TokenKind::KW_CONSTINIT},
    {"co_await", TokenKind::KW_CO_AWAIT},
    {"co_yield", TokenKind::KW_CO_YIELD},
    {"co_return", TokenKind::KW_CO_RETURN},
    {"concept", TokenKind::KW_CONCEPT},
    {"requires", TokenKind::KW_REQUIRES},
    {"export", TokenKind::KW_EXPORT},
    {"import", TokenKind::KW_IMPORT},
    {"module", TokenKind::KW_MODULE},
};

static std::map<std::string, TokenKind> punct_map = {
    {"+", TokenKind::PLUS},
    {"-", TokenKind::MINUS},
    {"*", TokenKind::STAR},
    {"/", TokenKind::DIVIDE},
    {"%", TokenKind::MOD},
    {"++", TokenKind::PLUSPLUS},
    {"--", TokenKind::MINUSMINUS},
    {"==", TokenKind::EQ},
    {"!=", TokenKind::NE},
    {"<", TokenKind::LT},
    {">", TokenKind::GT},
    {"<=", TokenKind::LE},
    {">=", TokenKind::GE},
    {"<=>", TokenKind::SPACESHIP},
    {"&&", TokenKind::AND},
    {"||", TokenKind::OR},
    {"!", TokenKind::NOT},
    {"&", TokenKind::AMPER},
    {"|", TokenKind::PIPE},
    {"^", TokenKind::CARET},
    {"^^", TokenKind::DOUBLE_CARET},
    {"~", TokenKind::TILDE},
    {"<<", TokenKind::LSHIFT},
    {">>", TokenKind::RSHIFT},
    {"=", TokenKind::ASSIGN},
    {"+=", TokenKind::ADD_ASSIGN},
    {"-=", TokenKind::SUB_ASSIGN},
    {"*=", TokenKind::MUL_ASSIGN},
    {"/=", TokenKind::DIV_ASSIGN},
    {"%=", TokenKind::MOD_ASSIGN},
    {"&=", TokenKind::AND_ASSIGN},
    {"|=", TokenKind::OR_ASSIGN},
    {"^=", TokenKind::XOR_ASSIGN},
    {"<<=", TokenKind::LSHIFT_ASSIGN},
    {">>=", TokenKind::RSHIFT_ASSIGN},
    {".", TokenKind::DOT},
    {".*", TokenKind::DOT_STAR},
    {"->", TokenKind::ARROW},
    {"->*", TokenKind::ARROW_STAR},
    {"::", TokenKind::SCOPE},
    {"(", TokenKind::LPAREN},
    {")", TokenKind::RPAREN},
    {"[", TokenKind::LBRACKET},
    {"]", TokenKind::RBRACKET},
    {"{", TokenKind::LBRACE},
    {"}", TokenKind::RBRACE},
    {";", TokenKind::SEMICOLON},
    {":", TokenKind::COLON},
    {",", TokenKind::COMMA},
    {"?", TokenKind::QUESTION},
    {"...", TokenKind::ELLIPSIS},
    {"#", TokenKind::HASH},
    {"##", TokenKind::HASHHASH},
};

std::vector<ParserToken> refine_tokens(const std::vector<Token>& pp_tokens, std::set<std::string>& typedef_names) {
    std::vector<ParserToken> result;

    for (const auto& pt : pp_tokens) {
        if (pt.type == TokenType::WHITESPACE || pt.type == TokenType::NEWLINE) {
            continue;
        }
        if (pt.type == TokenType::END_OF_FILE) {
            result.push_back({TokenKind::END_OF_FILE, "", pt.filename, pt.line, pt.column});
            break;
        }

        ParserToken t;
        t.value = pt.value;
        t.filename = pt.filename;
        t.line = pt.line;
        t.column = pt.column;

        switch (pt.type) {
            case TokenType::IDENTIFIER: {
                auto kit = keyword_map.find(pt.value);
                if (kit != keyword_map.end()) {
                    t.kind = kit->second;
                } else if (typedef_names.count(pt.value)) {
                    t.kind = TokenKind::TYPE_NAME;
                } else {
                    t.kind = TokenKind::IDENTIFIER;
                }
                break;
            }
            case TokenType::NUMBER:
                t.kind = TokenKind::NUMBER;
                break;
            case TokenType::STRING_LITERAL:
                t.kind = TokenKind::STRING_LITERAL;
                break;
            case TokenType::CHARACTER_LITERAL:
                t.kind = TokenKind::CHAR_LITERAL;
                break;
            case TokenType::PUNCTUATOR:
            case TokenType::PREPROCESSING_OP: {
                auto pit = punct_map.find(pt.value);
                if (pit != punct_map.end()) {
                    t.kind = pit->second;
                } else {
                    t.kind = TokenKind::ERROR;
                }
                break;
            }
            case TokenType::HEADER_NAME:
                t.kind = TokenKind::STRING_LITERAL;
                t.value = "\"" + pt.value + "\"";
                break;
            default:
                t.kind = TokenKind::ERROR;
                break;
        }

        result.push_back(t);
    }

    return result;
}
