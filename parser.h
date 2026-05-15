#ifndef C_COMPILER_PARSER_H
#define C_COMPILER_PARSER_H

#include <memory>
#include <vector>
#include <string>
#include <set>
#include "token.h"
#include "ast.h"

class Parser {
public:
    explicit Parser(std::vector<ParserToken> tokens);
    std::unique_ptr<TranslationUnit> parse();

private:
    std::vector<ParserToken> tokens;
    size_t pos = 0;
    std::set<std::string> typedef_names;
    bool saw_typedef = false;

    // Helpers
    const ParserToken& peek(int offset = 0) const;
    ParserToken consume();
    ParserToken expect(TokenKind kind, const std::string& msg = "");
    bool match(TokenKind kind);
    bool match(const std::string& val);
    void sync();
    std::string token_name(const ParserToken& t) const;

    // Types
    Type parse_type();
    Type parse_declaration_specifiers(bool allow_typedef = true);
    Type parse_struct_or_enum_specifier();
    Type parse_enum_specifier();
    Type parse_struct_or_union_specifier();
    Type parse_declarator(Type base, std::string& name);
    Type parse_abstract_declarator(Type base);
    Type parse_type_name();

    // Declarations
    std::unique_ptr<Decl> parse_declaration();
    std::unique_ptr<Decl> parse_struct_member();
    std::unique_ptr<Decl> parse_parameter();
    std::unique_ptr<FunctionDecl> parse_function_definition(Type return_type, std::string name);
    std::unique_ptr<VariableDecl> parse_variable_declaration(Type type, std::string name);
    void parse_struct_body(StructDecl* sd);

    // Statements
    std::unique_ptr<Stmt> parse_statement();
    std::unique_ptr<CompoundStmt> parse_compound_statement();
    std::unique_ptr<Stmt> parse_if_statement();
    std::unique_ptr<Stmt> parse_while_statement();
    std::unique_ptr<Stmt> parse_do_statement();
    std::unique_ptr<Stmt> parse_for_statement();
    std::unique_ptr<Stmt> parse_switch_statement();
    std::unique_ptr<Stmt> parse_return_statement();

    // Expressions
    std::unique_ptr<Expr> parse_expression();
    std::unique_ptr<Expr> parse_assignment_expression();
    std::unique_ptr<Expr> parse_conditional_expression();
    std::unique_ptr<Expr> parse_logical_or_expression();
    std::unique_ptr<Expr> parse_logical_and_expression();
    std::unique_ptr<Expr> parse_inclusive_or_expression();
    std::unique_ptr<Expr> parse_exclusive_or_expression();
    std::unique_ptr<Expr> parse_and_expression();
    std::unique_ptr<Expr> parse_equality_expression();
    std::unique_ptr<Expr> parse_relational_expression();
    std::unique_ptr<Expr> parse_shift_expression();
    std::unique_ptr<Expr> parse_additive_expression();
    std::unique_ptr<Expr> parse_multiplicative_expression();
    std::unique_ptr<Expr> parse_cast_expression();
    std::unique_ptr<Expr> parse_unary_expression();
    std::unique_ptr<Expr> parse_postfix_expression();
    std::unique_ptr<Expr> parse_primary_expression();
    std::unique_ptr<Expr> parse_initializer();

    static bool is_type_specifier(TokenKind k);
};

#endif
