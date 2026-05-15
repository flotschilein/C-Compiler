#ifndef C_COMPILER_SEMANTIC_H
#define C_COMPILER_SEMANTIC_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include "ast.h"

struct Symbol {
    std::string name;
    Type type;
    enum Kind { VARIABLE, FUNCTION, TYPEDEF, ENUM_CONST, TAG_STRUCT, TAG_UNION, TAG_ENUM, TEMPLATE };
    Kind kind;
    int scope_level = 0;
    long long enum_value = 0;
    bool is_defined = false;
    bool is_parameter = false;
    // Template info
    std::vector<Type> template_param_types;  // for TEMPLATE kind symbols
};

class SymbolTable {
public:
    SymbolTable();
    void push_scope();
    void pop_scope();
    bool add(const Symbol& s);
    Symbol* lookup(const std::string& name);
    Symbol* lookup_current_scope(const std::string& name);

private:
    std::vector<std::map<std::string, Symbol>> scopes;
};

class SemanticAnalyzer {
public:
    SemanticAnalyzer();
    void analyze(TranslationUnit& tu);

private:
    SymbolTable symtab;
    Type current_return_type;
    int loop_depth = 0;
    int switch_depth = 0;
    std::set<std::string> template_param_names;
    std::map<std::string, std::vector<std::pair<Type, std::string>>> struct_defs;

    // Declarations
    void visit_decl(Decl& d);
    void visit_func(FunctionDecl& d);
    void visit_var(VariableDecl& d);
    void visit_typedef(TypedefDecl& d);
    void visit_struct(StructDecl& d);
    void visit_enum(EnumDecl& d);
    void visit_static_assert(StaticAssertDecl& d);
    void visit_template_decl(TemplateDecl& d);

    // Statements
    void visit_stmt(Stmt& s);
    void visit_compound(CompoundStmt& s);
    void visit_if(IfStmt& s);
    void visit_while(WhileStmt& s);
    void visit_do(DoStmt& s);
    void visit_for(ForStmt& s);
    void visit_switch(SwitchStmt& s);
    void visit_case(CaseStmt& s);
    void visit_default(DefaultStmt& s);
    void visit_break(BreakStmt& s);
    void visit_continue(ContinueStmt& s);
    void visit_return(ReturnStmt& s);
    void visit_goto(GotoStmt& s);
    void visit_label(LabelStmt& s);
    void visit_expr_stmt(ExprStmt& s);

    // Expressions (return the computed type)
    Type visit_expr(Expr& e);
    Type visit_binary(BinaryExpr& e);
    Type visit_unary(UnaryExpr& e);
    Type visit_call(CallExpr& e);
    Type visit_member(MemberExpr& e);
    Type visit_subscript(SubscriptExpr& e);
    Type visit_cast(CastExpr& e);
    Type visit_conditional(ConditionalExpr& e);
    Type visit_assign(AssignExpr& e);
    Type visit_comma(CommaExpr& e);
    Type visit_constant(ConstantExpr& e);
    Type visit_string(StringExpr& e);
    Type visit_identifier(IdentifierExpr& e);
    Type visit_sizeof(SizeofExpr& e);
    Type visit_alignof(AlignofExpr& e);
    Type visit_generic(GenericExpr& e);
    Type visit_nullptr(NullptrExpr& e);
    Type visit_compound_literal(CompoundLiteralExpr& e);
    Type visit_init_list(InitListExpr& e);
    Type visit_template_id(TemplateIdExpr& e);

    // Helpers
    void resolve_struct_type(Type& t);

    // Helpers
    bool is_arithmetic(const Type& t) const;
    bool is_integer(const Type& t) const;
    bool is_scalar(const Type& t) const;
    Type unify(const Type& a, const Type& b) const;
    void error(const std::string& msg, const SourceLoc& loc) const;
    void note(const std::string& msg, const SourceLoc& loc) const;

    // Type conversion and comparison
    int type_rank(const Type& t) const;
    bool types_equal(const Type& a, const Type& b) const;
};

#endif
