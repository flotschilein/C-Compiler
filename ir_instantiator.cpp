#include "ir_instantiator.h"
#include <cassert>
#include <sstream>

// --- Substitution Helpers ---

IRType IRInstantiator::subst_type(const IRType& type) {
    if (type.is_param()) {
        auto& p = type.as_param();
        auto it = substitutions.find(p.index);
        if (it != substitutions.end())
            return it->second.copy();
        return type.copy();
    }
    if (type.is_ptr()) {
        return IRTypeFactory::ptr(subst_type(*type.as_ptr().pointee));
    }
    if (type.is_arr()) {
        auto& a = type.as_arr();
        return IRTypeFactory::arr(subst_type(*a.element), a.size);
    }
    if (type.is_fn()) {
        auto& f = type.as_fn();
        std::vector<IRType> params;
        for (auto& p : f.params)
            params.push_back(subst_type(p));
        return IRTypeFactory::fn(subst_type(*f.return_type), params, f.variadic);
    }
    if (type.is_struct()) {
        auto& s = type.as_struct();
        std::vector<IRType> args;
        for (auto& a : s.args)
            args.push_back(subst_type(a));
        return IRTypeFactory::strukt(s.name, args);
    }
    return type.copy();
}

Instruction IRInstantiator::subst_inst(const Instruction& inst, size_t value_offset) {
    Instruction result;
    result.opcode = inst.opcode;
    result.result_type = subst_type(inst.result_type);
    result.extra_type = inst.extra_type.is_void() ? inst.extra_type : subst_type(inst.extra_type);
    result.callee_name = inst.callee_name;
    result.target_label = inst.target_label;
    result.true_label = inst.true_label;
    result.false_label = inst.false_label;
    result.gep_index = inst.gep_index;
    result.const_val = inst.const_val;
    result.extract_index = inst.extract_index;

    for (auto op : inst.operands)
        result.operands.push_back(op + value_offset);

    for (auto& [val, label] : inst.phi_incoming)
        result.phi_incoming.push_back({val + value_offset, label});

    return result;
}

void IRInstantiator::subst_block(const IRBlock& src, IRBlock& dst, size_t& value_offset) {
    dst.label = src.label;
    for (auto& inst : src.instructions)
        dst.instructions.push_back(subst_inst(inst, value_offset));
}

// --- Instantiate ---

IRFunction IRInstantiator::instantiate(const IRFunction& generic_fn,
                                        const std::vector<IRType>& type_args) {
    substitutions.clear();

    for (size_t i = 0; i < generic_fn.type_params.size() && i < type_args.size(); i++)
        substitutions[generic_fn.type_params[i].index] = type_args[i].copy();

    IRFunction result;
    result.name = generic_fn.name;
    result.return_type = subst_type(generic_fn.return_type);
    result.is_defined = generic_fn.is_defined;
    result.next_value_id = generic_fn.next_value_id;

    for (auto& [type, name] : generic_fn.params)
        result.params.push_back({subst_type(type), name});

    size_t value_offset = 0;
    for (auto& block : generic_fn.blocks) {
        IRBlock new_block;
        subst_block(block, new_block, value_offset);
        result.blocks.push_back(std::move(new_block));
    }

    return result;
}

// --- Cache ---

bool IRCache::Key::operator<(const Key& o) const {
    if (fn_name != o.fn_name) return fn_name < o.fn_name;
    if (type_args.size() != o.type_args.size()) return type_args.size() < o.type_args.size();
    for (size_t i = 0; i < type_args.size(); i++) {
        if (type_args[i] != o.type_args[i])
            return type_args[i].is_param() < o.type_args[i].is_param();
    }
    return false;
}

IRFunction* IRCache::get_or_instantiate(IRInstantiator& inst,
                                         IRModule& module,
                                         const std::string& fn_name,
                                         const std::vector<IRType>& type_args) {
    Key key{fn_name, type_args};
    auto it = cache.find(key);
    if (it != cache.end()) {
        // Find the function in the module by name
        for (auto& fn : module.functions)
            if (fn.name == it->second.name)
                return &fn;
        return nullptr;
    }

    IRFunction* generic_fn = module.find_function(fn_name);
    if (!generic_fn || !generic_fn->is_generic())
        return nullptr;

    IRFunction concrete = inst.instantiate(*generic_fn, type_args);

    // Create a unique name for the instantiated function
    std::ostringstream ss;
    ss << fn_name << "$" << cache.size();
    concrete.name = ss.str();

    cache[key] = concrete; // store a copy

    module.functions.push_back(std::move(concrete));
    return &module.functions.back();
}
