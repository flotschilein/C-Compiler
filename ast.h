#ifndef C_COMPILER_AST_H
#define C_COMPILER_AST_H

#include <string>
#include <vector>
#include <memory>
#include <variant>

struct SourceLoc {
    std::string filename;
    int line = 0;
    int column = 0;
};

// --- Types ---

enum class PrimitiveKind {
    VOID, BOOL, CHAR, S_CHAR, U_CHAR, SHORT, U_SHORT, INT, U_INT,
    LONG, U_LONG, LONGLONG, U_LONGLONG, FLOAT, DOUBLE, LONGDOUBLE,
    COMPLEX_FLOAT, COMPLEX_DOUBLE, COMPLEX_LONGDOUBLE,
    TYPEOF_DECLTYPE
};

struct ArraySize {
    std::variant<std::monostate, long long, std::string> size;  // unspecified, constant, identifier (VLA)
};

struct Type {
    PrimitiveKind prim = PrimitiveKind::INT;
    bool is_const = false;
    bool is_volatile = false;
    bool is_signed = true;
    bool is_unsigned = false;
    bool is_typedef = false;
    std::string typedef_name;
    bool is_struct = false;
    bool is_union = false;
    bool is_enum = false;
    std::string tag_name;
    bool is_incomplete = false;

    // struct/union members
    bool has_members = false;
    std::vector<std::pair<Type, std::string>> members;

    // enum
    std::vector<std::pair<std::string, long long>> enumerators;

    // function
    bool is_function = false;
    bool is_variadic = false;
    std::unique_ptr<Type> return_type;
    std::vector<std::pair<Type, std::string>> params;

    // pointer
    bool is_pointer = false;
    std::unique_ptr<Type> pointee;

    // array
    bool is_array = false;
    std::unique_ptr<Type> element_type;
    ArraySize array_size;

    // typeof
    std::unique_ptr<Type> typeof_expr;

    // template type (e.g. `Vector<int>`)
    bool is_template_type = false;
    std::string template_name;
    std::vector<Type> template_args;

    Type() = default;
    Type(const Type& other);
    Type& operator=(const Type& other);
    Type(Type&& other) = default;
    Type& operator=(Type&& other) = default;
};

// --- AST Nodes ---

struct ASTNode {
    SourceLoc loc;
    virtual ~ASTNode() = default;
    virtual void dump(int indent = 0) const = 0;
};

struct Decl : ASTNode {};
struct Stmt : ASTNode {};
struct Expr : ASTNode {
    Type result_type;
};

// --- Declarations ---

struct TranslationUnit : ASTNode {
    std::vector<std::unique_ptr<Decl>> decls;
    void dump(int indent = 0) const override;
};

struct FunctionDecl : Decl {
    Type func_type;
    std::string name;
    std::vector<std::unique_ptr<Decl>> params;
    std::unique_ptr<Stmt> body;
    bool is_old_style = false;
    void dump(int indent = 0) const override;
};

struct VariableDecl : Decl {
    Type var_type;
    std::string name;
    std::unique_ptr<Expr> init;
    bool is_static = false;
    bool is_extern = false;
    bool is_thread_local = false;
    bool is_constexpr = false;
    void dump(int indent = 0) const override;
};

struct TypedefDecl : Decl {
    Type typedef_type;
    std::string name;
    void dump(int indent = 0) const override;
};

struct FieldDecl : Decl {
    Type field_type;
    std::string name;
    std::unique_ptr<Expr> bitfield_size;
    void dump(int indent = 0) const override;
};

struct ParamVarDecl : Decl {
    Type param_type;
    std::string name;
    void dump(int indent = 0) const override;
};

struct StructDecl : Decl {
    std::string name;
    bool is_union = false;
    std::vector<std::unique_ptr<Decl>> fields;
    void dump(int indent = 0) const override;
};

struct EnumDecl : Decl {
    std::string name;
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> enumerators;
    void dump(int indent = 0) const override;
};

struct StaticAssertDecl : Decl {
    std::unique_ptr<Expr> condition;
    std::string message;
    void dump(int indent = 0) const override;
};

struct EmptyDecl : Decl {
    void dump(int indent = 0) const override;
};

// --- Template ---

struct TemplateParamDecl : Decl {
    bool is_type_param = true;  // true: "typename T", false: "int N"
    Type param_type;            // for non-type params (e.g. int)
    std::string name;           // "T", "N"
    void dump(int indent = 0) const override;
};

struct TemplateDecl : Decl {
    std::vector<std::unique_ptr<TemplateParamDecl>> params;
    std::unique_ptr<Decl> wrapped_decl;  // FunctionDecl, StructDecl, etc.
    void dump(int indent = 0) const override;
};

// --- Statements ---

struct CompoundStmt : Stmt {
    std::vector<std::unique_ptr<Stmt>> stmts;
    void dump(int indent = 0) const override;
};

struct ExprStmt : Stmt {
    std::unique_ptr<Expr> expr;
    void dump(int indent = 0) const override;
};

struct DeclStmt : Stmt {
    std::unique_ptr<Decl> decl;
    void dump(int indent = 0) const override;
};

struct IfStmt : Stmt {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> then_body;
    std::unique_ptr<Stmt> else_body;
    void dump(int indent = 0) const override;
};

struct WhileStmt : Stmt {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> body;
    void dump(int indent = 0) const override;
};

struct DoStmt : Stmt {
    std::unique_ptr<Stmt> body;
    std::unique_ptr<Expr> cond;
    void dump(int indent = 0) const override;
};

struct ForStmt : Stmt {
    std::unique_ptr<Stmt> init;
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Expr> inc;
    std::unique_ptr<Stmt> body;
    void dump(int indent = 0) const override;
};

struct SwitchStmt : Stmt {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Stmt> body;
    void dump(int indent = 0) const override;
};

struct CaseStmt : Stmt {
    std::unique_ptr<Expr> value;
    std::unique_ptr<Stmt> body;
    void dump(int indent = 0) const override;
};

struct DefaultStmt : Stmt {
    std::unique_ptr<Stmt> body;
    void dump(int indent = 0) const override;
};

struct BreakStmt : Stmt {
    void dump(int indent = 0) const override;
};

struct ContinueStmt : Stmt {
    void dump(int indent = 0) const override;
};

struct ReturnStmt : Stmt {
    std::unique_ptr<Expr> value;
    void dump(int indent = 0) const override;
};

struct GotoStmt : Stmt {
    std::string label;
    void dump(int indent = 0) const override;
};

struct LabelStmt : Stmt {
    std::string name;
    std::unique_ptr<Stmt> stmt;
    void dump(int indent = 0) const override;
};

struct NullStmt : Stmt {
    void dump(int indent = 0) const override;
};

// --- Expressions ---

enum class BinaryOp {
    ADD, SUB, MUL, DIV, MOD,
    EQ, NE, LT, GT, LE, GE,
    AND, OR,
    BIT_AND, BIT_OR, BIT_XOR,
    LSHIFT, RSHIFT
};

enum class UnaryOp {
    PLUS, MINUS, NOT, BIT_NOT,
    DEREF, ADDR_OF,
    PRE_INC, PRE_DEC,
    POST_INC, POST_DEC
};

enum class AssignOp {
    ASSIGN, ADD, SUB, MUL, DIV, MOD,
    AND, OR, XOR, LSHIFT, RSHIFT
};

struct BinaryExpr : Expr {
    BinaryOp op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    void dump(int indent = 0) const override;
};

struct UnaryExpr : Expr {
    UnaryOp op;
    std::unique_ptr<Expr> operand;
    void dump(int indent = 0) const override;
};

struct CallExpr : Expr {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
    void dump(int indent = 0) const override;
};

struct MemberExpr : Expr {
    std::unique_ptr<Expr> object;
    std::string member;
    bool is_arrow = false;
    void dump(int indent = 0) const override;
};

struct SubscriptExpr : Expr {
    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> index;
    void dump(int indent = 0) const override;
};

struct CastExpr : Expr {
    Type cast_type;
    std::unique_ptr<Expr> operand;
    void dump(int indent = 0) const override;
};

struct ConditionalExpr : Expr {
    std::unique_ptr<Expr> cond;
    std::unique_ptr<Expr> then_expr;
    std::unique_ptr<Expr> else_expr;
    void dump(int indent = 0) const override;
};

struct AssignExpr : Expr {
    AssignOp op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    void dump(int indent = 0) const override;
};

struct CommaExpr : Expr {
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    void dump(int indent = 0) const override;
};

struct ConstantExpr : Expr {
    long long value = 0;
    std::string raw_value;
    void dump(int indent = 0) const override;
};

struct StringExpr : Expr {
    std::string value;
    void dump(int indent = 0) const override;
};

struct IdentifierExpr : Expr {
    std::string name;
    void dump(int indent = 0) const override;
};

struct SizeofExpr : Expr {
    bool is_type = false;
    Type sizeof_type;
    std::unique_ptr<Expr> operand;
    void dump(int indent = 0) const override;
};

struct AlignofExpr : Expr {
    Type align_type;
    void dump(int indent = 0) const override;
};

struct GenericExpr : Expr {
    std::unique_ptr<Expr> control;
    struct Association {
        Type type;
        bool is_default = false;
        std::unique_ptr<Expr> expr;
    };
    std::vector<Association> associations;
    void dump(int indent = 0) const override;
};

struct NullptrExpr : Expr {
    void dump(int indent = 0) const override;
};

struct CompoundLiteralExpr : Expr {
    Type literal_type;
    std::unique_ptr<Expr> init;
    void dump(int indent = 0) const override;
};

struct InitListExpr : Expr {
    std::vector<std::unique_ptr<Expr>> inits;
    void dump(int indent = 0) const override;
};

struct TemplateIdExpr : Expr {
    std::string template_name;
    std::vector<Type> template_args;
    std::vector<std::unique_ptr<Expr>> call_args;  // function call args (if any)
    void dump(int indent = 0) const override;
};

#endif
