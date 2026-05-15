#include "semantic.h"
#include <iostream>

// ==================== SymbolTable ====================

SymbolTable::SymbolTable() {
    scopes.emplace_back(); // global scope
}

void SymbolTable::push_scope() {
    scopes.emplace_back();
}

void SymbolTable::pop_scope() {
    if (scopes.size() > 1) scopes.pop_back();
}

bool SymbolTable::add(const Symbol& s) {
    auto& cur = scopes.back();
    if (cur.count(s.name)) return false;
    cur[s.name] = s;
    return true;
}

Symbol* SymbolTable::lookup(const std::string& name) {
    for (int i = (int)scopes.size() - 1; i >= 0; i--) {
        auto it = scopes[i].find(name);
        if (it != scopes[i].end()) return &it->second;
    }
    return nullptr;
}

Symbol* SymbolTable::lookup_current_scope(const std::string& name) {
    auto it = scopes.back().find(name);
    if (it != scopes.back().end()) return &it->second;
    return nullptr;
}

// ==================== SemanticAnalyzer ====================

SemanticAnalyzer::SemanticAnalyzer() {
    symtab.push_scope(); // global
}

void SemanticAnalyzer::analyze(TranslationUnit& tu) {
    for (auto& d : tu.decls) {
        if (d) visit_decl(*d);
    }
}

void SemanticAnalyzer::error(const std::string& msg, const SourceLoc& loc) const {
    std::cerr << "error: " << msg << " at " << loc.filename << ":" << loc.line << "\n";
}

void SemanticAnalyzer::note(const std::string& msg, const SourceLoc& loc) const {
    std::cerr << "note: " << msg << " at " << loc.filename << ":" << loc.line << "\n";
}

bool SemanticAnalyzer::is_integer(const Type& t) const {
    if (t.is_typedef) return is_integer(t); // handled via lookup
    if (t.is_pointer || t.is_array || t.is_function) return false;
    if (t.is_struct || t.is_union || t.is_enum) return t.is_enum;
    switch (t.prim) {
        case PrimitiveKind::VOID:
        case PrimitiveKind::FLOAT:
        case PrimitiveKind::DOUBLE:
        case PrimitiveKind::LONGDOUBLE:
        case PrimitiveKind::COMPLEX_FLOAT:
        case PrimitiveKind::COMPLEX_DOUBLE:
        case PrimitiveKind::COMPLEX_LONGDOUBLE:
        case PrimitiveKind::TYPEOF_DECLTYPE:
            return false;
        default:
            return true;
    }
}

bool SemanticAnalyzer::is_arithmetic(const Type& t) const {
    if (t.is_typedef) return is_arithmetic(t);
    if (t.is_pointer || t.is_array || t.is_function) return false;
    if (t.is_struct || t.is_union) return false;
    if (t.is_enum) return true;
    return true; // all primitives are arithmetic (void is not really, but we filter in ops)
}

bool SemanticAnalyzer::is_scalar(const Type& t) const {
    if (t.is_typedef) return is_scalar(t);
    if (t.is_pointer || t.is_array) return true;
    if (t.is_function) return false;
    if (t.is_struct || t.is_union) return false;
    if (t.prim == PrimitiveKind::VOID) return false;
    return true;
}

int SemanticAnalyzer::type_rank(const Type& t) const {
    // C type rank for usual arithmetic conversions
    if (t.is_typedef) return type_rank(t);
    if (t.is_enum) return 11;
    if (t.is_pointer || t.is_array) return 20;
    switch (t.prim) {
        case PrimitiveKind::BOOL: return 1;
        case PrimitiveKind::CHAR:
        case PrimitiveKind::S_CHAR:
        case PrimitiveKind::U_CHAR: return 2;
        case PrimitiveKind::SHORT:
        case PrimitiveKind::U_SHORT: return 3;
        case PrimitiveKind::INT:
        case PrimitiveKind::U_INT: return 4;
        case PrimitiveKind::LONG:
        case PrimitiveKind::U_LONG: return 5;
        case PrimitiveKind::LONGLONG:
        case PrimitiveKind::U_LONGLONG: return 6;
        case PrimitiveKind::FLOAT: return 7;
        case PrimitiveKind::DOUBLE: return 8;
        case PrimitiveKind::LONGDOUBLE: return 9;
        case PrimitiveKind::COMPLEX_FLOAT: return 10;
        case PrimitiveKind::COMPLEX_DOUBLE: return 11;
        case PrimitiveKind::COMPLEX_LONGDOUBLE: return 12;
        default: return 0;
    }
}

bool SemanticAnalyzer::types_equal(const Type& a, const Type& b) const {
    if (a.is_typedef || b.is_typedef) return &a == &b; // pointer comparison for typedefs
    if (a.is_pointer && b.is_pointer) return types_equal(*a.pointee, *b.pointee);
    if (a.is_array && b.is_array) return types_equal(*a.element_type, *b.element_type);
    if (a.is_function && b.is_function) {
        if (!types_equal(*a.return_type, *b.return_type)) return false;
        if (a.params.size() != b.params.size()) return false;
        for (size_t i = 0; i < a.params.size(); i++) {
            if (!types_equal(a.params[i].first, b.params[i].first)) return false;
        }
        return true;
    }
    if (a.is_struct && b.is_struct) return a.tag_name == b.tag_name;
    if (a.is_union && b.is_union) return a.tag_name == b.tag_name;
    if (a.is_enum && b.is_enum) return a.tag_name == b.tag_name;
    return a.prim == b.prim && a.is_const == b.is_const && a.is_volatile == b.is_volatile;
}

Type SemanticAnalyzer::unify(const Type& a, const Type& b) const {
    // Usual arithmetic conversions
    int ra = type_rank(a);
    int rb = type_rank(b);
    if (ra >= rb) return a;
    return b;
}

// ==================== Declarations ====================

void SemanticAnalyzer::visit_decl(Decl& d) {
    if (auto* fd = dynamic_cast<FunctionDecl*>(&d)) {
        visit_func(*fd);
    } else if (auto* vd = dynamic_cast<VariableDecl*>(&d)) {
        visit_var(*vd);
    } else if (auto* td = dynamic_cast<TypedefDecl*>(&d)) {
        visit_typedef(*td);
    } else if (auto* sd = dynamic_cast<StructDecl*>(&d)) {
        visit_struct(*sd);
    } else if (auto* ed = dynamic_cast<EnumDecl*>(&d)) {
        visit_enum(*ed);
    } else if (auto* sa = dynamic_cast<StaticAssertDecl*>(&d)) {
        visit_static_assert(*sa);
    }
    // EmptyDecl: nothing to do
}

void SemanticAnalyzer::visit_func(FunctionDecl& d) {
    // Check if already declared
    Symbol* existing = symtab.lookup(d.name);
    if (existing && existing->kind == Symbol::FUNCTION) {
        if (existing->is_defined && d.body) {
            error("redefinition of function '" + d.name + "'", d.loc);
            return;
        }
    }

    // Add function to symbol table
    Symbol s;
    s.name = d.name;
    s.type = d.func_type;
    s.kind = Symbol::FUNCTION;
    s.is_defined = (d.body != nullptr);
    symtab.add(s);

    if (!d.body) return; // forward declaration only

    // Enter function scope
    symtab.push_scope();
    current_return_type = d.func_type.is_function && d.func_type.return_type
        ? *d.func_type.return_type : d.func_type;

    // Add parameters to scope
    for (auto& p : d.params) {
        if (auto* pv = dynamic_cast<ParamVarDecl*>(p.get())) {
            Symbol ps;
            ps.name = pv->name;
            ps.type = pv->param_type;
            ps.kind = Symbol::VARIABLE;
            ps.is_parameter = true;
            if (!symtab.add(ps)) {
                error("duplicate parameter name '" + pv->name + "'", pv->loc);
            }
        }
    }

    // Visit body
    if (d.body) visit_stmt(*d.body);

    symtab.pop_scope();
    current_return_type = Type();
}

void SemanticAnalyzer::visit_var(VariableDecl& d) {
    Symbol s;
    s.name = d.name;
    s.type = d.var_type;
    s.kind = Symbol::VARIABLE;

    if (!symtab.add(s)) {
        error("redeclaration of '" + d.name + "'", d.loc);
    }

    if (d.init) {
        Type init_type = visit_expr(*d.init);
        if (!types_equal(init_type, d.var_type)) {
            // Check implicit conversion
            if (!is_arithmetic(init_type) || !is_arithmetic(d.var_type)) {
                if (!(init_type.is_pointer && d.var_type.is_pointer)) {
                    note("initializer type does not match variable type", d.init->loc);
                }
            }
        }
    }
}

void SemanticAnalyzer::visit_typedef(TypedefDecl& d) {
    Symbol s;
    s.name = d.name;
    s.type = d.typedef_type;
    s.kind = Symbol::TYPEDEF;
    symtab.add(s);
}

void SemanticAnalyzer::visit_struct(StructDecl& d) {
    Symbol s;
    s.name = d.name;
    s.kind = d.is_union ? Symbol::TAG_UNION : Symbol::TAG_STRUCT;
    symtab.add(s);

    for (auto& f : d.fields) {
        if (auto* fd = dynamic_cast<FieldDecl*>(f.get())) {
            // Check field type is complete
            if (fd->field_type.is_function) {
                error("function field '" + fd->name + "' in struct", fd->loc);
            }
        }
    }
}

void SemanticAnalyzer::visit_enum(EnumDecl& d) {
    Symbol s;
    s.name = d.name;
    s.kind = Symbol::TAG_ENUM;
    symtab.add(s);

    long long val = 0;
    for (auto& e : d.enumerators) {
        if (e.second) {
            auto etype = visit_expr(*e.second);
            if (auto* ce = dynamic_cast<ConstantExpr*>(e.second.get())) {
                val = ce->value;
            }
        }
        Symbol es;
        es.name = e.first;
        es.kind = Symbol::ENUM_CONST;
        es.enum_value = val;
        if (!symtab.add(es)) {
            error("duplicate enumerator '" + e.first + "'", d.loc);
        }
        val++;
    }
}

void SemanticAnalyzer::visit_static_assert(StaticAssertDecl& d) {
    if (d.condition) {
        visit_expr(*d.condition);
    }
}

// ==================== Statements ====================

void SemanticAnalyzer::visit_stmt(Stmt& s) {
    if (auto* cs = dynamic_cast<CompoundStmt*>(&s)) {
        visit_compound(*cs);
    } else if (auto* is = dynamic_cast<IfStmt*>(&s)) {
        visit_if(*is);
    } else if (auto* ws = dynamic_cast<WhileStmt*>(&s)) {
        visit_while(*ws);
    } else if (auto* ds = dynamic_cast<DoStmt*>(&s)) {
        visit_do(*ds);
    } else if (auto* fs = dynamic_cast<ForStmt*>(&s)) {
        visit_for(*fs);
    } else if (auto* ss = dynamic_cast<SwitchStmt*>(&s)) {
        visit_switch(*ss);
    } else if (auto* cs = dynamic_cast<CaseStmt*>(&s)) {
        visit_case(*cs);
    } else if (auto* ds = dynamic_cast<DefaultStmt*>(&s)) {
        visit_default(*ds);
    } else if (auto* bs = dynamic_cast<BreakStmt*>(&s)) {
        visit_break(*bs);
    } else if (auto* cs = dynamic_cast<ContinueStmt*>(&s)) {
        visit_continue(*cs);
    } else if (auto* rs = dynamic_cast<ReturnStmt*>(&s)) {
        visit_return(*rs);
    } else if (auto* gs = dynamic_cast<GotoStmt*>(&s)) {
        visit_goto(*gs);
    } else if (auto* ls = dynamic_cast<LabelStmt*>(&s)) {
        visit_label(*ls);
    } else if (auto* es = dynamic_cast<ExprStmt*>(&s)) {
        visit_expr_stmt(*es);
    } else if (auto* ds = dynamic_cast<DeclStmt*>(&s)) {
        if (ds->decl) visit_decl(*ds->decl);
    } else if (dynamic_cast<NullStmt*>(&s)) {
        // nothing
    }
}

void SemanticAnalyzer::visit_compound(CompoundStmt& s) {
    symtab.push_scope();
    for (auto& st : s.stmts) {
        if (st) visit_stmt(*st);
    }
    symtab.pop_scope();
}

void SemanticAnalyzer::visit_if(IfStmt& s) {
    if (s.cond) {
        Type t = visit_expr(*s.cond);
        if (!is_scalar(t)) {
            error("if condition must be scalar type", s.cond->loc);
        }
    }
    if (s.then_body) visit_stmt(*s.then_body);
    if (s.else_body) visit_stmt(*s.else_body);
}

void SemanticAnalyzer::visit_while(WhileStmt& s) {
    if (s.cond) {
        Type t = visit_expr(*s.cond);
        if (!is_scalar(t)) {
            error("while condition must be scalar type", s.cond->loc);
        }
    }
    loop_depth++;
    if (s.body) visit_stmt(*s.body);
    loop_depth--;
}

void SemanticAnalyzer::visit_do(DoStmt& s) {
    loop_depth++;
    if (s.body) visit_stmt(*s.body);
    loop_depth--;
    if (s.cond) {
        Type t = visit_expr(*s.cond);
        if (!is_scalar(t)) {
            error("do-while condition must be scalar type", s.cond->loc);
        }
    }
}

void SemanticAnalyzer::visit_for(ForStmt& s) {
    symtab.push_scope();
    if (s.init) visit_stmt(*s.init);
    if (s.cond) {
        Type t = visit_expr(*s.cond);
        if (!is_scalar(t)) {
            error("for condition must be scalar type", s.cond->loc);
        }
    }
    if (s.inc) visit_expr(*s.inc);
    loop_depth++;
    if (s.body) visit_stmt(*s.body);
    loop_depth--;
    symtab.pop_scope();
}

void SemanticAnalyzer::visit_switch(SwitchStmt& s) {
    if (s.cond) {
        Type t = visit_expr(*s.cond);
        if (!is_integer(t)) {
            error("switch condition must be integer type", s.cond->loc);
        }
    }
    switch_depth++;
    if (s.body) visit_stmt(*s.body);
    switch_depth--;
}

void SemanticAnalyzer::visit_case(CaseStmt& s) {
    if (switch_depth == 0) {
        error("case label outside switch", s.loc);
    }
    if (s.value) visit_expr(*s.value);
    if (s.body) visit_stmt(*s.body);
}

void SemanticAnalyzer::visit_default(DefaultStmt& s) {
    if (switch_depth == 0) {
        error("default label outside switch", s.loc);
    }
    if (s.body) visit_stmt(*s.body);
}

void SemanticAnalyzer::visit_break(BreakStmt& s) {
    if (loop_depth == 0 && switch_depth == 0) {
        error("break outside loop or switch", s.loc);
    }
}

void SemanticAnalyzer::visit_continue(ContinueStmt& s) {
    if (loop_depth == 0) {
        error("continue outside loop", s.loc);
    }
}

void SemanticAnalyzer::visit_return(ReturnStmt& s) {
    if (s.value) {
        Type t = visit_expr(*s.value);
        if (current_return_type.prim != PrimitiveKind::VOID) {
            if (!types_equal(t, current_return_type)) {
                if (!is_arithmetic(t) || !is_arithmetic(current_return_type)) {
                    error("incompatible return type", s.value->loc);
                }
            }
        }
    } else {
        if (current_return_type.prim != PrimitiveKind::VOID) {
            error("non-void function should return a value", s.loc);
        }
    }
}

void SemanticAnalyzer::visit_goto(GotoStmt& s) {
    // Labels are resolved at compile time; just accept for now
}

void SemanticAnalyzer::visit_label(LabelStmt& s) {
    if (s.stmt) visit_stmt(*s.stmt);
}

void SemanticAnalyzer::visit_expr_stmt(ExprStmt& s) {
    if (s.expr) visit_expr(*s.expr);
}

// ==================== Expressions ====================

Type SemanticAnalyzer::visit_expr(Expr& e) {
    Type result;

    if (auto* be = dynamic_cast<BinaryExpr*>(&e)) {
        result = visit_binary(*be);
    } else if (auto* ue = dynamic_cast<UnaryExpr*>(&e)) {
        result = visit_unary(*ue);
    } else if (auto* ce = dynamic_cast<CallExpr*>(&e)) {
        result = visit_call(*ce);
    } else if (auto* me = dynamic_cast<MemberExpr*>(&e)) {
        result = visit_member(*me);
    } else if (auto* se = dynamic_cast<SubscriptExpr*>(&e)) {
        result = visit_subscript(*se);
    } else if (auto* ce = dynamic_cast<CastExpr*>(&e)) {
        result = visit_cast(*ce);
    } else if (auto* ce = dynamic_cast<ConditionalExpr*>(&e)) {
        result = visit_conditional(*ce);
    } else if (auto* ae = dynamic_cast<AssignExpr*>(&e)) {
        result = visit_assign(*ae);
    } else if (auto* ce = dynamic_cast<CommaExpr*>(&e)) {
        result = visit_comma(*ce);
    } else if (auto* ce = dynamic_cast<ConstantExpr*>(&e)) {
        result = visit_constant(*ce);
    } else if (auto* se = dynamic_cast<StringExpr*>(&e)) {
        result = visit_string(*se);
    } else if (auto* ie = dynamic_cast<IdentifierExpr*>(&e)) {
        result = visit_identifier(*ie);
    } else if (auto* se = dynamic_cast<SizeofExpr*>(&e)) {
        result = visit_sizeof(*se);
    } else if (auto* ae = dynamic_cast<AlignofExpr*>(&e)) {
        result = visit_alignof(*ae);
    } else if (auto* ge = dynamic_cast<GenericExpr*>(&e)) {
        result = visit_generic(*ge);
    } else if (dynamic_cast<NullptrExpr*>(&e)) {
        result = visit_nullptr(static_cast<NullptrExpr&>(e));
    } else if (auto* cle = dynamic_cast<CompoundLiteralExpr*>(&e)) {
        result = visit_compound_literal(*cle);
    } else if (auto* ile = dynamic_cast<InitListExpr*>(&e)) {
        result = visit_init_list(*ile);
    }

    e.result_type = result;
    return result;
}

Type SemanticAnalyzer::visit_binary(BinaryExpr& e) {
    Type lt = visit_expr(*e.lhs);
    Type rt = visit_expr(*e.rhs);

    switch (e.op) {
        case BinaryOp::ADD:
        case BinaryOp::SUB:
        case BinaryOp::MUL:
        case BinaryOp::DIV:
        case BinaryOp::MOD:
            if (!is_arithmetic(lt) || !is_arithmetic(rt)) {
                error("arithmetic operands required", e.loc);
            }
            return unify(lt, rt);

        case BinaryOp::EQ:
        case BinaryOp::NE:
        case BinaryOp::LT:
        case BinaryOp::GT:
        case BinaryOp::LE:
        case BinaryOp::GE:
            if (!is_arithmetic(lt) || !is_arithmetic(rt)) {
                error("arithmetic operands required for comparison", e.loc);
            }
            return Type{}; // int

        case BinaryOp::AND:
        case BinaryOp::OR:
            if (!is_scalar(lt) || !is_scalar(rt)) {
                error("scalar operands required for logical op", e.loc);
            }
            return Type{}; // int

        case BinaryOp::BIT_AND:
        case BinaryOp::BIT_OR:
        case BinaryOp::BIT_XOR:
        case BinaryOp::LSHIFT:
        case BinaryOp::RSHIFT:
            if (!is_integer(lt) || !is_integer(rt)) {
                error("integer operands required for bitwise op", e.loc);
            }
            return unify(lt, rt);
    }
    return Type{};
}

Type SemanticAnalyzer::visit_unary(UnaryExpr& e) {
    Type opt = visit_expr(*e.operand);

    switch (e.op) {
        case UnaryOp::PLUS:
        case UnaryOp::MINUS:
            if (!is_arithmetic(opt)) {
                error("arithmetic operand required", e.operand->loc);
            }
            return opt;

        case UnaryOp::NOT:
            if (!is_scalar(opt)) {
                error("scalar operand required for !", e.operand->loc);
            }
            return Type{}; // int

        case UnaryOp::BIT_NOT:
            if (!is_integer(opt)) {
                error("integer operand required for ~", e.operand->loc);
            }
            return opt;

        case UnaryOp::DEREF:
            if (!opt.is_pointer) {
                error("dereference of non-pointer type", e.operand->loc);
            }
            return *opt.pointee;

        case UnaryOp::ADDR_OF: {
            Type r = opt;
            Type pt;
            pt.is_pointer = true;
            pt.pointee = std::make_unique<Type>(std::move(r));
            return pt;
        }

        case UnaryOp::PRE_INC:
        case UnaryOp::PRE_DEC:
        case UnaryOp::POST_INC:
        case UnaryOp::POST_DEC:
            if (!is_scalar(opt)) {
                error("scalar operand required for ++/--", e.operand->loc);
            }
            return opt;
    }
    return Type{};
}

Type SemanticAnalyzer::visit_call(CallExpr& e) {
    Type callee_type = visit_expr(*e.callee);

    if (!callee_type.is_function && callee_type.is_pointer && callee_type.pointee->is_function) {
        callee_type = *callee_type.pointee;
    }

    if (!callee_type.is_function) {
        error("called object is not a function", e.callee->loc);
        return Type{};
    }

    if (callee_type.is_function && callee_type.return_type) {
        size_t expected = callee_type.params.size();
        size_t actual = e.args.size();
        if (!callee_type.is_variadic && actual != expected) {
            error("function call has " + std::to_string(actual) + " args but expects " +
                  std::to_string(expected), e.loc);
        }
        if (callee_type.is_variadic && actual < expected) {
            error("too few arguments to variadic function", e.loc);
        }

        // Check parameter types
        for (size_t i = 0; i < std::min(expected, actual); i++) {
            Type at = visit_expr(*e.args[i]);
            if (!types_equal(at, callee_type.params[i].first)) {
                if (!is_arithmetic(at) || !is_arithmetic(callee_type.params[i].first)) {
                    note("argument " + std::to_string(i+1) + " type mismatch", e.args[i]->loc);
                }
            }
        }

        return *callee_type.return_type;
    }

    return Type{};
}

Type SemanticAnalyzer::visit_member(MemberExpr& e) {
    Type obj_type = visit_expr(*e.object);

    if (e.is_arrow) {
        if (!obj_type.is_pointer) {
            error("-> requires pointer type", e.object->loc);
            return Type{};
        }
        obj_type = *obj_type.pointee;
    }

    if (!obj_type.is_struct && !obj_type.is_union) {
        error("member access on non-struct/union type", e.object->loc);
        return Type{};
    }

    for (const auto& m : obj_type.members) {
        if (m.second == e.member) {
            return m.first;
        }
    }

    error("no member '" + e.member + "' in struct/union", e.loc);
    return Type{};
}

Type SemanticAnalyzer::visit_subscript(SubscriptExpr& e) {
    Type bt = visit_expr(*e.base);
    Type it = visit_expr(*e.index);

    if (!bt.is_pointer && !bt.is_array) {
        error("subscripted value is not an array or pointer", e.base->loc);
        return Type{};
    }

    if (!is_integer(it)) {
        error("array subscript must be integer", e.index->loc);
    }

    if (bt.is_pointer && bt.pointee) return *bt.pointee;
    if (bt.is_array && bt.element_type) return *bt.element_type;
    return Type{};
}

Type SemanticAnalyzer::visit_cast(CastExpr& e) {
    visit_expr(*e.operand);
    // Cast is always valid syntactically
    return e.cast_type;
}

Type SemanticAnalyzer::visit_conditional(ConditionalExpr& e) {
    if (e.cond) {
        Type ct = visit_expr(*e.cond);
        if (!is_scalar(ct)) {
            error("conditional must have scalar condition", e.cond->loc);
        }
    }
    Type tt = visit_expr(*e.then_expr);
    Type et = visit_expr(*e.else_expr);

    if (is_arithmetic(tt) && is_arithmetic(et)) {
        return unify(tt, et);
    }
    return tt;
}

Type SemanticAnalyzer::visit_assign(AssignExpr& e) {
    Type lt = visit_expr(*e.lhs);
    Type rt = visit_expr(*e.rhs);

    if (e.op != AssignOp::ASSIGN) {
        // Compound assignment: +=, -=, etc.
        if (!is_arithmetic(lt) || !is_arithmetic(rt)) {
            error("arithmetic operands required for compound assignment", e.loc);
        }
    } else {
        if (!types_equal(lt, rt)) {
            if (!(is_arithmetic(lt) && is_arithmetic(rt)) &&
                !(lt.is_pointer && rt.is_pointer)) {
                note("assignment type mismatch", e.loc);
            }
        }
    }
    return lt;
}

Type SemanticAnalyzer::visit_comma(CommaExpr& e) {
    visit_expr(*e.lhs);
    return visit_expr(*e.rhs);
}

Type SemanticAnalyzer::visit_constant(ConstantExpr& e) {
    Type t;
    // Determine type from the literal
    std::string val = e.raw_value;
    if (!val.empty()) {
        if (val.size() > 1 && val[0] == '0' && (val[1] == 'x' || val[1] == 'X')) {
            t.prim = PrimitiveKind::U_INT;
        } else if (val.find('.') != std::string::npos || val.find('e') != std::string::npos ||
                   val.find('E') != std::string::npos || val.find('p') != std::string::npos ||
                   val.find('P') != std::string::npos) {
            t.prim = PrimitiveKind::DOUBLE;
        } else {
            t.prim = PrimitiveKind::INT;
        }
    }
    return t;
}

Type SemanticAnalyzer::visit_string(StringExpr& e) {
    Type t;
    t.is_pointer = true;
    Type ct;
    ct.prim = PrimitiveKind::CHAR;
    ct.is_const = true;
    t.pointee = std::make_unique<Type>(std::move(ct));
    return t;
}

Type SemanticAnalyzer::visit_identifier(IdentifierExpr& e) {
    Symbol* s = symtab.lookup(e.name);
    if (!s) {
        error("undeclared identifier '" + e.name + "'", e.loc);
        return Type{};
    }

    switch (s->kind) {
        case Symbol::VARIABLE:
            return s->type;
        case Symbol::FUNCTION: {
            Type ft = s->type;
            // Function-to-pointer decay
            Type pt;
            pt.is_pointer = true;
            pt.pointee = std::make_unique<Type>(std::move(ft));
            return pt;
        }
        case Symbol::TYPEDEF:
            return s->type;
        case Symbol::ENUM_CONST: {
            Type t;
            t.prim = PrimitiveKind::INT;
            return t;
        }
        default:
            return Type{};
    }
}

Type SemanticAnalyzer::visit_sizeof(SizeofExpr& e) {
    if (e.is_type) {
        // valid
    } else if (e.operand) {
        visit_expr(*e.operand);
    }
    Type t;
    t.prim = PrimitiveKind::U_LONG;
    return t;
}

Type SemanticAnalyzer::visit_alignof(AlignofExpr& e) {
    Type t;
    t.prim = PrimitiveKind::U_LONG;
    return t;
}

Type SemanticAnalyzer::visit_generic(GenericExpr& e) {
    if (e.control) visit_expr(*e.control);
    Type result;
    for (auto& a : e.associations) {
        if (a.is_default) {
            if (a.expr) result = visit_expr(*a.expr);
        } else {
            if (a.expr) result = visit_expr(*a.expr);
        }
    }
    return result;
}

Type SemanticAnalyzer::visit_nullptr(NullptrExpr& e) {
    Type t;
    t.is_pointer = true;
    t.pointee = std::make_unique<Type>();
    t.pointee->prim = PrimitiveKind::VOID;
    return t;
}

Type SemanticAnalyzer::visit_compound_literal(CompoundLiteralExpr& e) {
    if (e.init) visit_expr(*e.init);
    return e.literal_type;
}

Type SemanticAnalyzer::visit_init_list(InitListExpr& e) {
    // Determine type from context
    Type t;
    t.is_array = true;
    t.element_type = std::make_unique<Type>();
    t.element_type->prim = PrimitiveKind::INT;
    return t;
}
