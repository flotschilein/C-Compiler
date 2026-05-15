#ifndef C_COMPILER_IR_PRINTER_H
#define C_COMPILER_IR_PRINTER_H

#include "ir.h"
#include <string>

struct IRPrinter {
    static std::string type_to_string(const IRType& type);
    static std::string prim_to_string(IRPrimitive p);
    static std::string opcode_to_string(Instruction::Opcode op);
    static std::string value_ref(size_t id);
    static std::string print_instruction(const Instruction& inst);
    static std::string print_block(const IRBlock& block);
    static std::string print_function(const IRFunction& fn);
    static std::string print_module(const IRModule& mod);
};

#endif
