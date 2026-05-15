#include "ir_verifier.h"
#include "ir_printer.h"
#include <set>
#include <map>
#include <cassert>
#include <iostream>

void IRVerifier::clear() {
    issues.clear();
}

void IRVerifier::error(const std::string& msg, const std::string& fn, const std::string& block) {
    issues.push_back({Issue::ERROR, msg, fn, block});
}

void IRVerifier::warning(const std::string& msg, const std::string& fn, const std::string& block) {
    issues.push_back({Issue::WARNING, msg, fn, block});
}

bool IRVerifier::has_errors() const {
    for (auto& i : issues)
        if (i.severity == Issue::ERROR) return true;
    return false;
}

void IRVerifier::print_report() const {
    for (auto& i : issues) {
        std::string tag = (i.severity == Issue::ERROR) ? "error" : "warning";
        std::cout << "  " << tag << ": " << i.message;
        if (!i.function.empty()) std::cout << " [in " << i.function << "]";
        if (!i.block.empty()) std::cout << " [" << i.block << "]";
        std::cout << "\n";
    }
}

// --- Check 1: No dangling TypeParams (CIR must be fully concrete) ---

struct TypeParamFinder {
    std::vector<IRType::Param> found;
    void visit(const IRType& type) {
        if (type.is_param()) {
            found.push_back(type.as_param());
        } else if (type.is_ptr()) {
            visit(*type.as_ptr().pointee);
        } else if (type.is_arr()) {
            visit(*type.as_arr().element);
        } else if (type.is_fn()) {
            visit(*type.as_fn().return_type);
            for (auto& p : type.as_fn().params) visit(p);
        } else if (type.is_struct()) {
            for (auto& a : type.as_struct().args) visit(a);
        }
    }
};

void IRVerifier::check_no_dangling_type_params(const IRType& type) {
    TypeParamFinder f;
    f.visit(type);
    for (auto& p : f.found) {
        error("dangling type parameter %T" + std::to_string(p.index) + " (" + p.name + ") in concrete IR", "(global)");
    }
}

// --- Check 2: SSA validity ---

void IRVerifier::check_ssa(const IRFunction& fn) {
    // Every value ID must be defined exactly once.
    // Track which IDs are defined in each block.
    std::set<size_t> defined;
    std::map<size_t, std::string> defined_in_block;

    size_t expected_id = 0;
    // Params are implicitly IDs 0..N-1
    for (size_t i = 0; i < fn.params.size(); i++) {
        defined.insert(i);
        defined_in_block[i] = "(params)";
        expected_id = i + 1;
    }

    for (auto& block : fn.blocks) {
        for (auto& inst : block.instructions) {
            size_t id = expected_id;
            expected_id++;

            // Check redefinition
            if (defined.count(id)) {
                error("SSA violation: value %" + std::to_string(id) + " defined more than once",
                      fn.name, block.label);
            }
            defined.insert(id);
            defined_in_block[id] = block.label;

            // Check that all operand IDs exist (forward references NOT allowed in SSA)
            // But PHI can reference values from predecessor blocks
            for (auto op : inst.operands) {
                if (!defined.count(op) && op < id) {
                    // This is a USE BEFORE DEF within the same block — illegal in SSA
                    error("SSA violation: value %" + std::to_string(op) + " used before definition",
                          fn.name, block.label);
                }
            }

            // For PHI, incoming values should be defined already
            for (auto& [val, label] : inst.phi_incoming) {
                if (!defined.count(val)) {
                    error("SSA violation: phi uses undefined value %" + std::to_string(val),
                          fn.name, block.label);
                }
            }
        }
    }

    // Check that total defined values matches next_value_id
    if (expected_id != fn.next_value_id) {
        error("value ID count mismatch: expected " + std::to_string(expected_id)
              + " values but next_value_id is " + std::to_string(fn.next_value_id),
              fn.name);
    }
}

// --- Check 3: Block terminators ---

void IRVerifier::check_terminators(const IRFunction& fn) {
    for (auto& block : fn.blocks) {
        if (block.instructions.empty()) {
            error("block has no instructions (no terminator)", fn.name, block.label);
            continue;
        }

        Instruction::Opcode last_op = block.instructions.back().opcode;
        bool is_terminator = (last_op == Instruction::BR ||
                              last_op == Instruction::BR_COND ||
                              last_op == Instruction::RET);

        if (!is_terminator) {
            error("block does not end with a terminator (last op: "
                  + IRPrinter::opcode_to_string(last_op) + ")",
                  fn.name, block.label);
        }

        // Check that no non-terminator instructions appear after the terminator
        bool seen_term = false;
        for (auto& inst : block.instructions) {
            if (inst.opcode == Instruction::BR ||
                inst.opcode == Instruction::BR_COND ||
                inst.opcode == Instruction::RET) {
                seen_term = true;
            } else if (seen_term) {
                error("instruction after terminator", fn.name, block.label);
                break;
            }
        }

        // Check br_cond has correct operands
        if (last_op == Instruction::BR_COND) {
            auto& inst = block.instructions.back();
            if (inst.operands.size() != 1) {
                error("br_cond expects 1 operand (condition), got " + std::to_string(inst.operands.size()),
                      fn.name, block.label);
            }
            if (inst.true_label.empty() || inst.false_label.empty()) {
                error("br_cond missing true/false label", fn.name, block.label);
            }
        }

        if (last_op == Instruction::BR) {
            auto& inst = block.instructions.back();
            if (inst.target_label.empty()) {
                error("br missing target label", fn.name, block.label);
            }
        }
    }
}

// --- Check 4: Control flow — all referenced labels exist ---

void IRVerifier::check_control_flow(const IRFunction& fn) {
    // Collect all defined labels
    std::set<std::string> labels;
    for (auto& block : fn.blocks)
        labels.insert(block.label);

    // Check entry block exists
    bool has_entry = false;
    for (auto& block : fn.blocks) {
        if (block.label == "entry") has_entry = true;
        break; // first block is entry
    }
    if (!fn.blocks.empty() && fn.blocks[0].label != "entry") {
        warning("first block is not 'entry' (got '" + fn.blocks[0].label + "')", fn.name);
    }

    // Check all branch targets exist
    for (auto& block : fn.blocks) {
        for (auto& inst : block.instructions) {
            if (inst.opcode == Instruction::BR) {
                if (!labels.count(inst.target_label)) {
                    error("branch to undefined label '" + inst.target_label + "'",
                          fn.name, block.label);
                }
            }
            if (inst.opcode == Instruction::BR_COND) {
                if (!labels.count(inst.true_label)) {
                    error("br_cond true target '" + inst.true_label + "' not found",
                          fn.name, block.label);
                }
                if (!labels.count(inst.false_label)) {
                    error("br_cond false target '" + inst.false_label + "' not found",
                          fn.name, block.label);
                }
            }
        }
    }
}

// --- Check 5: Instruction operand types ---

static size_t type_width(IRPrimitive p) {
    switch (p) {
        case IRPrimitive::I1:  return 1;
        case IRPrimitive::I8:  return 8;
        case IRPrimitive::I16: return 16;
        case IRPrimitive::I32: return 32;
        case IRPrimitive::I64: return 64;
        case IRPrimitive::U8:  return 8;
        case IRPrimitive::U16: return 16;
        case IRPrimitive::U32: return 32;
        case IRPrimitive::U64: return 64;
        case IRPrimitive::F32: return 32;
        case IRPrimitive::F64: return 64;
        default:               return 0;
    }
}

void IRVerifier::check_instruction_types(const IRFunction& fn) {
    // For now, just basic sanity checks
    std::map<size_t, IRType> value_types;

    size_t id = 0;
    for (size_t i = 0; i < fn.params.size(); i++) {
        value_types[id] = fn.params[i].first.copy();
        id++;
    }

    for (auto& block : fn.blocks) {
        for (auto& inst : block.instructions) {
            IRType result_t = inst.result_type.copy();

            switch (inst.opcode) {
            case Instruction::ADD:
            case Instruction::SUB:
            case Instruction::MUL:
            case Instruction::DIV:
            case Instruction::REM:
            case Instruction::BIT_AND:
            case Instruction::BIT_OR:
            case Instruction::BIT_XOR:
            case Instruction::SHL:
            case Instruction::SHR:
                // Binary ops: operands should have same type as result
                if (inst.operands.size() != 2)
                    error("binary op expects 2 operands", fn.name, block.label);
                break;

            case Instruction::EQ: case Instruction::NE:
            case Instruction::LT: case Instruction::LE:
            case Instruction::GT: case Instruction::GE:
                // Comparisons: result should be i1
                if (!result_t.is_prim() || result_t.as_prim() != IRPrimitive::I1)
                    warning("comparison result should be i1, got " + IRPrinter::type_to_string(result_t),
                            fn.name, block.label);
                break;

            case Instruction::LOAD:
                if (inst.operands.size() != 1)
                    error("load expects 1 operand", fn.name, block.label);
                if (!result_t.is_prim() && !result_t.is_ptr() && !result_t.is_struct())
                    warning("load result type looks unusual: " + IRPrinter::type_to_string(result_t),
                            fn.name, block.label);
                break;

            case Instruction::STORE:
                if (inst.operands.size() != 2)
                    error("store expects 2 operands (value, ptr)", fn.name, block.label);
                break;

            case Instruction::ALLOCA:
                if (inst.extra_type.is_void())
                    warning("alloca with void extra_type", fn.name, block.label);
                if (!result_t.is_ptr())
                    warning("alloca result should be a pointer type", fn.name, block.label);
                break;

            case Instruction::CALL:
                if (inst.callee_name.empty())
                    warning("call with empty callee name", fn.name, block.label);
                break;

            case Instruction::BR_COND:
                if (inst.operands.size() != 1)
                    error("br_cond expects 1 condition operand", fn.name, block.label);
                if (inst.true_label.empty() || inst.false_label.empty())
                    error("br_cond missing labels", fn.name, block.label);
                break;

            case Instruction::RET:
                if (fn.return_type.is_void() && !inst.operands.empty())
                    warning("returning value from void function", fn.name, block.label);
                if (!fn.return_type.is_void() && inst.operands.empty())
                    warning("no return value in non-void function", fn.name, block.label);
                break;

            case Instruction::PHI:
                if (inst.phi_incoming.size() < 2)
                    warning("phi with fewer than 2 incoming edges", fn.name, block.label);
                break;

            default:
                break;
            }

            // Record result type
            value_types[id] = std::move(result_t);
            id++;
        }
    }
}

// --- Check 6: Struct definitions ---

void IRVerifier::check_struct_defs(const IRModule& mod) {
    for (auto& s : mod.structs) {
        if (!s.is_defined) continue;
        for (auto& [type, name] : s.fields) {
            if (type.has_params()) {
                error("struct '" + s.name + "' has field '" + name
                      + "' with unresolved type parameters", "(structs)");
            }
        }
    }
}

// --- Verify module ---

void IRVerifier::verify(const IRModule& mod) {
    clear();

    // Struct definitions
    check_struct_defs(mod);

    // Each function
    for (auto& fn : mod.functions) {
        check_function(fn);
    }
}

void IRVerifier::check_function(const IRFunction& fn) {
    // Check for dangling type params
    if (!fn.is_generic()) {
        check_no_dangling_type_params(fn.return_type);
        for (auto& [t, _] : fn.params)
            check_no_dangling_type_params(t);
    }

    // SSA
    check_ssa(fn);

    // Terminators
    check_terminators(fn);

    // Control flow
    check_control_flow(fn);

    // Instruction types
    check_instruction_types(fn);
}
