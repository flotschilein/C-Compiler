#include "ir_builder.h"
#include <cassert>
#include <stdexcept>
#include <sstream>
#include <algorithm>

// --- Utilities ---

IRFunction* IRBuilder::current_fn() {
    if (current_fn_idx >= module.functions.size()) return nullptr;
    return &module.functions[current_fn_idx];
}

void IRBuilder::set_current_fn(IRFunction* fn) {
    if (!fn) {
        current_fn_idx = (size_t)-1;
        return;
    }
    for (size_t i = 0; i < module.functions.size(); i++) {
        if (&module.functions[i] == fn) {
            current_fn_idx = i;
            return;
        }
    }
    current_fn_idx = (size_t)-1;
}

std::string IRBuilder::new_label() {
    return "L" + std::to_string(next_label_id++);
}

size_t IRBuilder::new_value_id() {
    return current_fn()->next_value_id++;
}

size_t IRBuilder::emit(Instruction::Opcode op, IRType result_type,
                       std::vector<size_t> operands) {
    size_t id = new_value_id();
    Instruction inst;
    inst.opcode = op;
    inst.result_type = std::move(result_type);
    inst.operands = std::move(operands);
    current_block->instructions.push_back(std::move(inst));
    return id;
}

size_t IRBuilder::push_inst(Instruction inst) {
    size_t id = new_value_id();
    current_block->instructions.push_back(std::move(inst));
    return id;
}

void IRBuilder::set_block(IRBlock* block) {
    current_block = block;
}

IRBlock* IRBuilder::add_block(std::string label) {
    if (label.empty()) label = new_label();
    current_fn()->blocks.push_back({std::move(label)});
    return &current_fn()->blocks.back();
}

size_t IRBuilder::find_var(const std::string& name) {
    for (auto it = var_stack.rbegin(); it != var_stack.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return f->second;
    }
    return (size_t)-1;
}

// --- Type Lowering ---

IRPrimitive IRBuilder::lower_prim(PrimitiveKind k) {
    switch (k) {
        case PrimitiveKind::VOID:          return IRPrimitive::VOID;
        case PrimitiveKind::BOOL:          return IRPrimitive::I1;
        case PrimitiveKind::CHAR:
        case PrimitiveKind::S_CHAR:        return IRPrimitive::I8;
        case PrimitiveKind::U_CHAR:        return IRPrimitive::U8;
        case PrimitiveKind::SHORT:         return IRPrimitive::I16;
        case PrimitiveKind::U_SHORT:       return IRPrimitive::U16;
        case PrimitiveKind::INT:           return IRPrimitive::I32;
        case PrimitiveKind::U_INT:         return IRPrimitive::U32;
        case PrimitiveKind::LONG:
        case PrimitiveKind::LONGLONG:      return IRPrimitive::I64;
        case PrimitiveKind::U_LONG:
        case PrimitiveKind::U_LONGLONG:    return IRPrimitive::U64;
        case PrimitiveKind::FLOAT:         return IRPrimitive::F32;
        case PrimitiveKind::DOUBLE:
        case PrimitiveKind::LONGDOUBLE:    return IRPrimitive::F64;
        case PrimitiveKind::COMPLEX_FLOAT:
        case PrimitiveKind::COMPLEX_DOUBLE:
        case PrimitiveKind::COMPLEX_LONGDOUBLE:
            return IRPrimitive::F64; // Represent as a single f64 for now (component type widened)
        case PrimitiveKind::TYPEOF_DECLTYPE:
        default:                           return IRPrimitive::I32;
    }
}

IRType IRBuilder::lower_type(const Type& ast_type) {
    if (ast_type.is_pointer) {
        return IRTypeFactory::ptr(lower_type(*ast_type.pointee));
    }
    if (ast_type.is_array) {
        size_t sz = 0;
        if (auto* cst = std::get_if<long long>(&ast_type.array_size.size))
            sz = static_cast<size_t>(*cst);
        return IRTypeFactory::arr(lower_type(*ast_type.element_type), sz);
    }
    if (ast_type.is_function) {
        std::vector<IRType> param_types;
        for (auto& [pt, _] : ast_type.params) {
            param_types.push_back(lower_type(pt));
        }
        return IRTypeFactory::fn(lower_type(*ast_type.return_type), param_types, ast_type.is_variadic);
    }
    if (ast_type.is_template_type) {
        // Template type: look up or create struct with args
        std::vector<IRType> args;
        for (auto& ta : ast_type.template_args)
            args.push_back(lower_type(ta));
        return IRTypeFactory::strukt(ast_type.template_name, args);
    }
    if (ast_type.is_struct || ast_type.is_union) {
        // Ensure the struct definition exists in the module
        if (ast_type.has_members && !ast_type.tag_name.empty()) {
            IRStructDef* sdef = module.find_struct(ast_type.tag_name);
            if (!sdef) {
                IRStructDef new_def;
                new_def.name = ast_type.tag_name;
                new_def.is_defined = true;
                for (auto& m : ast_type.members) {
                    IRType ft = lower_type(std::get<0>(m));
                    new_def.fields.push_back({std::move(ft), std::get<1>(m)});
                }
                module.structs.push_back(std::move(new_def));
            }
        }
        return IRTypeFactory::strukt(ast_type.tag_name);
    }
    if (ast_type.is_enum) {
        return IRPrimitive::I32; // enums are int-sized
    }
    if (ast_type.is_typedef) {
        // Check if this is a template parameter name
        for (auto& gp : generic_params) {
            if (gp.name == ast_type.typedef_name) {
                return IRType::Param{gp.index, gp.name};
            }
        }
        return lower_prim(ast_type.prim);
    }
    if (ast_type.prim == PrimitiveKind::TYPEOF_DECLTYPE && ast_type.typeof_expr) {
        return lower_type(*ast_type.typeof_expr);
    }
    return lower_prim(ast_type.prim);
}

// --- Type Size Helpers ---

static long long align_up(long long offset, long long alignment) {
    if (alignment <= 1) return offset;
    return (offset + alignment - 1) & ~(alignment - 1);
}

static long long compute_type_align(const Type& type) {
    if (type.is_pointer) return 8;
    if (type.is_array) return compute_type_align(*type.element_type);
    if (type.is_struct && type.has_members) {
        long long max = 0;
        for (auto& m : type.members)
            max = std::max(max, compute_type_align(std::get<0>(m)));
        return max > 0 ? max : 1;
    }
    if (type.is_union && type.has_members) {
        long long max = 0;
        for (auto& m : type.members)
            max = std::max(max, compute_type_align(std::get<0>(m)));
        return max > 0 ? max : 1;
    }
    if (type.is_enum) return 4;
    if (type.is_function) return 8;
    switch (type.prim) {
        case PrimitiveKind::VOID:          return 1;
        case PrimitiveKind::BOOL:          return 1;
        case PrimitiveKind::CHAR:
        case PrimitiveKind::S_CHAR:
        case PrimitiveKind::U_CHAR:        return 1;
        case PrimitiveKind::SHORT:
        case PrimitiveKind::U_SHORT:       return 2;
        case PrimitiveKind::INT:
        case PrimitiveKind::U_INT:         return 4;
        case PrimitiveKind::LONG:
        case PrimitiveKind::U_LONG:
        case PrimitiveKind::LONGLONG:
        case PrimitiveKind::U_LONGLONG:    return 8;
        case PrimitiveKind::FLOAT:         return 4;
        case PrimitiveKind::DOUBLE:
        case PrimitiveKind::LONGDOUBLE:    return 8;
        case PrimitiveKind::COMPLEX_FLOAT:           return 4;
        case PrimitiveKind::COMPLEX_DOUBLE:          return 8;
        case PrimitiveKind::COMPLEX_LONGDOUBLE:      return 8;
        case PrimitiveKind::TYPEOF_DECLTYPE:
            if (type.typeof_expr) return compute_type_align(*type.typeof_expr);
            return 4;
        default:                           return 4;
    }
}

static long long compute_type_size(const Type& type) {
    if (type.is_pointer) return 8;
    if (type.is_array) {
        long long elem = compute_type_size(*type.element_type);
        if (auto* cst = std::get_if<long long>(&type.array_size.size))
            return elem * (*cst);
        return elem;
    }
    if (type.is_struct && type.has_members) {
        long long total = 0;
        long long max_align = 1;
        for (auto& m : type.members) {
            long long align = compute_type_align(std::get<0>(m));
            if (align > max_align) max_align = align;
            total = align_up(total, align);
            total += compute_type_size(std::get<0>(m));
        }
        return align_up(total, max_align);
    }
    if (type.is_union && type.has_members) {
        long long max_size = 0;
        long long max_align = 1;
        for (auto& m : type.members) {
            long long a = compute_type_align(std::get<0>(m));
            if (a > max_align) max_align = a;
            long long s = compute_type_size(std::get<0>(m));
            if (s > max_size) max_size = s;
        }
        return align_up(max_size, max_align);
    }
    if (type.is_enum) return 4;
    if (type.is_function) return 8;
    switch (type.prim) {
        case PrimitiveKind::VOID:          return 1;
        case PrimitiveKind::BOOL:          return 1;
        case PrimitiveKind::CHAR:
        case PrimitiveKind::S_CHAR:
        case PrimitiveKind::U_CHAR:        return 1;
        case PrimitiveKind::SHORT:
        case PrimitiveKind::U_SHORT:       return 2;
        case PrimitiveKind::INT:
        case PrimitiveKind::U_INT:         return 4;
        case PrimitiveKind::LONG:
        case PrimitiveKind::U_LONG:
        case PrimitiveKind::LONGLONG:
        case PrimitiveKind::U_LONGLONG:    return 8;
        case PrimitiveKind::FLOAT:         return 4;
        case PrimitiveKind::DOUBLE:
        case PrimitiveKind::LONGDOUBLE:    return 8;
        case PrimitiveKind::COMPLEX_FLOAT:           return 8;
        case PrimitiveKind::COMPLEX_DOUBLE:          return 16;
        case PrimitiveKind::COMPLEX_LONGDOUBLE:      return 16;
        case PrimitiveKind::TYPEOF_DECLTYPE:
            if (type.typeof_expr) return compute_type_size(*type.typeof_expr);
            return 4;
        default:                           return 4;
    }
}

static long long compute_field_offset(const Type& struct_type, const std::string& member) {
    long long offset = 0;
    for (auto& m : struct_type.members) {
        long long align = compute_type_align(std::get<0>(m));
        offset = align_up(offset, align);
        if (std::get<1>(m) == member) return offset;
        offset += compute_type_size(std::get<0>(m));
    }
    return -1;
}

// --- Expression Lowering (rvalue) ---

size_t IRBuilder::lower_expr(const Expr& expr) {
    if (auto* e = dynamic_cast<const ConstantExpr*>(&expr))  return lower_constant(*e);
    if (auto* e = dynamic_cast<const StringExpr*>(&expr))     return lower_string(*e);
    if (auto* e = dynamic_cast<const IdentifierExpr*>(&expr)) return lower_identifier_val(*e);
    if (auto* e = dynamic_cast<const BinaryExpr*>(&expr))     return lower_binary(*e);
    if (auto* e = dynamic_cast<const UnaryExpr*>(&expr))      return lower_unary(*e);
    if (auto* e = dynamic_cast<const CallExpr*>(&expr))       return lower_call(*e);
    if (auto* e = dynamic_cast<const CastExpr*>(&expr))       return lower_cast(*e);
    if (auto* e = dynamic_cast<const AssignExpr*>(&expr))     return lower_assign(*e);
    if (auto* e = dynamic_cast<const ConditionalExpr*>(&expr)) return lower_conditional(*e);
    if (auto* e = dynamic_cast<const MemberExpr*>(&expr))     return lower_member(*e);
    if (auto* e = dynamic_cast<const SubscriptExpr*>(&expr))  return lower_subscript(*e);
    if (auto* e = dynamic_cast<const CommaExpr*>(&expr))      return lower_comma(*e);
    if (auto* e = dynamic_cast<const SizeofExpr*>(&expr))     return lower_sizeof(*e);
    if (auto* e = dynamic_cast<const AlignofExpr*>(&expr))    return lower_alignof(*e);
    if (auto* e = dynamic_cast<const GenericExpr*>(&expr)) {
        if (e->selected_index >= 0 && (size_t)e->selected_index < e->associations.size()) {
            if (e->associations[e->selected_index].expr)
                return lower_expr(*e->associations[e->selected_index].expr);
        }
        return emit(Instruction::CONST, IRPrimitive::I32, {});
    }
    if (auto* e = dynamic_cast<const TemplateIdExpr*>(&expr)) return lower_template_id(*e);
    if (dynamic_cast<const NullptrExpr*>(&expr)) {
        return emit(Instruction::CONST, IRTypeFactory::ptr(IRPrimitive::VOID), {});
    }
    if (auto* e = dynamic_cast<const CompoundLiteralExpr*>(&expr)) {
        IRType t = lower_type(e->literal_type);
        size_t addr = emit(Instruction::ALLOCA, IRTypeFactory::ptr(t), {});
        current_block->instructions.back().extra_type = t.copy();
        if (e->init) {
            size_t val = lower_expr(*e->init);
            emit(Instruction::STORE, IRPrimitive::VOID, {val, addr});
        }
        return addr;
    }
    if (auto* e = dynamic_cast<const InitListExpr*>(&expr)) {
        if (e->inits.empty()) return emit(Instruction::CONST, IRPrimitive::I32, {});
        return lower_expr(*e->inits[0]);
    }

    throw std::runtime_error("unhandled expression type in IR builder");
}

// --- Expression Lowering (lvalue / address) ---

size_t IRBuilder::lower_expr_addr(const Expr& expr) {
    if (auto* e = dynamic_cast<const IdentifierExpr*>(&expr)) return lower_identifier_addr(*e);
    if (auto* e = dynamic_cast<const MemberExpr*>(&expr)) {
        return lower_member_addr(*e);
    }
    if (auto* e = dynamic_cast<const SubscriptExpr*>(&expr)) {
        size_t base = lower_expr(*e->base);
        size_t idx = lower_expr(*e->index);
        size_t gep = emit(Instruction::GEP, IRTypeFactory::ptr(IRPrimitive::VOID), {base, idx});
        current_block->instructions.back().extra_type = IRPrimitive::VOID;
        current_block->instructions.back().gep_index = 0;
        return gep;
    }
    if (auto* e = dynamic_cast<const UnaryExpr*>(&expr)) {
        if (e->op == UnaryOp::DEREF) {
            // *p as lvalue means p is already a pointer
            return lower_expr(*e->operand);
        }
    }
    return (size_t)-1; // not an lvalue
}

// --- Specific Expression Lowering ---

size_t IRBuilder::lower_constant(const ConstantExpr& expr) {
    IRType type = lower_type(expr.result_type);
    size_t id = emit(Instruction::CONST, type, {});
    if (type.is_prim()) {
        auto p = type.as_prim();
        if (p == IRPrimitive::F32 || p == IRPrimitive::F64) {
            current_block->instructions.back().const_val.float_val = std::stod(expr.raw_value);
        } else {
            current_block->instructions.back().const_val.int_val = expr.value;
        }
    } else {
        current_block->instructions.back().const_val.int_val = expr.value;
    }
    return id;
}

size_t IRBuilder::lower_string(const StringExpr& expr) {
    module.global_strings.push_back(expr.value);
    // Return a pointer to the string (conceptual: a pointer to global data)
    size_t id = emit(Instruction::CONST, IRTypeFactory::ptr(IRPrimitive::I8), {});
    current_block->instructions.back().const_val.int_val = (long long)(module.global_strings.size() - 1);
    return id;
}

size_t IRBuilder::lower_identifier_val(const IdentifierExpr& expr) {
    size_t addr = find_var(expr.name);
    if (addr == (size_t)-1) {
        // Check if it's a global variable
        IRGlobal* g = module.find_global(expr.name);
        if (g) {
            return emit(Instruction::CONST, lower_type(expr.result_type), {});
        }
        // External/global variable — treat as extern placeholder
        return emit(Instruction::CONST, lower_type(expr.result_type), {});
    }
    IRType loaded_type = lower_type(expr.result_type);
    return emit(Instruction::LOAD, loaded_type, {addr});
}

size_t IRBuilder::lower_identifier_addr(const IdentifierExpr& expr) {
    size_t addr = find_var(expr.name);
    if (addr == (size_t)-1) {
        // Check global
        IRGlobal* g = module.find_global(expr.name);
        if (g) {
            size_t id = emit(Instruction::CONST, IRTypeFactory::ptr(g->type.copy()), {});
            current_block->instructions.back().const_val.int_val = 0;
            return id;
        }
        throw std::runtime_error("unknown variable: " + expr.name);
    }
    return addr;
}

size_t IRBuilder::lower_binary(const BinaryExpr& expr) {
    // Short-circuit logical ops: expand into branches with phi
    if (expr.op == BinaryOp::AND) {
        size_t lhs = lower_expr(*expr.lhs);
        std::string rhs_label = new_label();
        std::string end_label = new_label();
        std::string entry_label = current_block->label;

        size_t zero = emit(Instruction::CONST, IRPrimitive::I1, {});
        current_block->instructions.back().const_val.int_val = 0;

        {
            Instruction br;
            br.opcode = Instruction::BR_COND;
            br.result_type = IRPrimitive::VOID;
            br.operands = {lhs};
            br.true_label = rhs_label;
            br.false_label = end_label;
            push_inst(std::move(br));
        }

        set_block(add_block(rhs_label));
        size_t rhs = lower_expr(*expr.rhs);
        std::string rhs_block_label = current_block->label;
        size_t zero_r = emit(Instruction::CONST, lower_type(expr.rhs->result_type), {});
        current_block->instructions.back().const_val.int_val = 0;
        size_t cmp = emit(Instruction::NE, IRPrimitive::I1, {rhs, zero_r});
        {
            Instruction br;
            br.opcode = Instruction::BR;
            br.result_type = IRPrimitive::VOID;
            br.target_label = end_label;
            push_inst(std::move(br));
        }

        set_block(add_block(end_label));
        Instruction phi;
        phi.opcode = Instruction::PHI;
        phi.result_type = IRPrimitive::I1;
        phi.phi_incoming = {{zero, entry_label}, {cmp, rhs_block_label}};
        return push_inst(std::move(phi));
    }

    if (expr.op == BinaryOp::OR) {
        size_t lhs = lower_expr(*expr.lhs);
        std::string true_label = new_label();
        std::string rhs_label = new_label();
        std::string end_label = new_label();
        std::string entry_label = current_block->label;

        {
            Instruction br;
            br.opcode = Instruction::BR_COND;
            br.result_type = IRPrimitive::VOID;
            br.operands = {lhs};
            br.true_label = true_label;
            br.false_label = rhs_label;
            push_inst(std::move(br));
        }

        set_block(add_block(true_label));
        size_t one = emit(Instruction::CONST, IRPrimitive::I1, {});
        current_block->instructions.back().const_val.int_val = 1;
        std::string true_block_label = current_block->label;
        {
            Instruction br;
            br.opcode = Instruction::BR;
            br.result_type = IRPrimitive::VOID;
            br.target_label = end_label;
            push_inst(std::move(br));
        }

        set_block(add_block(rhs_label));
        size_t rhs_val = lower_expr(*expr.rhs);
        std::string rhs_block_label = current_block->label;
        size_t zero_r = emit(Instruction::CONST, lower_type(expr.rhs->result_type), {});
        current_block->instructions.back().const_val.int_val = 0;
        size_t cmp = emit(Instruction::NE, IRPrimitive::I1, {rhs_val, zero_r});
        {
            Instruction br;
            br.opcode = Instruction::BR;
            br.result_type = IRPrimitive::VOID;
            br.target_label = end_label;
            push_inst(std::move(br));
        }

        set_block(add_block(end_label));
        Instruction phi;
        phi.opcode = Instruction::PHI;
        phi.result_type = IRPrimitive::I1;
        phi.phi_incoming = {{one, true_block_label}, {cmp, rhs_block_label}};
        return push_inst(std::move(phi));
    }

    Instruction::Opcode opcode;
    switch (expr.op) {
        case BinaryOp::ADD:      opcode = Instruction::ADD; break;
        case BinaryOp::SUB:      opcode = Instruction::SUB; break;
        case BinaryOp::MUL:      opcode = Instruction::MUL; break;
        case BinaryOp::DIV:      opcode = Instruction::DIV; break;
        case BinaryOp::MOD:      opcode = Instruction::REM; break;
        case BinaryOp::EQ:       opcode = Instruction::EQ; break;
        case BinaryOp::NE:       opcode = Instruction::NE; break;
        case BinaryOp::LT:       opcode = Instruction::LT; break;
        case BinaryOp::GT:       opcode = Instruction::GT; break;
        case BinaryOp::LE:       opcode = Instruction::LE; break;
        case BinaryOp::GE:       opcode = Instruction::GE; break;
        case BinaryOp::BIT_AND:  opcode = Instruction::BIT_AND; break;
        case BinaryOp::BIT_OR:   opcode = Instruction::BIT_OR; break;
        case BinaryOp::BIT_XOR:  opcode = Instruction::BIT_XOR; break;
        case BinaryOp::LSHIFT:   opcode = Instruction::SHL; break;
        case BinaryOp::RSHIFT:   opcode = Instruction::SHR; break;
        default: break;
    }

    size_t lhs = lower_expr(*expr.lhs);
    size_t rhs = lower_expr(*expr.rhs);

    IRType result_type;
    switch (expr.op) {
        case BinaryOp::EQ: case BinaryOp::NE:
        case BinaryOp::LT: case BinaryOp::GT:
        case BinaryOp::LE: case BinaryOp::GE:
            result_type = IRPrimitive::I1;
            break;
        default:
            result_type = lower_type(expr.result_type);
            break;
    }

    return emit(opcode, result_type, {lhs, rhs});
}

size_t IRBuilder::lower_unary(const UnaryExpr& expr) {
    switch (expr.op) {
        case UnaryOp::PLUS:
            return lower_expr(*expr.operand);

        case UnaryOp::MINUS: {
            size_t op = lower_expr(*expr.operand);
            return emit(Instruction::NEG, lower_type(expr.result_type), {op});
        }

        case UnaryOp::NOT: {
            size_t op = lower_expr(*expr.operand);
            return emit(Instruction::LOGIC_NOT, IRPrimitive::I1, {op});
        }

        case UnaryOp::BIT_NOT: {
            size_t op = lower_expr(*expr.operand);
            return emit(Instruction::BIT_NOT, lower_type(expr.result_type), {op});
        }

        case UnaryOp::DEREF: {
            // *expr -> load from pointer
            size_t ptr = lower_expr(*expr.operand);
            return emit(Instruction::LOAD, lower_type(expr.result_type), {ptr});
        }

        case UnaryOp::ADDR_OF: {
            // &expr -> get address
            return lower_expr_addr(*expr.operand);
        }

        case UnaryOp::PRE_INC:
        case UnaryOp::PRE_DEC:
        case UnaryOp::POST_INC:
        case UnaryOp::POST_DEC: {
            // ++/-- : load, add/sub 1, store, return
            size_t addr = lower_expr_addr(*expr.operand);
            size_t val = emit(Instruction::LOAD, lower_type(expr.result_type), {addr});
            Instruction::Opcode inc_op = (expr.op == UnaryOp::PRE_INC || expr.op == UnaryOp::POST_INC)
                                         ? Instruction::ADD : Instruction::SUB;
            size_t one = emit(Instruction::CONST, lower_type(expr.result_type), {});
            current_block->instructions.back().const_val.int_val = 1;
            size_t result = emit(inc_op, lower_type(expr.result_type), {val, one});
            emit(Instruction::STORE, IRPrimitive::VOID, {result, addr});
            if (expr.op == UnaryOp::PRE_INC || expr.op == UnaryOp::PRE_DEC)
                return result;
            else
                return val;
        }
    }
    return (size_t)-1;
}

size_t IRBuilder::lower_call(const CallExpr& expr) {
    std::vector<size_t> arg_ids;
    for (auto& arg : expr.args)
        arg_ids.push_back(lower_expr(*arg));

    std::string callee_name;
    if (auto* id = dynamic_cast<const IdentifierExpr*>(expr.callee.get()))
        callee_name = id->name;

    IRType return_type = lower_type(expr.result_type);
    size_t id = emit(Instruction::CALL, return_type, arg_ids);
    current_block->instructions.back().callee_name = callee_name;
    return id;
}

size_t IRBuilder::lower_cast(const CastExpr& expr) {
    size_t op = lower_expr(*expr.operand);
    IRType target = lower_type(expr.cast_type);
    IRType source = lower_type(expr.operand->result_type);

    // Determine the appropriate cast opcode
    Instruction::Opcode cast_op = Instruction::CAST;

    auto is_int_prim = [](IRPrimitive p) -> bool {
        return p == IRPrimitive::I8 || p == IRPrimitive::I16 || p == IRPrimitive::I32 || p == IRPrimitive::I64
            || p == IRPrimitive::U8 || p == IRPrimitive::U16 || p == IRPrimitive::U32 || p == IRPrimitive::U64
            || p == IRPrimitive::I1;
    };
    auto is_float_prim = [](IRPrimitive p) -> bool {
        return p == IRPrimitive::F32 || p == IRPrimitive::F64;
    };

    if (source.is_prim() && target.is_prim()) {
        IRPrimitive sp = source.as_prim();
        IRPrimitive tp = target.as_prim();

        if (sp == tp) {
            // Same type, no-op
            return op;
        }

        if (is_int_prim(sp) && is_int_prim(tp)) {
            // Integer-to-integer: trunc or extend based on size
            // For simplicity, use CAST for now
            cast_op = Instruction::CAST;
        } else if (is_float_prim(sp) && is_float_prim(tp)) {
            cast_op = Instruction::CAST;
        } else if (is_float_prim(sp) && is_int_prim(tp)) {
            cast_op = (tp == IRPrimitive::I8 || tp == IRPrimitive::I16 || tp == IRPrimitive::I32 || tp == IRPrimitive::I64)
                      ? Instruction::FPTOSI : Instruction::FPTOUI;
        } else if (is_int_prim(sp) && is_float_prim(tp)) {
            cast_op = (sp == IRPrimitive::I8 || sp == IRPrimitive::I16 || sp == IRPrimitive::I32 || sp == IRPrimitive::I64)
                      ? Instruction::SITOFP : Instruction::UITOFP;
        }
    } else if (source.is_ptr() && target.is_ptr()) {
        cast_op = Instruction::CAST;
    } else if (source.is_ptr() && target.is_prim() && target.as_prim() == IRPrimitive::I64) {
        cast_op = Instruction::CAST; // ptr to int
    } else if (source.is_prim() && source.as_prim() == IRPrimitive::I64 && target.is_ptr()) {
        cast_op = Instruction::CAST; // int to ptr
    }

    size_t id = emit(cast_op, target, {op});
    current_block->instructions.back().extra_type = target.copy();
    return id;
}

size_t IRBuilder::lower_assign(const AssignExpr& expr) {
    size_t rhs = lower_expr(*expr.rhs);
    size_t addr = lower_expr_addr(*expr.lhs);

    if (addr == (size_t)-1) {
        // Not an lvalue (e.g., assignment to dereference)
        // For *p = val, the RHS is already computed; the LHS expr gives the pointer
        if (auto* unary = dynamic_cast<const UnaryExpr*>(expr.lhs.get())) {
            if (unary->op == UnaryOp::DEREF) {
                addr = lower_expr(*unary->operand);
            }
        }
    }

    if (expr.op == AssignOp::ASSIGN) {
        emit(Instruction::STORE, IRPrimitive::VOID, {rhs, addr});
        return rhs;
    }

    // Compound assignment: a += b → tmp = a + b; a = tmp
    static const std::map<AssignOp, Instruction::Opcode> compound_ops = {
        {AssignOp::ADD, Instruction::ADD},
        {AssignOp::SUB, Instruction::SUB},
        {AssignOp::MUL, Instruction::MUL},
        {AssignOp::DIV, Instruction::DIV},
        {AssignOp::MOD, Instruction::REM},
        {AssignOp::AND, Instruction::BIT_AND},
        {AssignOp::OR, Instruction::BIT_OR},
        {AssignOp::XOR, Instruction::BIT_XOR},
        {AssignOp::LSHIFT, Instruction::SHL},
        {AssignOp::RSHIFT, Instruction::SHR},
    };

    size_t lhs_val = emit(Instruction::LOAD, lower_type(expr.lhs->result_type), {addr});
    auto it = compound_ops.find(expr.op);
    Instruction::Opcode compound_op = (it != compound_ops.end()) ? it->second : Instruction::ADD;
    size_t result = emit(compound_op, lower_type(expr.result_type), {lhs_val, rhs});
    emit(Instruction::STORE, IRPrimitive::VOID, {result, addr});
    return result;
}

size_t IRBuilder::lower_conditional(const ConditionalExpr& expr) {
    // cond ? then : else
    // Expand into branches with phi
    size_t cond_id = lower_expr(*expr.cond);
    std::string then_label = new_label();
    std::string else_label = new_label();
    std::string end_label = new_label();

    Instruction br_cond;
    br_cond.opcode = Instruction::BR_COND;
    br_cond.result_type = IRPrimitive::VOID;
    br_cond.operands = {cond_id};
    br_cond.true_label = then_label;
    br_cond.false_label = else_label;
    push_inst(std::move(br_cond));

    // then block
    set_block(add_block(then_label));
    size_t then_val = lower_expr(*expr.then_expr);
    Instruction br_then;
    br_then.opcode = Instruction::BR;
    br_then.result_type = IRPrimitive::VOID;
    br_then.target_label = end_label;
    push_inst(std::move(br_then));

    // else block
    set_block(add_block(else_label));
    size_t else_val = lower_expr(*expr.else_expr);
    Instruction br_else;
    br_else.opcode = Instruction::BR;
    br_else.result_type = IRPrimitive::VOID;
    br_else.target_label = end_label;
    push_inst(std::move(br_else));

    // end block with phi
    set_block(add_block(end_label));
    IRType result_type = lower_type(expr.result_type);
    size_t phi_id = new_value_id();
    Instruction phi;
    phi.opcode = Instruction::PHI;
    phi.result_type = result_type.copy();
    phi.phi_incoming = {{then_val, then_label}, {else_val, else_label}};
    push_inst(std::move(phi));
    return phi_id;
}

size_t IRBuilder::lower_member_addr(const MemberExpr& expr) {
    size_t base = expr.is_arrow
                  ? lower_expr(*expr.object)
                  : lower_expr_addr(*expr.object);

    Type obj_type = expr.object->result_type;
    if (expr.is_arrow && obj_type.is_pointer && obj_type.pointee)
        obj_type = *obj_type.pointee;

    long long offset = compute_field_offset(obj_type, expr.member);
    if (offset < 0) return (size_t)-1;

    size_t offset_val = emit(Instruction::CONST, IRPrimitive::I64, {});
    current_block->instructions.back().const_val.int_val = offset;
    size_t gep = emit(Instruction::GEP, IRTypeFactory::ptr(IRPrimitive::VOID), {base, offset_val});
    current_block->instructions.back().extra_type = IRPrimitive::I8;
    current_block->instructions.back().gep_index = 0;
    return gep;
}

size_t IRBuilder::lower_member(const MemberExpr& expr) {
    size_t addr = lower_member_addr(expr);
    return emit(Instruction::LOAD, lower_type(expr.result_type), {addr});
}

size_t IRBuilder::lower_subscript(const SubscriptExpr& expr) {
    size_t base = lower_expr(*expr.base);
    size_t idx = lower_expr(*expr.index);
    // GEP: ptr + index
    size_t gep = emit(Instruction::GEP, IRTypeFactory::ptr(lower_type(expr.result_type)), {base, idx});
    current_block->instructions.back().extra_type = lower_type(expr.result_type);
    current_block->instructions.back().gep_index = 0;
    // Load the result
    return emit(Instruction::LOAD, lower_type(expr.result_type), {gep});
}

size_t IRBuilder::lower_comma(const CommaExpr& expr) {
    lower_expr(*expr.lhs);
    return lower_expr(*expr.rhs);
}

size_t IRBuilder::lower_sizeof(const SizeofExpr& expr) {
    long long size = 4;
    if (!expr.is_type) {
        size = compute_type_size(expr.operand->result_type);
    } else {
        size = compute_type_size(expr.sizeof_type);
    }
    size_t id = emit(Instruction::CONST, IRPrimitive::I64, {});
    current_block->instructions.back().const_val.int_val = size;
    return id;
}

size_t IRBuilder::lower_alignof(const AlignofExpr& expr) {
    long long align = compute_type_align(expr.align_type);
    size_t id = emit(Instruction::CONST, IRPrimitive::I64, {});
    current_block->instructions.back().const_val.int_val = align;
    return id;
}

// --- Statement Lowering ---

void IRBuilder::lower_stmt(const Stmt& stmt) {
    if (auto* s = dynamic_cast<const CompoundStmt*>(&stmt))   return lower_compound(*s);
    if (auto* s = dynamic_cast<const ExprStmt*>(&stmt))       return lower_expr_stmt(*s);
    if (auto* s = dynamic_cast<const DeclStmt*>(&stmt))       return lower_decl_stmt(*s);
    if (auto* s = dynamic_cast<const IfStmt*>(&stmt))         return lower_if(*s);
    if (auto* s = dynamic_cast<const WhileStmt*>(&stmt))      return lower_while(*s);
    if (auto* s = dynamic_cast<const DoStmt*>(&stmt))         return lower_do(*s);
    if (auto* s = dynamic_cast<const ForStmt*>(&stmt))        return lower_for(*s);
    if (auto* s = dynamic_cast<const SwitchStmt*>(&stmt))     return lower_switch(*s);
    if (auto* s = dynamic_cast<const ReturnStmt*>(&stmt))     return lower_return(*s);
    if (auto* s = dynamic_cast<const BreakStmt*>(&stmt))      return lower_break(*s);
    if (auto* s = dynamic_cast<const ContinueStmt*>(&stmt))   return lower_continue(*s);
    if (auto* s = dynamic_cast<const GotoStmt*>(&stmt))       return lower_goto(*s);
    if (auto* s = dynamic_cast<const LabelStmt*>(&stmt))      return lower_label(*s);
    if (auto* s = dynamic_cast<const CaseStmt*>(&stmt)) {
        if (!switch_stack.empty()) {
            auto& info = switch_stack.back();
            if (info.next_case_idx < info.cases.size()) {
                auto& c = info.cases[info.next_case_idx++];
                // Terminate previous block if needed (fall-through)
                if (current_block && !current_block->instructions.empty()) {
                    auto& last = current_block->instructions.back();
                    if (last.opcode != Instruction::BR && last.opcode != Instruction::BR_COND && last.opcode != Instruction::RET) {
                        Instruction br;
                        br.opcode = Instruction::BR;
                        br.result_type = IRPrimitive::VOID;
                        br.target_label = c.label;
                        push_inst(std::move(br));
                    }
                }
                set_block(add_block(c.label));
            }
        }
        if (s->body) lower_stmt(*s->body);
        return;
    }
    if (auto* s = dynamic_cast<const DefaultStmt*>(&stmt)) {
        if (!switch_stack.empty()) {
            auto& info = switch_stack.back();
            if (!info.default_label.empty()) {
                if (current_block && !current_block->instructions.empty()) {
                    auto& last = current_block->instructions.back();
                    if (last.opcode != Instruction::BR && last.opcode != Instruction::BR_COND && last.opcode != Instruction::RET) {
                        Instruction br;
                        br.opcode = Instruction::BR;
                        br.result_type = IRPrimitive::VOID;
                        br.target_label = info.default_label;
                        push_inst(std::move(br));
                    }
                }
                set_block(add_block(info.default_label));
            }
        }
        if (s->body) lower_stmt(*s->body);
        return;
    }
    if (dynamic_cast<const NullStmt*>(&stmt))                 return;
}

void IRBuilder::lower_compound(const CompoundStmt& stmt) {
    var_stack.emplace_back();
    for (auto& s : stmt.stmts)
        lower_stmt(*s);
    var_stack.pop_back();
}

void IRBuilder::lower_if(const IfStmt& stmt) {
    size_t cond_id = lower_expr(*stmt.cond);
    std::string then_label = new_label();
    std::string else_label = stmt.else_body ? new_label() : "";
    std::string end_label = new_label();

    Instruction br_cond;
    br_cond.opcode = Instruction::BR_COND;
    br_cond.result_type = IRPrimitive::VOID;
    br_cond.operands = {cond_id};
    br_cond.true_label = then_label;
    br_cond.false_label = stmt.else_body ? else_label : end_label;
    push_inst(std::move(br_cond));

    // then block
    set_block(add_block(then_label));
    lower_stmt(*stmt.then_body);
    if (current_block->instructions.empty() ||
        current_block->instructions.back().opcode != Instruction::BR_COND &&
        current_block->instructions.back().opcode != Instruction::BR &&
        current_block->instructions.back().opcode != Instruction::RET) {
        Instruction br;
        br.opcode = Instruction::BR;
        br.result_type = IRPrimitive::VOID;
        br.target_label = end_label;
        push_inst(std::move(br));
    }

    // else block
    if (stmt.else_body) {
        set_block(add_block(else_label));
        lower_stmt(*stmt.else_body);
        if (current_block->instructions.empty() ||
            current_block->instructions.back().opcode != Instruction::BR &&
            current_block->instructions.back().opcode != Instruction::BR_COND &&
            current_block->instructions.back().opcode != Instruction::RET) {
            Instruction br;
            br.opcode = Instruction::BR;
            br.result_type = IRPrimitive::VOID;
            br.target_label = end_label;
            push_inst(std::move(br));
        }
    }

    // end block
    set_block(add_block(end_label));
}

void IRBuilder::lower_while(const WhileStmt& stmt) {
    std::string cond_label = new_label();
    std::string body_label = new_label();
    std::string end_label = new_label();

    loop_stack.push_back({end_label, cond_label});

    // Branch to condition check
    Instruction br_entry;
    br_entry.opcode = Instruction::BR;
    br_entry.result_type = IRPrimitive::VOID;
    br_entry.target_label = cond_label;
    push_inst(std::move(br_entry));

    // Condition block
    set_block(add_block(cond_label));
    size_t cond_id = lower_expr(*stmt.cond);
    Instruction br_cond;
    br_cond.opcode = Instruction::BR_COND;
    br_cond.result_type = IRPrimitive::VOID;
    br_cond.operands = {cond_id};
    br_cond.true_label = body_label;
    br_cond.false_label = end_label;
    push_inst(std::move(br_cond));

    // Body block
    set_block(add_block(body_label));
    lower_stmt(*stmt.body);
    // Branch back to condition
    Instruction br_back;
    br_back.opcode = Instruction::BR;
    br_back.result_type = IRPrimitive::VOID;
    br_back.target_label = cond_label;
    push_inst(std::move(br_back));

    // End block
    set_block(add_block(end_label));

    loop_stack.pop_back();
}

void IRBuilder::lower_do(const DoStmt& stmt) {
    std::string body_label = new_label();
    std::string cond_label = new_label();
    std::string end_label = new_label();

    loop_stack.push_back({end_label, cond_label});

    // Body (first iteration)
    set_block(add_block(body_label));
    lower_stmt(*stmt.body);
    // Branch to condition
    Instruction br_body_end;
    br_body_end.opcode = Instruction::BR;
    br_body_end.result_type = IRPrimitive::VOID;
    br_body_end.target_label = cond_label;
    push_inst(std::move(br_body_end));

    // Condition block
    set_block(add_block(cond_label));
    size_t cond_id = lower_expr(*stmt.cond);
    Instruction br_cond;
    br_cond.opcode = Instruction::BR_COND;
    br_cond.result_type = IRPrimitive::VOID;
    br_cond.operands = {cond_id};
    br_cond.true_label = body_label;
    br_cond.false_label = end_label;
    push_inst(std::move(br_cond));

    // End block
    set_block(add_block(end_label));

    loop_stack.pop_back();
}

void IRBuilder::lower_for(const ForStmt& stmt) {
    std::string cond_label = new_label();
    std::string body_label = new_label();
    std::string inc_label = new_label();
    std::string end_label = new_label();

    loop_stack.push_back({end_label, inc_label});

    var_stack.emplace_back();

    // Init
    if (stmt.init) lower_stmt(*stmt.init);

    // Branch to condition
    Instruction br_init;
    br_init.opcode = Instruction::BR;
    br_init.result_type = IRPrimitive::VOID;
    br_init.target_label = cond_label;
    push_inst(std::move(br_init));

    // Condition block
    set_block(add_block(cond_label));
    if (stmt.cond) {
        size_t cond_id = lower_expr(*stmt.cond);
        Instruction br_cond;
        br_cond.opcode = Instruction::BR_COND;
        br_cond.result_type = IRPrimitive::VOID;
        br_cond.operands = {cond_id};
        br_cond.true_label = body_label;
        br_cond.false_label = end_label;
        push_inst(std::move(br_cond));
    } else {
        // Infinite loop: for(;;)
        Instruction br_body;
        br_body.opcode = Instruction::BR;
        br_body.result_type = IRPrimitive::VOID;
        br_body.target_label = body_label;
        push_inst(std::move(br_body));
    }

    // Body block
    set_block(add_block(body_label));
    lower_stmt(*stmt.body);
    // Branch to increment
    Instruction br_body_end;
    br_body_end.opcode = Instruction::BR;
    br_body_end.result_type = IRPrimitive::VOID;
    br_body_end.target_label = inc_label;
    push_inst(std::move(br_body_end));

    // Increment block
    set_block(add_block(inc_label));
    if (stmt.inc) lower_expr(*stmt.inc);
    Instruction br_inc;
    br_inc.opcode = Instruction::BR;
    br_inc.result_type = IRPrimitive::VOID;
    br_inc.target_label = cond_label;
    push_inst(std::move(br_inc));

    // End block
    set_block(add_block(end_label));

    var_stack.pop_back();
    loop_stack.pop_back();
}

void IRBuilder::collect_switch_cases(const Stmt& stmt, std::vector<SwitchCase>& cases, std::string& default_label) {
    if (auto* cs = dynamic_cast<const CaseStmt*>(&stmt)) {
        long long val = 0;
        if (auto* ce = dynamic_cast<const ConstantExpr*>(cs->value.get()))
            val = ce->value;
        std::string label = "case_" + std::to_string(cases.size());
        cases.push_back({val, label});
        if (cs->body) collect_switch_cases(*cs->body, cases, default_label);
    } else if (auto* ds = dynamic_cast<const DefaultStmt*>(&stmt)) {
        if (default_label.empty())
            default_label = "case_default";
        if (ds->body) collect_switch_cases(*ds->body, cases, default_label);
    } else if (auto* cs = dynamic_cast<const CompoundStmt*>(&stmt)) {
        for (auto& s : cs->stmts)
            if (s) collect_switch_cases(*s, cases, default_label);
    } else if (auto* is = dynamic_cast<const IfStmt*>(&stmt)) {
        if (is->then_body) collect_switch_cases(*is->then_body, cases, default_label);
        if (is->else_body) collect_switch_cases(*is->else_body, cases, default_label);
    }
}

void IRBuilder::lower_switch(const SwitchStmt& stmt) {
    size_t cond_id = lower_expr(*stmt.cond);
    std::string end_label = new_label();

    loop_stack.push_back({end_label, ""});

    // Collect case values from body
    SwitchInfo info;
    info.cond_id = cond_id;
    info.end_label = end_label;
    collect_switch_cases(*stmt.body, info.cases, info.default_label);

    // Emit dispatch: if-else chain
    // Each case: if (cond == val) goto case_label; else next;
    // Default: goto default_label at end of chain
    std::string next_label = new_label();
    Instruction br_first;
    br_first.opcode = Instruction::BR;
    br_first.result_type = IRPrimitive::VOID;
    br_first.target_label = next_label;
    push_inst(std::move(br_first));

    set_block(add_block(next_label));
    for (size_t i = 0; i < info.cases.size(); i++) {
        std::string case_label = info.cases[i].label;
        long long case_val = info.cases[i].value;

        size_t case_cmp = emit(Instruction::CONST, lower_type(stmt.cond->result_type), {});
        current_block->instructions.back().const_val.int_val = case_val;
        size_t eq = emit(Instruction::EQ, IRPrimitive::I1, {cond_id, case_cmp});

        std::string else_label = (i + 1 < info.cases.size()) ? new_label() : info.default_label.empty() ? end_label : info.default_label;

        Instruction brc;
        brc.opcode = Instruction::BR_COND;
        brc.result_type = IRPrimitive::VOID;
        brc.operands = {eq};
        brc.true_label = case_label;
        brc.false_label = else_label;
        push_inst(std::move(brc));

        if (i + 1 < info.cases.size()) {
            set_block(add_block(else_label));
        }
    }

    // If no cases matched and no default, go to end
    if (info.cases.empty() || (!info.default_label.empty() && info.cases.size() > 0)) {
        // The last else label is already the current block (default or end)
    }
    if (info.cases.empty()) {
        Instruction br_end;
        br_end.opcode = Instruction::BR;
        br_end.result_type = IRPrimitive::VOID;
        br_end.target_label = end_label;
        push_inst(std::move(br_end));
    }

    // Switch body (with case labels)
    switch_stack.push_back(std::move(info));
    lower_stmt(*stmt.body);

    // If fall-through from last case, branch to end
    if (current_block && (current_block->instructions.empty() ||
        (current_block->instructions.back().opcode != Instruction::RET &&
         current_block->instructions.back().opcode != Instruction::BR &&
         current_block->instructions.back().opcode != Instruction::BR_COND))) {
        Instruction br_end;
        br_end.opcode = Instruction::BR;
        br_end.result_type = IRPrimitive::VOID;
        br_end.target_label = end_label;
        push_inst(std::move(br_end));
    }

    switch_stack.pop_back();

    // End block (for break)
    set_block(add_block(end_label));

    loop_stack.pop_back();
}

void IRBuilder::lower_return(const ReturnStmt& stmt) {
    if (stmt.value) {
        size_t val_id = lower_expr(*stmt.value);
        Instruction ret;
        ret.opcode = Instruction::RET;
        ret.result_type = IRPrimitive::VOID;
        ret.operands = {val_id};
        push_inst(std::move(ret));
    } else {
        Instruction ret;
        ret.opcode = Instruction::RET;
        ret.result_type = IRPrimitive::VOID;
        push_inst(std::move(ret));
    }
}

void IRBuilder::lower_break(const BreakStmt&) {
    if (loop_stack.empty()) return;
    Instruction br;
    br.opcode = Instruction::BR;
    br.result_type = IRPrimitive::VOID;
    br.target_label = loop_stack.back().break_label;
    push_inst(std::move(br));
}

void IRBuilder::lower_continue(const ContinueStmt&) {
    if (loop_stack.empty()) return;
    Instruction br;
    br.opcode = Instruction::BR;
    br.result_type = IRPrimitive::VOID;
    br.target_label = loop_stack.back().continue_label;
    push_inst(std::move(br));
}

void IRBuilder::lower_goto(const GotoStmt& stmt) {
    Instruction br;
    br.opcode = Instruction::BR;
    br.result_type = IRPrimitive::VOID;
    br.target_label = stmt.label;
    push_inst(std::move(br));
}

void IRBuilder::lower_label(const LabelStmt& stmt) {
    // Create a new block for the label
    std::string lbl = stmt.name;
    set_block(add_block(lbl));
    lower_stmt(*stmt.stmt);
}

void IRBuilder::lower_expr_stmt(const ExprStmt& stmt) {
    if (stmt.expr) lower_expr(*stmt.expr);
}

void IRBuilder::lower_decl_stmt(const DeclStmt& stmt) {
    if (stmt.decl) lower_decl(*stmt.decl);
}

// --- Declaration Lowering ---

void IRBuilder::lower_decl(const Decl& decl) {
    if (auto* d = dynamic_cast<const VariableDecl*>(&decl)) return lower_var_decl(*d);
    if (auto* d = dynamic_cast<const FunctionDecl*>(&decl)) return lower_fn_decl(*d);
    if (auto* d = dynamic_cast<const TemplateDecl*>(&decl)) return lower_template_decl(*d);
    // Skip other decl types (typedef, struct, etc.)
}

void IRBuilder::lower_var_decl(const VariableDecl& decl) {
    IRType var_type = lower_type(decl.var_type);

    if (!current_block) {
        // Global scope — emit global variable
        IRGlobal g;
        g.name = decl.name;
        g.type = var_type.copy();
        g.is_extern = decl.is_extern;
        g.is_static = decl.is_static;
        g.is_thread_local = decl.is_thread_local;
        g.has_init = decl.init != nullptr;
        if (decl.init) {
            if (auto* ce = dynamic_cast<const ConstantExpr*>(decl.init.get())) {
                g.init_val = ce->value;
            }
            // For non-constant initializers, we just note that it has an init
            // The actual initialization code is part of runtime, not compile-time
        }
        module.globals.push_back(std::move(g));
        return;
    }

    size_t alloca_id = emit(Instruction::ALLOCA, IRTypeFactory::ptr(var_type), {});
    current_block->instructions.back().extra_type = var_type.copy();

    if (!var_stack.empty())
        var_stack.back()[decl.name] = alloca_id;

    if (decl.init) {
        // Handle init list directly: store each field to the alloca
        if (auto* init_list = dynamic_cast<const InitListExpr*>(decl.init.get())) {
            Type result_type = decl.var_type;
            if (result_type.is_struct && result_type.has_members) {
                long long off = 0;
                for (size_t i = 0; i < init_list->inits.size() && i < result_type.members.size(); i++) {
                    size_t val = lower_expr(*init_list->inits[i]);
                    size_t off_val = emit(Instruction::CONST, IRPrimitive::I64, {});
                    current_block->instructions.back().const_val.int_val = off;
                    size_t field_addr = emit(Instruction::GEP, IRTypeFactory::ptr(IRPrimitive::VOID), {alloca_id, off_val});
                    current_block->instructions.back().extra_type = IRPrimitive::I8;
                    current_block->instructions.back().gep_index = 0;
                    emit(Instruction::STORE, IRPrimitive::VOID, {val, field_addr});
                    if (i + 1 < result_type.members.size())
                        off += compute_type_size(std::get<0>(result_type.members[i]));
                }
            } else {
                size_t init_val = lower_expr(*decl.init);
                emit(Instruction::STORE, IRPrimitive::VOID, {init_val, alloca_id});
            }
        } else {
            size_t init_val = lower_expr(*decl.init);
            emit(Instruction::STORE, IRPrimitive::VOID, {init_val, alloca_id});
        }
    }
}

void IRBuilder::lower_fn_decl(const FunctionDecl& decl) {
    IRFunction fn;
    fn.name = decl.name;
    // Extract return type from the function type
    if (decl.func_type.is_function) {
        fn.return_type = lower_type(*decl.func_type.return_type);
    } else {
        fn.return_type = lower_type(decl.func_type);
    }

    for (auto& param : decl.params) {
        if (auto* p = dynamic_cast<ParamVarDecl*>(param.get())) {
            fn.params.push_back({lower_type(p->param_type), p->name});
        }
    }

    fn.is_defined = decl.body != nullptr;
    fn.next_value_id = fn.params.size(); // params are value IDs 0..N-1
    current_fn_idx = module.functions.size();
    module.functions.push_back(std::move(fn));

    if (!decl.body) {
        set_current_fn(nullptr);
        current_block = nullptr;
        return;
    }

    current_fn()->blocks.clear();

    // Entry block
    auto* entry = add_block("entry");
    set_block(entry);

    // Alloca for each param and store the incoming SSA value
    for (size_t i = 0; i < current_fn()->params.size(); i++) {
        auto& [type, name] = current_fn()->params[i];
        size_t alloca_id = emit(Instruction::ALLOCA, IRTypeFactory::ptr(type), {});
        current_block->instructions.back().extra_type = type.copy();
        emit(Instruction::STORE, IRPrimitive::VOID, {i, alloca_id});
        var_stack.emplace_back();
        var_stack.back()[name] = alloca_id;
    }

    // Lower function body
    if (auto* cs = dynamic_cast<const CompoundStmt*>(decl.body.get())) {
        lower_compound(*cs);
    } else {
        lower_stmt(*decl.body);
    }

    // Ensure the last block has a terminator
    if (current_block && (current_block->instructions.empty() ||
        (current_block->instructions.back().opcode != Instruction::RET &&
         current_block->instructions.back().opcode != Instruction::BR &&
         current_block->instructions.back().opcode != Instruction::BR_COND))) {
        Instruction ret;
        ret.opcode = Instruction::RET;
        ret.result_type = IRPrimitive::VOID;
        push_inst(std::move(ret));
    }

    // Pop param allocas
    for (size_t i = 0; i < current_fn()->params.size(); i++)
        var_stack.pop_back();

    set_current_fn(nullptr);
    current_block = nullptr;
}

// --- Template Lowering ---

void IRBuilder::lower_template_decl(const TemplateDecl& decl) {
    // If the wrapped declaration is a function, populate type_params
    if (auto* fd = dynamic_cast<const FunctionDecl*>(decl.wrapped_decl.get())) {
        // Set generic params before lowering so lower_type can resolve them
        generic_params.clear();
        size_t idx = 0;
        for (auto& tp : decl.params) {
            if (tp->is_type_param) {
                generic_params.push_back(IRType::Param{idx, tp->name});
                idx++;
            }
        }

        // Lower the function body (parameter and return types will use generic_params)
        lower_fn_decl(*fd);

        // Set type_params on the generated function
        for (auto& fn : module.functions) {
            if (fn.name == fd->name) {
                fn.type_params = generic_params;
                break;
            }
        }

        generic_params.clear();
    } else if (auto* sd = dynamic_cast<const StructDecl*>(decl.wrapped_decl.get())) {
        // For struct templates, add to module structs with type_params
        IRStructDef sdef;
        sdef.name = sd->name;
        sdef.is_defined = true;
        size_t idx = 0;
        for (auto& tp : decl.params) {
            if (tp->is_type_param) {
                sdef.type_params.push_back(IRType::Param{idx, tp->name});
                idx++;
            }
        }
        // Lower fields
        for (auto& f : sd->fields) {
            if (auto* fd = dynamic_cast<FieldDecl*>(f.get())) {
                IRType ft = lower_type(fd->field_type);
                sdef.fields.push_back({std::move(ft), fd->name});
            }
        }
        module.structs.push_back(std::move(sdef));
    }
    // Other template-wrapped decls: skip for now
}

size_t IRBuilder::lower_template_id(const TemplateIdExpr& expr) {
    // Lower the call arguments
    std::vector<size_t> arg_ids;
    for (auto& arg : expr.call_args)
        arg_ids.push_back(lower_expr(*arg));

    // Get the concrete types for the template args
    std::vector<IRType> type_args;
    for (auto& ta : expr.template_args)
        type_args.push_back(lower_type(ta));

    // Find the generic function in the module
    IRFunction* generic_fn = module.find_function(expr.template_name);
    if (!generic_fn || !generic_fn->is_generic()) {
        size_t id = emit(Instruction::CALL, lower_type(expr.result_type), arg_ids);
        current_block->instructions.back().callee_name = expr.template_name;
        return id;
    }

    // Instantiate the generic function
    size_t old_fn_count = module.functions.size();
    IRInstantiator inst;
    IRCache cache;
    IRFunction* concrete = cache.get_or_instantiate(inst, module,
                                                     expr.template_name, type_args);

    // Re-acquire current_fn (vector may have reallocated)
    if (old_fn_count < module.functions.size() && current_fn_idx != (size_t)-1) {
        // current_fn_idx is still valid index (vector reallocation doesn't change indices)
        // But current_block pointer may still be valid since blocks are stable
    }

    if (concrete) {
        size_t id = emit(Instruction::CALL, lower_type(expr.result_type), arg_ids);
        current_block->instructions.back().callee_name = concrete->name;
        return id;
    }

    size_t id = emit(Instruction::CALL, lower_type(expr.result_type), arg_ids);
    current_block->instructions.back().callee_name = expr.template_name;
    return id;
}

// --- Entry Point ---

void IRBuilder::lower_translation_unit(const TranslationUnit& tu) {
    // Pre-allocate space to reduce pointer invalidation
    module.functions.reserve(tu.decls.size() * 2);
    for (auto& decl : tu.decls)
        lower_decl(*decl);
}
