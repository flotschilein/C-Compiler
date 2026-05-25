#include "ir_printer.h"
#include <sstream>
#include <cassert>

std::string IRPrinter::prim_to_string(IRPrimitive p) {
    switch (p) {
        case IRPrimitive::VOID:  return "void";
        case IRPrimitive::I1:    return "i1";
        case IRPrimitive::I8:    return "i8";
        case IRPrimitive::I16:   return "i16";
        case IRPrimitive::I32:   return "i32";
        case IRPrimitive::I64:   return "i64";
        case IRPrimitive::U8:    return "u8";
        case IRPrimitive::U16:   return "u16";
        case IRPrimitive::U32:   return "u32";
        case IRPrimitive::U64:   return "u64";
        case IRPrimitive::F32:   return "f32";
        case IRPrimitive::F64:   return "f64";
    }
    return "?";
}

std::string IRPrinter::type_to_string(const IRType& type) {
    if (type.is_prim()) return prim_to_string(type.as_prim());
    if (type.is_ptr())  return type_to_string(*type.as_ptr().pointee) + "*";
    if (type.is_arr()) {
        auto& a = type.as_arr();
        return "[" + type_to_string(*a.element) + " x " + std::to_string(a.size) + "]";
    }
    if (type.is_fn()) {
        auto& f = type.as_fn();
        std::string s = type_to_string(*f.return_type) + "(";
        for (size_t i = 0; i < f.params.size(); i++) {
            if (i) s += ", ";
            s += type_to_string(f.params[i]);
        }
        if (f.variadic) { if (!f.params.empty()) s += ", "; s += "..."; }
        s += ")";
        return s;
    }
    if (type.is_struct()) {
        auto& s = type.as_struct();
        std::string r = "%" + s.name;
        if (!s.args.empty()) {
            r += "<";
            for (size_t i = 0; i < s.args.size(); i++) {
                if (i) r += ", ";
                r += type_to_string(s.args[i]);
            }
            r += ">";
        }
        return r;
    }
    if (type.is_param()) {
        auto& p = type.as_param();
        return "%T" + std::to_string(p.index);
    }
    return "?";
}

std::string IRPrinter::opcode_to_string(Instruction::Opcode op) {
    switch (op) {
        case Instruction::ADD:       return "add";
        case Instruction::NEG:       return "neg";
        case Instruction::SUB:       return "sub";
        case Instruction::MUL:       return "mul";
        case Instruction::DIV:       return "div";
        case Instruction::REM:       return "rem";
        case Instruction::EQ:        return "eq";
        case Instruction::NE:        return "ne";
        case Instruction::LT:        return "lt";
        case Instruction::LE:        return "le";
        case Instruction::GT:        return "gt";
        case Instruction::GE:        return "ge";
        case Instruction::LOGIC_AND: return "and";
        case Instruction::LOGIC_OR:  return "or";
        case Instruction::LOGIC_NOT: return "not";
        case Instruction::BIT_AND:   return "bit_and";
        case Instruction::BIT_OR:    return "bit_or";
        case Instruction::BIT_XOR:   return "bit_xor";
        case Instruction::SHL:       return "shl";
        case Instruction::SHR:       return "shr";
        case Instruction::BIT_NOT:   return "bit_not";
        case Instruction::ALLOCA:    return "alloca";
        case Instruction::LOAD:      return "load";
        case Instruction::STORE:     return "store";
        case Instruction::GEP:       return "gep";
        case Instruction::CAST:      return "cast";
        case Instruction::TRUNC:     return "trunc";
        case Instruction::ZEXT:      return "zext";
        case Instruction::SEXT:      return "sext";
        case Instruction::FPTOUI:    return "fptoui";
        case Instruction::FPTOSI:    return "fptosi";
        case Instruction::UITOFP:    return "uitofp";
        case Instruction::SITOFP:    return "sitofp";
        case Instruction::CALL:      return "call";
        case Instruction::BR:        return "br";
        case Instruction::BR_COND:   return "br_cond";
        case Instruction::RET:       return "ret";
        case Instruction::PHI:       return "phi";
        case Instruction::SELECT:    return "select";
        case Instruction::EXTRACT:   return "extract";
        case Instruction::CONST:     return "const";
    }
    return "?";
}

std::string IRPrinter::value_ref(size_t id) {
    return "%" + std::to_string(id);
}

std::string IRPrinter::print_instruction(const Instruction& inst) {
    std::ostringstream os;

    switch (inst.opcode) {
    case Instruction::CONST:
        os << "  " << value_ref(0) /* placeholder */ << " = " << opcode_to_string(inst.opcode)
           << " " << type_to_string(inst.result_type) << " ";
        if (inst.result_type.is_prim()) {
            auto p = inst.result_type.as_prim();
            if (p == IRPrimitive::F32 || p == IRPrimitive::F64)
                os << inst.const_val.float_val;
            else
                os << inst.const_val.int_val;
        } else {
            os << inst.const_val.int_val;
        }
        break;

    case Instruction::ALLOCA:
        os << "  " << value_ref(0) << " = " << opcode_to_string(inst.opcode)
           << " " << type_to_string(inst.extra_type);
        break;

    case Instruction::LOAD:
        os << "  " << value_ref(0) << " = " << opcode_to_string(inst.opcode)
           << " " << type_to_string(inst.result_type)
           << ", " << value_ref(inst.operands[0]);
        break;

    case Instruction::STORE:
        os << "  " << opcode_to_string(inst.opcode)
           << " " << value_ref(inst.operands[0])
           << ", " << value_ref(inst.operands[1]);
        break;

    case Instruction::BR:
        os << "  " << opcode_to_string(inst.opcode) << " " << inst.target_label;
        break;

    case Instruction::BR_COND:
        os << "  " << opcode_to_string(inst.opcode) << " " << value_ref(inst.operands[0])
           << ", " << inst.true_label << ", " << inst.false_label;
        break;

    case Instruction::RET:
        os << "  " << opcode_to_string(inst.opcode);
        if (!inst.operands.empty())
            os << " " << value_ref(inst.operands[0]);
        break;

    case Instruction::CALL:
        os << "  " << value_ref(0) << " = " << opcode_to_string(inst.opcode)
           << " " << type_to_string(inst.result_type)
           << " @" << inst.callee_name << "(";
        for (size_t i = 0; i < inst.operands.size(); i++) {
            if (i) os << ", ";
            os << value_ref(inst.operands[i]);
        }
        os << ")";
        break;

    case Instruction::PHI:
        os << "  " << value_ref(0) << " = " << opcode_to_string(inst.opcode)
           << " " << type_to_string(inst.result_type) << " ";
        for (size_t i = 0; i < inst.phi_incoming.size(); i++) {
            if (i) os << ", ";
            os << "[" << value_ref(inst.phi_incoming[i].first)
               << ", " << inst.phi_incoming[i].second << "]";
        }
        break;

    case Instruction::SELECT:
        os << "  " << value_ref(0) << " = " << opcode_to_string(inst.opcode)
           << " " << type_to_string(inst.result_type)
           << " " << value_ref(inst.operands[0])
           << ", " << value_ref(inst.operands[1])
           << ", " << value_ref(inst.operands[2]);
        break;

    case Instruction::GEP:
        os << "  " << value_ref(0) << " = " << opcode_to_string(inst.opcode)
           << " " << type_to_string(inst.extra_type)
           << ", " << value_ref(inst.operands[0])
           << ", " << inst.gep_index;
        break;

    case Instruction::CAST: case Instruction::TRUNC: case Instruction::ZEXT:
    case Instruction::SEXT: case Instruction::FPTOUI: case Instruction::FPTOSI:
    case Instruction::UITOFP: case Instruction::SITOFP:
        os << "  " << value_ref(0) << " = " << opcode_to_string(inst.opcode)
           << " " << value_ref(inst.operands[0])
           << " to " << type_to_string(inst.extra_type);
        break;

    case Instruction::EXTRACT:
        os << "  " << value_ref(0) << " = " << opcode_to_string(inst.opcode)
           << " " << value_ref(inst.operands[0])
           << ", " << inst.extract_index;
        break;

    default: {
        // Binary ops
        const char* sep = " ";
        os << "  " << value_ref(0) << " = " << opcode_to_string(inst.opcode) << sep;
        if (!inst.result_type.is_void())
            os << type_to_string(inst.result_type) << sep;
        for (size_t i = 0; i < inst.operands.size(); i++) {
            if (i) os << ", ";
            os << value_ref(inst.operands[i]);
        }
        break;
    }
    }

    return os.str();
}

std::string IRPrinter::print_block(const IRBlock& block) {
    std::ostringstream os;
    os << block.label << ":\n";

    size_t val_id = 0; // We don't know the actual IDs here; this is a placeholder
    for (auto& inst : block.instructions) {
        std::string line = print_instruction(inst);
        // Replace placeholder with actual value_ref
        // Actually, the printer doesn't have value ID context. Let me fix this.
        os << "  " << line << "\n";
    }

    return os.str();
}

static const char* op_sep(Instruction::Opcode op) {
    (void)op;
    return " ";
}

static bool is_terminator(Instruction::Opcode op) {
    return op == Instruction::BR || op == Instruction::BR_COND || op == Instruction::RET;
}

static bool produces_value(Instruction::Opcode op) {
    return op != Instruction::STORE && op != Instruction::BR
        && op != Instruction::BR_COND && op != Instruction::RET;
}

std::string IRPrinter::print_function(const IRFunction& fn) {
    std::ostringstream os;

    os << "function @" << fn.name;
    if (!fn.type_params.empty()) {
        os << "<";
        for (size_t i = 0; i < fn.type_params.size(); i++) {
            if (i) os << ", ";
            os << fn.type_params[i].name;
        }
        os << ">";
    }
    os << "(";
    for (size_t i = 0; i < fn.params.size(); i++) {
        if (i) os << ", ";
        os << type_to_string(fn.params[i].first) << " %" << i << " " << fn.params[i].second;
    }
    os << ") -> " << type_to_string(fn.return_type) << "\n";

    if (!fn.is_defined) {
        os << "  ; external\n";
        return os.str();
    }

    size_t vid = fn.params.size();
    for (auto& block : fn.blocks) {
        os << block.label << ":\n";
        for (auto& inst : block.instructions) {
            bool has_val = produces_value(inst.opcode);
            os << "  ";
            if (has_val) {
                os << "%" << vid << " = ";
            }
            os << opcode_to_string(inst.opcode);

            switch (inst.opcode) {
            case Instruction::CONST:
                os << " " << type_to_string(inst.result_type);
                if (inst.result_type.is_prim()) {
                    auto p = inst.result_type.as_prim();
                    if (p == IRPrimitive::F32 || p == IRPrimitive::F64)
                        os << " " << inst.const_val.float_val;
                    else
                        os << " " << inst.const_val.int_val;
                } else {
                    os << " " << inst.const_val.int_val;
                }
                break;

            case Instruction::ALLOCA:
                os << " " << type_to_string(inst.extra_type);
                break;

            case Instruction::LOAD:
                os << " " << type_to_string(inst.result_type)
                   << ", %" << inst.operands[0];
                break;

            case Instruction::STORE:
                os << " %" << inst.operands[0] << ", %" << inst.operands[1];
                break;

            case Instruction::BR:
                os << " " << inst.target_label;
                break;

            case Instruction::BR_COND:
                os << " %" << inst.operands[0]
                   << ", " << inst.true_label << ", " << inst.false_label;
                break;

            case Instruction::RET:
                if (!inst.operands.empty())
                    os << " %" << inst.operands[0];
                break;

            case Instruction::CALL:
                os << " " << type_to_string(inst.result_type)
                   << " @" << inst.callee_name << "(";
                for (size_t i = 0; i < inst.operands.size(); i++) {
                    if (i) os << ", ";
                    os << "%" << inst.operands[i];
                }
                os << ")";
                break;

            case Instruction::PHI:
                os << " " << type_to_string(inst.result_type);
                for (size_t i = 0; i < inst.phi_incoming.size(); i++) {
                    os << " [%" << inst.phi_incoming[i].first
                       << ", " << inst.phi_incoming[i].second << "]";
                }
                break;

            case Instruction::SELECT:
                os << " " << type_to_string(inst.result_type)
                   << " %" << inst.operands[0]
                   << ", %" << inst.operands[1]
                   << ", %" << inst.operands[2];
                break;

            case Instruction::GEP:
                os << " " << type_to_string(inst.extra_type)
                   << ", %" << inst.operands[0];
                for (size_t i = 1; i < inst.operands.size(); i++)
                    os << ", %" << inst.operands[i];
                break;

            case Instruction::CAST: case Instruction::TRUNC:
            case Instruction::ZEXT: case Instruction::SEXT:
            case Instruction::FPTOUI: case Instruction::FPTOSI:
            case Instruction::UITOFP: case Instruction::SITOFP:
                os << " %" << inst.operands[0]
                   << " to " << type_to_string(inst.extra_type);
                break;

            case Instruction::EXTRACT:
                os << " %" << inst.operands[0] << ", " << inst.extract_index;
                break;

            default: {
                os << " " << type_to_string(inst.result_type);
                for (auto op : inst.operands)
                    os << " %" << op;
                break;
            }
            }

            os << "\n";
            vid++;
        }
    }

    return os.str();
}

std::string IRPrinter::print_module(const IRModule& mod) {
    std::ostringstream os;
    os << "; IR Module\n\n";

    // Print struct definitions
    for (auto& s : mod.structs) {
        if (!s.is_defined) continue;
        os << "struct %" << s.name;
        if (!s.type_params.empty()) {
            os << "<";
            for (size_t i = 0; i < s.type_params.size(); i++) {
                if (i) os << ", ";
                os << s.type_params[i].name;
            }
            os << ">";
        }
        os << " {\n";
        for (auto& [type, name] : s.fields) {
            os << "  " << type_to_string(type) << " " << name << ";\n";
        }
        os << "}\n\n";
    }

    // Print global variables
    for (auto& g : mod.globals) {
        os << "global @" << g.name << " : " << type_to_string(g.type);
        if (g.is_static) os << " static";
        if (g.is_extern) os << " extern";
        if (g.has_init) os << " = " << g.init_val;
        os << "\n";
    }
    if (!mod.globals.empty()) os << "\n";

    // Print global strings
    for (auto& s : mod.global_strings) {
        os << "global_string \"" << s << "\"\n";
    }
    if (!mod.global_strings.empty()) os << "\n";

    // Print functions
    for (auto& fn : mod.functions) {
        os << print_function(fn) << "\n";
    }

    return os.str();
}
