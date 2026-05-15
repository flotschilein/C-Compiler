#ifndef C_COMPILER_IR_INSTANTIATOR_H
#define C_COMPILER_IR_INSTANTIATOR_H

#include "ir.h"
#include <map>
#include <vector>

class IRInstantiator {
    std::map<size_t, IRType> substitutions; // TypeParam index -> concrete type

    IRType subst_type(const IRType& type);
    Instruction subst_inst(const Instruction& inst, size_t value_offset);
    void subst_block(const IRBlock& src, IRBlock& dst, size_t& value_offset);

public:
    IRFunction instantiate(const IRFunction& generic_fn,
                           const std::vector<IRType>& type_args);
};

class IRCache {
    struct Key {
        std::string fn_name;
        std::vector<IRType> type_args;
        bool operator<(const Key& o) const;
    };
    std::map<Key, IRFunction> cache;

public:
    IRFunction* get_or_instantiate(IRInstantiator& inst,
                                   IRModule& module,
                                   const std::string& fn_name,
                                   const std::vector<IRType>& type_args);
};

#endif
