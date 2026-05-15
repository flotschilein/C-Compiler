#ifndef C_COMPILER_IR_VERIFIER_H
#define C_COMPILER_IR_VERIFIER_H

#include "ir.h"
#include <string>
#include <vector>

struct IRVerifier {
    struct Issue {
        enum Severity { ERROR, WARNING };
        Severity severity;
        std::string message;
        std::string function;
        std::string block;
    };

    std::vector<Issue> issues;

    // Run all checks on a module
    void verify(const IRModule& mod);

    // Individual checks
    void check_function(const IRFunction& fn);
    void check_no_dangling_type_params(const IRType& type);
    void check_ssa(const IRFunction& fn);
    void check_terminators(const IRFunction& fn);
    void check_control_flow(const IRFunction& fn);
    void check_instruction_types(const IRFunction& fn);
    void check_struct_defs(const IRModule& mod);

    bool has_errors() const;
    void print_report() const;
    void clear();

private:
    void error(const std::string& msg, const std::string& fn, const std::string& block = "");
    void warning(const std::string& msg, const std::string& fn, const std::string& block = "");
};

#endif
