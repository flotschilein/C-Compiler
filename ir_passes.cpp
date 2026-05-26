#include "ir_passes.h"
#include "ir_printer.h"
#include <map>
#include <set>
#include <vector>
#include <optional>
#include <cmath>
#include <cstring>
#include <cstdint>

// --- PassManager ---

void PassManager::add_pass(std::unique_ptr<IRPass> pass) {
    passes.push_back(std::move(pass));
}

bool PassManager::run(IRModule& mod) {
    bool any_changed = false;
    for (auto& pass : passes) {
        bool changed = pass->run(mod);
        if (changed) any_changed = true;
    }
    return any_changed;
}

// --- Utility Helpers ---

static bool is_int_prim(IRPrimitive p) {
    switch (p) {
        case IRPrimitive::I1:
        case IRPrimitive::I8:
        case IRPrimitive::I16:
        case IRPrimitive::I32:
        case IRPrimitive::I64:
        case IRPrimitive::U8:
        case IRPrimitive::U16:
        case IRPrimitive::U32:
        case IRPrimitive::U64:
            return true;
        default:
            return false;
    }
}

static bool is_float_prim(IRPrimitive p) {
    return p == IRPrimitive::F32 || p == IRPrimitive::F64;
}

static int prim_width(IRPrimitive p) {
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

static long long mask_int(long long val, int bits) {
    if (bits >= 64) return val;
    unsigned long long mask = (1ULL << bits) - 1;
    return (long long)((unsigned long long)val & mask);
}

static bool is_unsigned_prim(IRPrimitive p) {
    return p == IRPrimitive::U8 || p == IRPrimitive::U16
        || p == IRPrimitive::U32 || p == IRPrimitive::U64;
}

struct FoldedConst {
    bool is_float;
    long long int_val;
    double float_val;
};

static bool has_side_effects(Instruction::Opcode op) {
    switch (op) {
        case Instruction::STORE:
        case Instruction::CALL:
        case Instruction::RET:
        case Instruction::BR:
        case Instruction::BR_COND:
            return true;
        default:
            return false;
    }
}

static std::optional<FoldedConst> get_const_value(
    size_t vid,
    const std::map<size_t, FoldedConst>& const_vals,
    const IRType& type)
{
    auto it = const_vals.find(vid);
    if (it == const_vals.end()) return {};
    return it->second;
}

static void apply_forward_map(IRFunction& fn, const std::map<size_t, size_t>& forward_map) {
    for (auto& block : fn.blocks) {
        for (auto& inst : block.instructions) {
            for (auto& op : inst.operands) {
                auto it = forward_map.find(op);
                if (it != forward_map.end()) op = it->second;
            }
            for (auto& [val, label] : inst.phi_incoming) {
                auto it = forward_map.find(val);
                if (it != forward_map.end()) val = it->second;
            }
        }
    }
}

// Renumber value IDs to be sequential after removals.
// Returns false if no renumbering needed.
static bool renumber_values(IRFunction& fn) {
    // Build old -> new mapping
    size_t n = fn.next_value_id;
    std::vector<size_t> old_to_new(n, (size_t)-1);

    for (size_t i = 0; i < fn.params.size(); i++)
        old_to_new[i] = i;

    size_t new_vid = fn.params.size();
    size_t old_vid = fn.params.size();

    for (auto& block : fn.blocks) {
        for (auto& inst : block.instructions) {
            old_to_new[old_vid] = new_vid;
            old_vid++;
            new_vid++;
        }
    }

    if (new_vid == old_vid)
        return false; // no change

    // Apply mapping to operands
    for (auto& block : fn.blocks) {
        for (auto& inst : block.instructions) {
            for (auto& op : inst.operands)
                op = old_to_new[op];
            for (auto& [val, label] : inst.phi_incoming)
                val = old_to_new[val];
        }
    }

    fn.next_value_id = new_vid;
    return true;
}

// --- SimplifyPass ---

static bool try_fold_const(const Instruction& inst,
                           const std::map<size_t, FoldedConst>& const_vals,
                           FoldedConst& result)
{
    if (inst.operands.empty()) return false;

    // Resolve all operands to constants
    std::vector<FoldedConst> args;
    for (auto op : inst.operands) {
        auto it = const_vals.find(op);
        if (it == const_vals.end()) return false;
        args.push_back(it->second);
    }

    auto& rt = inst.result_type;
    bool res_int = rt.is_prim() && is_int_prim(rt.as_prim());
    bool res_float = rt.is_prim() && is_float_prim(rt.as_prim());
    if (!res_int && !res_float) return false;

    int width = rt.is_prim() ? prim_width(rt.as_prim()) : 64;
    result.is_float = res_float;

    switch (inst.opcode) {
    case Instruction::ADD:
        if (res_int) result.int_val = mask_int(args[0].int_val + args[1].int_val, width);
        else result.float_val = args[0].float_val + args[1].float_val;
        return true;

    case Instruction::SUB:
        if (res_int) result.int_val = mask_int(args[0].int_val - args[1].int_val, width);
        else result.float_val = args[0].float_val - args[1].float_val;
        return true;

    case Instruction::MUL:
        if (res_int) result.int_val = mask_int(args[0].int_val * args[1].int_val, width);
        else result.float_val = args[0].float_val * args[1].float_val;
        return true;

    case Instruction::DIV:
        if (args[1].int_val == 0) return false; // avoid divide-by-zero
        if (res_int) result.int_val = mask_int(args[0].int_val / args[1].int_val, width);
        else result.float_val = args[0].float_val / args[1].float_val;
        return true;

    case Instruction::REM:
        if (args[1].int_val == 0) return false;
        result.int_val = mask_int(args[0].int_val % args[1].int_val, width);
        result.is_float = false;
        return true;

    case Instruction::NEG:
        if (res_int) result.int_val = mask_int(-args[0].int_val, width);
        else result.float_val = -args[0].float_val;
        return true;

    case Instruction::EQ:
        if (res_int) result.int_val = (args[0].int_val == args[1].int_val) ? 1 : 0;
        else result.int_val = (args[0].float_val == args[1].float_val) ? 1 : 0;
        result.is_float = false;
        return true;

    case Instruction::NE:
        if (res_int) result.int_val = (args[0].int_val != args[1].int_val) ? 1 : 0;
        else result.int_val = (args[0].float_val != args[1].float_val) ? 1 : 0;
        result.is_float = false;
        return true;

    case Instruction::LT: {
        result.is_float = false;
        if (res_float) {
            result.int_val = (args[0].float_val < args[1].float_val) ? 1 : 0;
        } else if (is_unsigned_prim(rt.as_prim())) {
            auto a = (unsigned long long)args[0].int_val;
            auto b = (unsigned long long)args[1].int_val;
            result.int_val = (a < b) ? 1 : 0;
        } else {
            result.int_val = (args[0].int_val < args[1].int_val) ? 1 : 0;
        }
        return true;
    }

    case Instruction::LE: {
        result.is_float = false;
        if (res_float) {
            result.int_val = (args[0].float_val <= args[1].float_val) ? 1 : 0;
        } else if (is_unsigned_prim(rt.as_prim())) {
            auto a = (unsigned long long)args[0].int_val;
            auto b = (unsigned long long)args[1].int_val;
            result.int_val = (a <= b) ? 1 : 0;
        } else {
            result.int_val = (args[0].int_val <= args[1].int_val) ? 1 : 0;
        }
        return true;
    }

    case Instruction::GT: {
        result.is_float = false;
        if (res_float) {
            result.int_val = (args[0].float_val > args[1].float_val) ? 1 : 0;
        } else if (is_unsigned_prim(rt.as_prim())) {
            auto a = (unsigned long long)args[0].int_val;
            auto b = (unsigned long long)args[1].int_val;
            result.int_val = (a > b) ? 1 : 0;
        } else {
            result.int_val = (args[0].int_val > args[1].int_val) ? 1 : 0;
        }
        return true;
    }

    case Instruction::GE: {
        result.is_float = false;
        if (res_float) {
            result.int_val = (args[0].float_val >= args[1].float_val) ? 1 : 0;
        } else if (is_unsigned_prim(rt.as_prim())) {
            auto a = (unsigned long long)args[0].int_val;
            auto b = (unsigned long long)args[1].int_val;
            result.int_val = (a >= b) ? 1 : 0;
        } else {
            result.int_val = (args[0].int_val >= args[1].int_val) ? 1 : 0;
        }
        return true;
    }

    case Instruction::LOGIC_NOT:
        result.is_float = false;
        result.int_val = args[0].int_val ? 0 : 1;
        return true;

    case Instruction::BIT_AND:
        result.is_float = false;
        result.int_val = mask_int(args[0].int_val & args[1].int_val, width);
        return true;

    case Instruction::BIT_OR:
        result.is_float = false;
        result.int_val = mask_int(args[0].int_val | args[1].int_val, width);
        return true;

    case Instruction::BIT_XOR:
        result.is_float = false;
        result.int_val = mask_int(args[0].int_val ^ args[1].int_val, width);
        return true;

    case Instruction::SHL:
        result.is_float = false;
        result.int_val = mask_int(args[0].int_val << args[1].int_val, width);
        return true;

    case Instruction::SHR:
        result.is_float = false;
        if (is_unsigned_prim(rt.as_prim())) {
            auto a = (unsigned long long)args[0].int_val;
            result.int_val = (long long)(a >> args[1].int_val);
        } else {
            result.int_val = args[0].int_val >> args[1].int_val;
        }
        return true;

    case Instruction::BIT_NOT:
        result.is_float = false;
        result.int_val = mask_int(~args[0].int_val, width);
        return true;

    case Instruction::CAST: {
        // Bitcast between same-width types
        int src_width = prim_width(inst.result_type.as_prim());
        if (src_width == 32 && is_float_prim(inst.result_type.as_prim())) {
            float f;
            int32_t i32 = (int32_t)args[0].int_val;
            memcpy(&f, &i32, sizeof(f));
            result.float_val = f;
            result.is_float = true;
        } else if (src_width == 64 && is_float_prim(inst.result_type.as_prim())) {
            double d;
            int64_t i64 = (int64_t)args[0].int_val;
            memcpy(&d, &i64, sizeof(d));
            result.float_val = d;
            result.is_float = true;
        } else if (src_width == 32 && is_int_prim(inst.result_type.as_prim())) {
            float f = (float)args[0].float_val;
            int32_t i32;
            memcpy(&i32, &f, sizeof(i32));
            result.int_val = i32;
            result.is_float = false;
        } else if (src_width == 64 && is_int_prim(inst.result_type.as_prim())) {
            double d = args[0].float_val;
            int64_t i64;
            memcpy(&i64, &d, sizeof(i64));
            result.int_val = i64;
            result.is_float = false;
        } else {
            return false;
        }
        return true;
    }

    case Instruction::TRUNC:
        result.is_float = false;
        result.int_val = mask_int(args[0].int_val, width);
        return true;

    case Instruction::ZEXT:
        result.is_float = false;
        result.int_val = args[0].int_val; // zero-extend is a no-op for positive values
        return true;

    case Instruction::SEXT:
        result.is_float = false;
        result.int_val = args[0].int_val; // sign extension is automatic in two's complement
        return true;

    case Instruction::FPTOUI:
    case Instruction::FPTOSI:
        result.is_float = false;
        result.int_val = (long long)args[0].float_val;
        return true;

    case Instruction::UITOFP:
    case Instruction::SITOFP:
        result.is_float = true;
        result.float_val = (double)args[0].int_val;
        return true;

    default:
        return false;
    }
}

// Algebraic simplifications that always produce a constant
// even when not all operands are constants (e.g., mul %x, 0 → 0).
static bool try_simplify_to_const(const Instruction& inst,
                                   const std::map<size_t, FoldedConst>& const_vals,
                                   FoldedConst& result)
{
    auto& rt = inst.result_type;
    bool res_int = rt.is_prim() && is_int_prim(rt.as_prim());
    if (!res_int) return false;

    int width = prim_width(rt.as_prim());
    int const_idx = -1;
    int var_idx = -1;
    long long const_int_val = 0;

    for (size_t i = 0; i < inst.operands.size(); i++) {
        auto it = const_vals.find(inst.operands[i]);
        if (it != const_vals.end()) {
            if (const_idx >= 0) return false; // 2+ constants → handled by constant folding
            const_idx = (int)i;
            const_int_val = it->second.int_val;
        } else if (var_idx < 0) {
            var_idx = (int)i;
        }
    }

    switch (inst.opcode) {
    case Instruction::MUL:
        if (const_idx >= 0 && const_int_val == 0) {
            result.is_float = false; result.int_val = 0; return true;
        }
        return false;
    case Instruction::BIT_AND:
        if (const_idx >= 0 && const_int_val == 0) {
            result.is_float = false; result.int_val = 0; return true;
        }
        return false;
    case Instruction::BIT_OR:
        if (const_idx >= 0 && const_int_val == mask_int(-1, width)) {
            result.is_float = false; result.int_val = mask_int(-1, width); return true;
        }
        return false;
    case Instruction::SUB:
    case Instruction::BIT_XOR:
        if (inst.operands.size() == 2 && inst.operands[0] == inst.operands[1]) {
            result.is_float = false; result.int_val = 0; return true;
        }
        return false;
    default:
        return false;
    }
}

// Algebraic simplifications: patterns like mul %x, 0 → 0, add %x, 0 → %x, etc.
// Returns true if the instruction was simplified (mutated in place or forwarded).
static bool try_algebraic_simplify(const Instruction& inst,
                                   size_t vid,
                                   const std::map<size_t, FoldedConst>& const_vals,
                                   std::map<size_t, size_t>& forward_map)
{
    auto& rt = inst.result_type;
    bool res_int = rt.is_prim() && is_int_prim(rt.as_prim());
    bool res_float = rt.is_prim() && is_float_prim(rt.as_prim());
    if (!res_int && !res_float) return false;

    int width = rt.is_prim() ? prim_width(rt.as_prim()) : 64;

    // Find constant operands (need exactly one for algebraic patterns)
    int const_idx = -1;
    int var_idx = -1;
    long long const_int_val = 0;
    double const_float_val = 0;
    bool const_is_float = false;

    for (size_t i = 0; i < inst.operands.size(); i++) {
        auto it = const_vals.find(inst.operands[i]);
        if (it != const_vals.end()) {
            if (const_idx >= 0) return false; // multiple constants — handled by constant folding
            const_idx = (int)i;
            const_int_val = it->second.int_val;
            const_float_val = it->second.float_val;
            const_is_float = it->second.is_float;
        } else {
            var_idx = (int)i;
        }
    }

    if (const_idx < 0) return false; // no constant operand
    // If both operands are constants, constant folding handles it

    switch (inst.opcode) {
    case Instruction::ADD:
        if (const_int_val == 0) { forward_map[vid] = inst.operands[var_idx]; return true; }
        return false;

    case Instruction::SUB:
        if (const_idx == 1 && const_int_val == 0) { forward_map[vid] = inst.operands[0]; return true; }
        return false;

    case Instruction::MUL:
        if (const_int_val == 0) {
            // Return constant 0
            return false; // let constant folding handle if we have 0 from a const
        }
        if (const_int_val == 1) { forward_map[vid] = inst.operands[var_idx]; return true; }
        return false;

    case Instruction::DIV:
        if (const_idx == 1 && const_int_val == 1) { forward_map[vid] = inst.operands[0]; return true; }
        return false;

    case Instruction::BIT_AND:
        if (const_int_val == 0) {
            // result is 0
            return false;
        }
        if (const_int_val == mask_int(-1, width)) { forward_map[vid] = inst.operands[var_idx]; return true; }
        return false;

    case Instruction::BIT_OR:
        if (const_int_val == 0) { forward_map[vid] = inst.operands[var_idx]; return true; }
        if (const_int_val == mask_int(-1, width)) {
            // result is all 1s
            return false;
        }
        return false;

    case Instruction::BIT_XOR:
        if (const_int_val == 0) { forward_map[vid] = inst.operands[var_idx]; return true; }
        return false;

    case Instruction::SHL:
    case Instruction::SHR:
        if (const_int_val == 0) { forward_map[vid] = inst.operands[var_idx]; return true; }
        return false;

    case Instruction::SELECT: {
        if (const_idx != 0) return false; // condition must be the constant
        if (const_int_val != 0) {
            forward_map[vid] = inst.operands[1]; // true branch
        } else {
            forward_map[vid] = inst.operands[2]; // false branch
        }
        return true;
    }

    default:
        return false;
    }
}

bool SimplifyPass::run_on_function(IRFunction& fn) {
    bool changed = false;

    // Step 1: record all existing CONST values
    std::map<size_t, FoldedConst> const_vals;
    size_t vid = fn.params.size();
    for (auto& block : fn.blocks) {
        for (auto& inst : block.instructions) {
            if (inst.opcode == Instruction::CONST) {
                FoldedConst fc;
                fc.is_float = inst.result_type.is_prim() && is_float_prim(inst.result_type.as_prim());
                fc.int_val = inst.const_val.int_val;
                fc.float_val = inst.const_val.float_val;
                const_vals[vid] = fc;
            }
            vid++;
        }
    }

    // Step 2: try to fold or simplify each instruction
    std::map<size_t, size_t> forward_map;
    vid = fn.params.size();
    for (auto& block : fn.blocks) {
        for (auto& inst : block.instructions) {
            if (inst.opcode == Instruction::CONST) {
                vid++;
                continue;
            }

            // Replace forwarded operands
            for (auto& op : inst.operands) {
                auto it = forward_map.find(op);
                if (it != forward_map.end()) op = it->second;
            }
            for (auto& [val, label] : inst.phi_incoming) {
                auto it = forward_map.find(val);
                if (it != forward_map.end()) val = it->second;
            }

            // Try constant folding
            FoldedConst result;
            if (try_fold_const(inst, const_vals, result)) {
                inst.opcode = Instruction::CONST;
                inst.operands.clear();
                if (result.is_float) {
                    inst.const_val.float_val = result.float_val;
                } else {
                    inst.const_val.int_val = result.int_val;
                }
                const_vals[vid] = result;
                changed = true;
                vid++;
                continue;
            }

            // Try simplification to constant (e.g., mul %x, 0 → 0)
            if (try_simplify_to_const(inst, const_vals, result)) {
                inst.opcode = Instruction::CONST;
                inst.operands.clear();
                if (result.is_float)
                    inst.const_val.float_val = result.float_val;
                else
                    inst.const_val.int_val = result.int_val;
                const_vals[vid] = result;
                changed = true;
                vid++;
                continue;
            }

            // Try algebraic simplification (forwarding)
            if (try_algebraic_simplify(inst, vid, const_vals, forward_map)) {
                changed = true;
                // Mark instruction as dead (will be removed by DCE)
                inst.opcode = Instruction::NOP;
                inst.operands.clear();
                // Do NOT add to const_vals — the value is forwarded
            }

            vid++;
        }
    }

    // Step 3: apply value forwarding
    if (!forward_map.empty()) {
        apply_forward_map(fn, forward_map);
        // Renumber to close gaps from NOPs
        renumber_values(fn);
    }

    return changed;
}

bool SimplifyPass::run(IRModule& mod) {
    bool changed = false;
    for (auto& fn : mod.functions) {
        if (!fn.is_defined || fn.is_generic()) continue;
        if (run_on_function(fn)) changed = true;
    }
    return changed;
}

// --- DeadCodeEliminationPass ---

bool DeadCodeEliminationPass::run_on_function(IRFunction& fn) {
    // Build use-count map and definition map
    std::map<size_t, size_t> use_counts;
    std::map<size_t, std::pair<size_t, size_t>> def_loc; // vid -> (block_idx, inst_idx)
    std::vector<Instruction::Opcode> opcodes; // vid -> opcode (for params, use CONST)

    size_t vid = fn.params.size();
    for (size_t bi = 0; bi < fn.blocks.size(); bi++) {
        auto& block = fn.blocks[bi];
        for (size_t ii = 0; ii < block.instructions.size(); ii++) {
            auto& inst = block.instructions[ii];
            def_loc[vid] = {bi, ii};
            opcodes.push_back(inst.opcode);
            for (auto op : inst.operands) use_counts[op]++;
            for (auto& [val, _] : inst.phi_incoming) use_counts[val]++;
            vid++;
        }
    }

    // Build list of vids for each instruction
    vid = fn.params.size();
    std::vector<size_t> inst_vids;
    for (size_t bi = 0; bi < fn.blocks.size(); bi++) {
        auto& block = fn.blocks[bi];
        for (size_t ii = 0; ii < block.instructions.size(); ii++) {
            inst_vids.push_back(vid);
            vid++;
        }
    }

    // Worklist: start with all non-side-effecting instructions that have 0 uses
    std::vector<size_t> worklist;
    vid = fn.params.size();
    for (size_t i = 0; i < opcodes.size(); i++) {
        size_t v = inst_vids[i];
        if (use_counts[v] == 0 && !has_side_effects(opcodes[i]))
            worklist.push_back(v);
    }

    if (worklist.empty()) return false;

    // Process worklist
    std::set<size_t> dead_set;
    while (!worklist.empty()) {
        size_t dead_vid = worklist.back();
        worklist.pop_back();
        if (dead_set.count(dead_vid)) continue;
        dead_set.insert(dead_vid);

        auto [bi, ii] = def_loc[dead_vid];
        auto& inst = fn.blocks[bi].instructions[ii];

        // Decrement use counts of operands
        for (auto op : inst.operands) {
            use_counts[op]--;
            // If this operand now has 0 uses and is not side-effecting (and is an instruction, not a param)
            if (op >= fn.params.size() && use_counts[op] == 0 && !dead_set.count(op)) {
                // Look up its opcode
                auto it = def_loc.find(op);
                if (it != def_loc.end()) {
                    auto [obi, oii] = it->second;
                    auto& oinst = fn.blocks[obi].instructions[oii];
                    if (!has_side_effects(oinst.opcode))
                        worklist.push_back(op);
                }
            }
        }
        for (auto& [val, _] : inst.phi_incoming) {
            use_counts[val]--;
            if (val >= fn.params.size() && use_counts[val] == 0 && !dead_set.count(val)) {
                auto it = def_loc.find(val);
                if (it != def_loc.end()) {
                    auto [obi, oii] = it->second;
                    auto& oinst = fn.blocks[obi].instructions[oii];
                    if (!has_side_effects(oinst.opcode))
                        worklist.push_back(val);
                }
            }
        }
    }

    // Remove dead instructions and renumber
    vid = fn.params.size();
    std::vector<size_t> old_to_new(fn.next_value_id, (size_t)-1);
    for (size_t i = 0; i < fn.params.size(); i++)
        old_to_new[i] = i;

    size_t new_vid = fn.params.size();
    for (size_t bi = 0; bi < fn.blocks.size(); bi++) {
        auto& block = fn.blocks[bi];
        std::vector<Instruction> new_insts;
        for (size_t ii = 0; ii < block.instructions.size(); ii++) {
            size_t old_vid = vid;
            vid++;
            if (dead_set.count(old_vid)) continue;
            old_to_new[old_vid] = new_vid++;
            new_insts.push_back(std::move(block.instructions[ii]));
        }
        block.instructions = std::move(new_insts);
    }

    // Apply renaming to all operands
    for (auto& block : fn.blocks) {
        for (auto& inst : block.instructions) {
            for (auto& op : inst.operands)
                op = old_to_new[op];
            for (auto& [val, label] : inst.phi_incoming)
                val = old_to_new[val];
        }
    }

    fn.next_value_id = new_vid;
    return true;
}

bool DeadCodeEliminationPass::run(IRModule& mod) {
    bool changed = false;
    for (auto& fn : mod.functions) {
        if (!fn.is_defined || fn.is_generic()) continue;
        if (run_on_function(fn)) changed = true;
    }
    return changed;
}

// --- LoadForwardingPass ---

bool LoadForwardingPass::run_on_function(IRFunction& fn) {
    bool changed = false;

    for (auto& block : fn.blocks) {
        std::map<size_t, size_t> forward_store;  // ptr_vid -> val_vid (already forwarded)
        std::map<size_t, size_t> vid_to_replacement; // load_vid -> replacement_vid
        std::set<size_t> loads_to_nop;

        size_t global_vid = fn.params.size();
        for (auto& b : fn.blocks) {
            if (&b == &block) break;
            global_vid += b.instructions.size();
        }

        // First pass: eagerly resolve operands, build forward_store and vid_to_replacement
        for (auto& inst : block.instructions) {
            // Resolve operands using existing replacements first
            // This ensures forward_store entries point to the most-forwarded value
            for (auto& op : inst.operands) {
                auto it = vid_to_replacement.find(op);
                if (it != vid_to_replacement.end()) op = it->second;
            }

            if (inst.opcode == Instruction::STORE) {
                size_t val = inst.operands[0];
                size_t ptr = inst.operands[1];
                forward_store[ptr] = val;
            } else if (inst.opcode == Instruction::LOAD) {
                size_t ptr = inst.operands[0];
                auto it = forward_store.find(ptr);
                if (it != forward_store.end()) {
                    vid_to_replacement[global_vid] = it->second;
                    loads_to_nop.insert(global_vid);
                }
            } else if (inst.opcode == Instruction::CALL) {
                forward_store.clear();
            }
            global_vid++;
        }

        if (vid_to_replacement.empty()) continue;
        changed = true;

        // Apply vid_to_replacement to remaining operand references
        global_vid = fn.params.size();
        for (auto& b : fn.blocks) {
            if (&b == &block) break;
            global_vid += b.instructions.size();
        }
        for (auto& inst : block.instructions) {
            for (auto& op : inst.operands) {
                auto it = vid_to_replacement.find(op);
                if (it != vid_to_replacement.end()) op = it->second;
            }
            for (auto& [val, label] : inst.phi_incoming) {
                auto it = vid_to_replacement.find(val);
                if (it != vid_to_replacement.end()) val = it->second;
            }
            global_vid++;
        }

        // Convert forwarded loads to NOP (DCE will clean them up)
        global_vid = fn.params.size();
        for (auto& b : fn.blocks) {
            if (&b == &block) break;
            global_vid += b.instructions.size();
        }
        for (auto& inst : block.instructions) {
            if (loads_to_nop.count(global_vid)) {
                inst.opcode = Instruction::NOP;
                inst.operands.clear();
            }
            global_vid++;
        }
    }

    return changed;
}

bool LoadForwardingPass::run(IRModule& mod) {
    bool changed = false;
    for (auto& fn : mod.functions) {
        if (!fn.is_defined || fn.is_generic()) continue;
        if (run_on_function(fn)) changed = true;
    }
    return changed;
}

// --- ControlFlowSimplifyPass ---

bool ControlFlowSimplifyPass::run_on_function(IRFunction& fn) {
    bool changed = false;

    // --- Fold constant branches ---
    for (auto& block : fn.blocks) {
        if (block.instructions.empty()) continue;
        auto& term = block.instructions.back();
        if (term.opcode != Instruction::BR_COND) continue;
        if (term.operands.empty()) continue;

        // Check if condition is a constant
        // We need the VID of the condition operand. We can look it up but we don't
        // have the VID mapping here easily. Let me compute it.
    }

    // --- Merge blocks: if A -> B (unconditional) and B has only one predecessor, merge ---
    // Compute predecessor counts
    std::map<std::string, std::set<std::string>> predecessors;
    for (auto& block : fn.blocks) {
        if (block.instructions.empty()) continue;
        auto& term = block.instructions.back();
        if (term.opcode == Instruction::BR) {
            predecessors[term.target_label].insert(block.label);
        } else if (term.opcode == Instruction::BR_COND) {
            predecessors[term.true_label].insert(block.label);
            predecessors[term.false_label].insert(block.label);
        }
    }

    std::set<std::string> blocks_to_remove;
    for (size_t i = 0; i < fn.blocks.size(); i++) {
        auto& block = fn.blocks[i];
        if (block.instructions.empty()) continue;
        auto& term = block.instructions.back();
        if (term.opcode != Instruction::BR) continue;
        std::string target = term.target_label;

        // Only merge if target has exactly one predecessor (this block)
        auto pit = predecessors.find(target);
        if (pit == predecessors.end() || pit->second.size() != 1) continue;
        if (target == block.label) continue; // self-loop

        // Find target block
        size_t target_idx = (size_t)-1;
        for (size_t j = 0; j < fn.blocks.size(); j++) {
            if (fn.blocks[j].label == target) { target_idx = j; break; }
        }
        if (target_idx == (size_t)-1) continue;

        // Merge: copy target's instructions into this block (after removing the terminator)
        auto& target_block = fn.blocks[target_idx];
        auto& target_insts = target_block.instructions;

        // Remove the trailing BR from this block
        block.instructions.pop_back();

        // Append all instructions from target block
        for (auto& inst : target_insts) {
            block.instructions.push_back(std::move(inst));
        }

        // Update predecessor references: anyone who referred to target should refer to this block
        // (but target only had one predecessor, which is this block)
        blocks_to_remove.insert(target);

        // Remap branch targets in the merged block
        // PHI nodes in the target that referenced the old entry block need updating
        // Actually, the PHI entries from this block's predecessor are still valid
        // PHI entries from other predecessors don't exist (single predecessor)

        changed = true;
    }

    // Remove merged blocks
    if (!blocks_to_remove.empty()) {
        std::vector<IRBlock> new_blocks;
        for (auto& block : fn.blocks) {
            if (blocks_to_remove.count(block.label)) continue;
            new_blocks.push_back(std::move(block));
        }
        fn.blocks = std::move(new_blocks);
        changed = true;
    }

    return changed;
}

bool ControlFlowSimplifyPass::run(IRModule& mod) {
    bool changed = false;
    for (auto& fn : mod.functions) {
        if (!fn.is_defined || fn.is_generic()) continue;
        if (run_on_function(fn)) changed = true;
    }
    return changed;
}
