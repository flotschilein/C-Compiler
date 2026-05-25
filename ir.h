#ifndef C_COMPILER_IR_H
#define C_COMPILER_IR_H

#include "ir_type.h"
#include <string>
#include <vector>
#include <cstdint>

// --- Instruction ---

struct Instruction {
    enum Opcode : uint8_t {
        // Arithmetic
        ADD, SUB, MUL, DIV, REM, NEG,
        // Comparison
        EQ, NE, LT, LE, GT, GE,
        // Logical (short-circuit expanded via branches in the builder)
        LOGIC_AND, LOGIC_OR, LOGIC_NOT,
        // Bitwise
        BIT_AND, BIT_OR, BIT_XOR, SHL, SHR, BIT_NOT,
        // Memory
        ALLOCA, LOAD, STORE, GEP,
        // Conversions
        CAST, TRUNC, ZEXT, SEXT, FPTOUI, FPTOSI, UITOFP, SITOFP,
        // Call
        CALL,
        // Control
        BR, BR_COND, RET, PHI,
        // Misc
        SELECT, EXTRACT,
        // Constants
        CONST
    };

    Opcode opcode;
    IRType result_type;
    std::vector<size_t> operands;

    // ALLOCA / CAST: the target or allocated type
    IRType extra_type;
    // CALL
    std::string callee_name;
    // BR (uncond)
    std::string target_label;
    // BR_COND
    std::string true_label;
    std::string false_label;
    // PHI: (value_id, label) pairs
    std::vector<std::pair<size_t, std::string>> phi_incoming;
    // GEP
    size_t gep_index = 0;
    // CONST
    union {
        long long int_val = 0;
        double float_val;
    } const_val;
    // EXTRACT (variadic pack expansion index)
    size_t extract_index = 0;
};

// --- Basic Block ---

struct IRBlock {
    std::string label;
    std::vector<Instruction> instructions;
};

// --- Function ---

struct IRFunction {
    std::string name;
    IRType return_type;
    std::vector<std::pair<IRType, std::string>> params;
    std::vector<IRType::Param> type_params;
    std::vector<IRBlock> blocks;
    size_t next_value_id = 0;
    bool is_defined = false;

    bool is_generic() const { return !type_params.empty(); }
};

// --- Struct Definition ---

struct IRStructDef {
    std::string name;
    std::vector<IRType::Param> type_params;
    std::vector<std::pair<IRType, std::string>> fields;
    bool is_defined = false;
    bool is_generic() const { return !type_params.empty(); }
};

// --- Global Variable ---

struct IRGlobal {
    std::string name;
    IRType type;
    bool is_defined = false;     // has an initializer
    bool is_extern = false;
    bool is_static = false;
    bool is_thread_local = false;
    // For simple constant initializers: the value
    // (0 means zero-init; for struct/array init we track separately)
    long long init_val = 0;
    bool has_init = false;
};

// --- Module ---

struct IRModule {
    std::vector<IRFunction> functions;
    std::vector<IRStructDef> structs;
    std::vector<std::string> global_strings;
    std::vector<IRGlobal> globals;

    IRFunction* find_function(const std::string& name);
    IRStructDef* find_struct(const std::string& name);
    IRStructDef* add_struct(std::string name);
    IRGlobal* find_global(const std::string& name);
};

#endif
