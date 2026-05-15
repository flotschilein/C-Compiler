#include "ast.h"
#include <iostream>

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) std::cout << "  ";
}

static const char* prim_name(PrimitiveKind k) {
    switch (k) {
        case PrimitiveKind::VOID: return "void";
        case PrimitiveKind::BOOL: return "_Bool";
        case PrimitiveKind::CHAR: return "char";
        case PrimitiveKind::S_CHAR: return "signed char";
        case PrimitiveKind::U_CHAR: return "unsigned char";
        case PrimitiveKind::SHORT: return "short";
        case PrimitiveKind::U_SHORT: return "unsigned short";
        case PrimitiveKind::INT: return "int";
        case PrimitiveKind::U_INT: return "unsigned int";
        case PrimitiveKind::LONG: return "long";
        case PrimitiveKind::U_LONG: return "unsigned long";
        case PrimitiveKind::LONGLONG: return "long long";
        case PrimitiveKind::U_LONGLONG: return "unsigned long long";
        case PrimitiveKind::FLOAT: return "float";
        case PrimitiveKind::DOUBLE: return "double";
        case PrimitiveKind::LONGDOUBLE: return "long double";
        case PrimitiveKind::COMPLEX_FLOAT: return "float _Complex";
        case PrimitiveKind::COMPLEX_DOUBLE: return "double _Complex";
        case PrimitiveKind::COMPLEX_LONGDOUBLE: return "long double _Complex";
        case PrimitiveKind::TYPEOF_DECLTYPE: return "typeof";
    }
    return "?";
}

static void dump_type(const Type& t) {
    if (t.is_typedef) { std::cout << t.typedef_name; return; }
    if (t.is_struct || t.is_union) {
        std::cout << (t.is_struct ? "struct " : "union ") << t.tag_name;
        return;
    }
    if (t.is_enum) { std::cout << "enum " << t.tag_name; return; }
    if (t.is_const) std::cout << "const ";
    if (t.is_volatile) std::cout << "volatile ";
    if (t.is_function) {
        std::cout << "function(";
        for (size_t i = 0; i < t.params.size(); i++) {
            if (i) std::cout << ", ";
            dump_type(t.params[i].first);
            if (!t.params[i].second.empty()) std::cout << " " << t.params[i].second;
        }
        if (t.is_variadic) { if (!t.params.empty()) std::cout << ", "; std::cout << "..."; }
        std::cout << ") -> ";
        if (t.return_type) dump_type(*t.return_type);
        return;
    }
    if (t.is_pointer) { if (t.pointee) dump_type(*t.pointee); std::cout << "*"; return; }
    if (t.is_array) {
        if (t.element_type) dump_type(*t.element_type);
        std::cout << "[";
        if (auto* s = std::get_if<long long>(&t.array_size.size)) std::cout << *s;
        else if (std::get_if<std::string>(&t.array_size.size)) std::cout << "?";
        std::cout << "]";
        return;
    }
    std::cout << prim_name(t.prim);
}

// --- dump implementations ---

Type::Type(const Type& other)
    : prim(other.prim),
      is_const(other.is_const), is_volatile(other.is_volatile),
      is_signed(other.is_signed), is_unsigned(other.is_unsigned),
      is_typedef(other.is_typedef), typedef_name(other.typedef_name),
      is_struct(other.is_struct), is_union(other.is_union),
      is_enum(other.is_enum), tag_name(other.tag_name),
      is_incomplete(other.is_incomplete),
      has_members(other.has_members),
      members(other.members),
      enumerators(other.enumerators),
      is_function(other.is_function), is_variadic(other.is_variadic),
      params(other.params),
      is_pointer(other.is_pointer),
      is_array(other.is_array),
      array_size(other.array_size)
{
    if (other.return_type) return_type = std::make_unique<Type>(*other.return_type);
    if (other.pointee) pointee = std::make_unique<Type>(*other.pointee);
    if (other.element_type) element_type = std::make_unique<Type>(*other.element_type);
    if (other.typeof_expr) typeof_expr = std::make_unique<Type>(*other.typeof_expr);
}

Type& Type::operator=(const Type& other) {
    if (this == &other) return *this;
    prim = other.prim;
    is_const = other.is_const; is_volatile = other.is_volatile;
    is_signed = other.is_signed; is_unsigned = other.is_unsigned;
    is_typedef = other.is_typedef; typedef_name = other.typedef_name;
    is_struct = other.is_struct; is_union = other.is_union;
    is_enum = other.is_enum; tag_name = other.tag_name;
    is_incomplete = other.is_incomplete;
    has_members = other.has_members;
    members = other.members;
    enumerators = other.enumerators;
    is_function = other.is_function; is_variadic = other.is_variadic;
    params = other.params;
    is_pointer = other.is_pointer;
    is_array = other.is_array;
    array_size = other.array_size;
    return_type = other.return_type ? std::make_unique<Type>(*other.return_type) : nullptr;
    pointee = other.pointee ? std::make_unique<Type>(*other.pointee) : nullptr;
    element_type = other.element_type ? std::make_unique<Type>(*other.element_type) : nullptr;
    typeof_expr = other.typeof_expr ? std::make_unique<Type>(*other.typeof_expr) : nullptr;
    return *this;
}

void TranslationUnit::dump(int indent) const {
    std::cout << "TranslationUnit\n";
    for (const auto& d : decls) {
        d->dump(indent + 1);
    }
}

void FunctionDecl::dump(int indent) const {
    print_indent(indent);
    std::cout << "FunctionDecl ";
    dump_type(func_type);
    std::cout << " " << name << "\n";
    for (const auto& p : params) p->dump(indent + 1);
    if (body) body->dump(indent + 1);
}

void VariableDecl::dump(int indent) const {
    print_indent(indent);
    if (is_static) std::cout << "static ";
    if (is_extern) std::cout << "extern ";
    if (is_constexpr) std::cout << "constexpr ";
    std::cout << "VarDecl ";
    dump_type(var_type);
    std::cout << " " << name;
    if (init) { std::cout << " = "; init->dump(0); }
    std::cout << "\n";
}

void TypedefDecl::dump(int indent) const {
    print_indent(indent);
    std::cout << "Typedef ";
    dump_type(typedef_type);
    std::cout << " " << name << "\n";
}

void FieldDecl::dump(int indent) const {
    print_indent(indent);
    std::cout << "Field ";
    dump_type(field_type);
    std::cout << " " << name;
    if (bitfield_size) { std::cout << " : "; bitfield_size->dump(0); }
    std::cout << "\n";
}

void ParamVarDecl::dump(int indent) const {
    print_indent(indent);
    std::cout << "Param ";
    dump_type(param_type);
    std::cout << " " << name << "\n";
}

void StructDecl::dump(int indent) const {
    print_indent(indent);
    std::cout << (is_union ? "UnionDecl" : "StructDecl") << " " << name << "\n";
    for (const auto& f : fields) f->dump(indent + 1);
}

void EnumDecl::dump(int indent) const {
    print_indent(indent);
    std::cout << "EnumDecl " << name << "\n";
    for (const auto& e : enumerators) {
        print_indent(indent + 1);
        std::cout << "Enumerator " << e.first;
        if (e.second) { std::cout << " = "; e.second->dump(0); }
        std::cout << "\n";
    }
}

void StaticAssertDecl::dump(int indent) const {
    print_indent(indent);
    std::cout << "StaticAssert ";
    if (condition) condition->dump(0);
    std::cout << " \"" << message << "\"\n";
}

void EmptyDecl::dump(int indent) const {
    print_indent(indent);
    std::cout << "EmptyDecl\n";
}

void CompoundStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "CompoundStmt\n";
    for (const auto& s : stmts) s->dump(indent + 1);
}

void ExprStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "ExprStmt ";
    if (expr) expr->dump(0);
    std::cout << "\n";
}

void IfStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "IfStmt\n";
    if (cond) { print_indent(indent + 1); std::cout << "Cond: "; cond->dump(0); std::cout << "\n"; }
    if (then_body) then_body->dump(indent + 1);
    if (else_body) {
        print_indent(indent);
        std::cout << "Else:\n";
        else_body->dump(indent + 1);
    }
}

void WhileStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "WhileStmt\n";
    if (cond) { print_indent(indent + 1); std::cout << "Cond: "; cond->dump(0); std::cout << "\n"; }
    if (body) body->dump(indent + 1);
}

void DoStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "DoStmt\n";
    if (body) body->dump(indent + 1);
    if (cond) { print_indent(indent + 1); std::cout << "Cond: "; cond->dump(0); std::cout << "\n"; }
}

void ForStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "ForStmt\n";
    if (init) init->dump(indent + 1);
    if (cond) { print_indent(indent + 1); std::cout << "Cond: "; cond->dump(0); std::cout << "\n"; }
    if (inc) { print_indent(indent + 1); std::cout << "Inc: "; inc->dump(0); std::cout << "\n"; }
    if (body) body->dump(indent + 1);
}

void SwitchStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "SwitchStmt\n";
    if (cond) { print_indent(indent + 1); std::cout << "Cond: "; cond->dump(0); std::cout << "\n"; }
    if (body) body->dump(indent + 1);
}

void CaseStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "CaseStmt ";
    if (value) value->dump(0);
    std::cout << "\n";
    if (body) body->dump(indent + 1);
}

void DefaultStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "DefaultStmt\n";
    if (body) body->dump(indent + 1);
}

void BreakStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "BreakStmt\n";
}

void ContinueStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "ContinueStmt\n";
}

void ReturnStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "ReturnStmt";
    if (value) { std::cout << " "; value->dump(0); }
    std::cout << "\n";
}

void GotoStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "GotoStmt " << label << "\n";
}

void LabelStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "LabelStmt " << name << "\n";
    if (stmt) stmt->dump(indent + 1);
}

void DeclStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "DeclStmt\n";
    if (decl) decl->dump(indent + 1);
}

void NullStmt::dump(int indent) const {
    print_indent(indent);
    std::cout << "NullStmt\n";
}

static const char* binop_name(BinaryOp op) {
    switch (op) {
        case BinaryOp::ADD: return "+"; case BinaryOp::SUB: return "-";
        case BinaryOp::MUL: return "*"; case BinaryOp::DIV: return "/";
        case BinaryOp::MOD: return "%"; case BinaryOp::EQ: return "==";
        case BinaryOp::NE: return "!="; case BinaryOp::LT: return "<";
        case BinaryOp::GT: return ">"; case BinaryOp::LE: return "<=";
        case BinaryOp::GE: return ">="; case BinaryOp::AND: return "&&";
        case BinaryOp::OR: return "||"; case BinaryOp::BIT_AND: return "&";
        case BinaryOp::BIT_OR: return "|"; case BinaryOp::BIT_XOR: return "^";
        case BinaryOp::LSHIFT: return "<<"; case BinaryOp::RSHIFT: return ">>";
    }
    return "?";
}

static const char* unop_name(UnaryOp op) {
    switch (op) {
        case UnaryOp::PLUS: return "+"; case UnaryOp::MINUS: return "-";
        case UnaryOp::NOT: return "!"; case UnaryOp::BIT_NOT: return "~";
        case UnaryOp::DEREF: return "*"; case UnaryOp::ADDR_OF: return "&";
        case UnaryOp::PRE_INC: return "++"; case UnaryOp::PRE_DEC: return "--";
        case UnaryOp::POST_INC: return "++"; case UnaryOp::POST_DEC: return "--";
    }
    return "?";
}

static const char* asop_name(AssignOp op) {
    switch (op) {
        case AssignOp::ASSIGN: return "="; case AssignOp::ADD: return "+=";
        case AssignOp::SUB: return "-="; case AssignOp::MUL: return "*=";
        case AssignOp::DIV: return "/="; case AssignOp::MOD: return "%=";
        case AssignOp::AND: return "&="; case AssignOp::OR: return "|=";
        case AssignOp::XOR: return "^="; case AssignOp::LSHIFT: return "<<=";
        case AssignOp::RSHIFT: return ">>=";
    }
    return "?";
}

void BinaryExpr::dump(int indent) const {
    std::cout << "(";
    if (lhs) lhs->dump(0);
    std::cout << " " << binop_name(op) << " ";
    if (rhs) rhs->dump(0);
    std::cout << ")";
}

void UnaryExpr::dump(int indent) const {
    std::cout << unop_name(op);
    if (operand) operand->dump(0);
}

void CallExpr::dump(int indent) const {
    if (callee) callee->dump(0);
    std::cout << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (i) std::cout << ", ";
        if (args[i]) args[i]->dump(0);
    }
    std::cout << ")";
}

void MemberExpr::dump(int indent) const {
    if (object) object->dump(0);
    std::cout << (is_arrow ? "->" : ".") << member;
}

void SubscriptExpr::dump(int indent) const {
    if (base) base->dump(0);
    std::cout << "[";
    if (index) index->dump(0);
    std::cout << "]";
}

void CastExpr::dump(int indent) const {
    std::cout << "(cast:";
    dump_type(cast_type);
    std::cout << ")";
    if (operand) operand->dump(0);
}

void ConditionalExpr::dump(int indent) const {
    if (cond) cond->dump(0);
    std::cout << " ? ";
    if (then_expr) then_expr->dump(0);
    std::cout << " : ";
    if (else_expr) else_expr->dump(0);
}

void AssignExpr::dump(int indent) const {
    if (lhs) lhs->dump(0);
    std::cout << " " << asop_name(op) << " ";
    if (rhs) rhs->dump(0);
}

void CommaExpr::dump(int indent) const {
    if (lhs) lhs->dump(0);
    std::cout << ", ";
    if (rhs) rhs->dump(0);
}

void ConstantExpr::dump(int indent) const {
    std::cout << raw_value;
}

void StringExpr::dump(int indent) const {
    std::cout << "\"" << value << "\"";
}

void IdentifierExpr::dump(int indent) const {
    std::cout << name;
}

void SizeofExpr::dump(int indent) const {
    std::cout << "sizeof(";
    if (is_type) {
        dump_type(sizeof_type);
    } else if (operand) {
        operand->dump(0);
    }
    std::cout << ")";
}

void AlignofExpr::dump(int indent) const {
    std::cout << "_Alignof(";
    dump_type(align_type);
    std::cout << ")";
}

void GenericExpr::dump(int indent) const {
    std::cout << "_Generic(";
    if (control) control->dump(0);
    for (const auto& a : associations) {
        std::cout << ", ";
        if (a.is_default) std::cout << "default";
        else dump_type(a.type);
        std::cout << ": ";
        if (a.expr) a.expr->dump(0);
    }
    std::cout << ")";
}

void NullptrExpr::dump(int indent) const {
    std::cout << "nullptr";
}

void CompoundLiteralExpr::dump(int indent) const {
    std::cout << "(compound_literal:";
    dump_type(literal_type);
    std::cout << ")";
    if (init) init->dump(0);
}

void InitListExpr::dump(int indent) const {
    std::cout << "{";
    for (size_t i = 0; i < inits.size(); i++) {
        if (i) std::cout << ", ";
        if (inits[i]) inits[i]->dump(0);
    }
    std::cout << "}";
}
