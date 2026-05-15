#ifndef C_COMPILER_IR_BUILDER_H
#define C_COMPILER_IR_BUILDER_H

#include "ir.h"
#include "ir_instantiator.h"
#include "ast.h"
#include <map>
#include <vector>
#include <string>

class IRBuilder {
    IRModule& module;
    size_t current_fn_idx = (size_t)-1;  // index into module.functions
    IRBlock* current_block = nullptr;
    size_t next_label_id = 0;
    IRFunction* current_fn();
    void set_current_fn(IRFunction* fn);

    struct LoopInfo {
        std::string break_label;
        std::string continue_label;
    };
    std::vector<LoopInfo> loop_stack;

    struct SwitchCase {
        long long value;
        std::string label;
    };
    struct SwitchInfo {
        size_t cond_id;
        std::string end_label;
        std::vector<SwitchCase> cases;
        std::string default_label;
        size_t next_case_idx = 0;
    };
    std::vector<SwitchInfo> switch_stack;

    // Variable scoping: stack of maps (name → alloca value ID)
    std::vector<std::map<std::string, size_t>> var_stack;

    // Template parameter names for the current generic function
    std::vector<IRType::Param> generic_params;

    std::string new_label();
    size_t new_value_id();

    size_t emit(Instruction::Opcode op, IRType result_type,
                std::vector<size_t> operands = {});
    // push_inst: like emit but for pre-built instructions (terminators).
    // Always consumes a value_id to keep IDs in sync across blocks.
    size_t push_inst(Instruction inst);

    void set_block(IRBlock* block);
    IRBlock* add_block(std::string label = "");

    // AST type -> IR type lowering
    IRType lower_type(const Type& ast_type);
    IRPrimitive lower_prim(PrimitiveKind k);

    // Variable lookup
    size_t find_var(const std::string& name);

    // Expression lowering: returns the *value* (rvalue) ID
    size_t lower_expr(const Expr& expr);
    // Expression lowering: returns the *address* (lvalue) ID
    size_t lower_expr_addr(const Expr& expr);

    size_t lower_constant(const ConstantExpr& expr);
    size_t lower_string(const StringExpr& expr);
    size_t lower_identifier_val(const IdentifierExpr& expr);
    size_t lower_identifier_addr(const IdentifierExpr& expr);
    size_t lower_binary(const BinaryExpr& expr);
    size_t lower_unary(const UnaryExpr& expr);
    size_t lower_call(const CallExpr& expr);
    size_t lower_cast(const CastExpr& expr);
    size_t lower_assign(const AssignExpr& expr);
    size_t lower_conditional(const ConditionalExpr& expr);
    size_t lower_member(const MemberExpr& expr);
    size_t lower_member_addr(const MemberExpr& expr);
    size_t lower_subscript(const SubscriptExpr& expr);
    size_t lower_comma(const CommaExpr& expr);
    size_t lower_sizeof(const SizeofExpr& expr);
    size_t lower_alignof(const AlignofExpr& expr);

    void lower_stmt(const Stmt& stmt);
    void lower_compound(const CompoundStmt& stmt);
    void lower_if(const IfStmt& stmt);
    void lower_while(const WhileStmt& stmt);
    void lower_do(const DoStmt& stmt);
    void lower_for(const ForStmt& stmt);
    void lower_switch(const SwitchStmt& stmt);
    void lower_return(const ReturnStmt& stmt);
    void lower_break(const BreakStmt& stmt);
    void lower_continue(const ContinueStmt& stmt);
    void lower_goto(const GotoStmt& stmt);
    void lower_label(const LabelStmt& stmt);
    void lower_expr_stmt(const ExprStmt& stmt);
    void lower_decl_stmt(const DeclStmt& stmt);

    void lower_decl(const Decl& decl);
    void lower_var_decl(const VariableDecl& decl);
    void lower_fn_decl(const FunctionDecl& decl);
    void lower_template_decl(const TemplateDecl& decl);
    size_t lower_template_id(const TemplateIdExpr& expr);

    void collect_switch_cases(const Stmt& stmt, std::vector<SwitchCase>& cases, std::string& default_label);

public:
    explicit IRBuilder(IRModule& mod) : module(mod) {}

    void lower_translation_unit(const TranslationUnit& tu);
};

#endif
