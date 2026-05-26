#ifndef C_COMPILER_IR_PASSES_H
#define C_COMPILER_IR_PASSES_H

#include "ir.h"
#include <memory>
#include <vector>

class IRPass {
public:
    virtual ~IRPass() = default;
    virtual bool run(IRModule& mod) = 0;
    virtual const char* name() const = 0;
};

class PassManager {
    std::vector<std::unique_ptr<IRPass>> passes;
public:
    void add_pass(std::unique_ptr<IRPass> pass);
    bool run(IRModule& mod);
};

class SimplifyPass : public IRPass {
    bool run_on_function(IRFunction& fn);
public:
    bool run(IRModule& mod) override;
    const char* name() const override { return "simplify"; }
};

class DeadCodeEliminationPass : public IRPass {
    bool run_on_function(IRFunction& fn);
public:
    bool run(IRModule& mod) override;
    const char* name() const override { return "dce"; }
};

class LoadForwardingPass : public IRPass {
    bool run_on_function(IRFunction& fn);
public:
    bool run(IRModule& mod) override;
    const char* name() const override { return "load-forwarding"; }
};

class DeadStoreEliminationPass : public IRPass {
    bool run_on_function(IRFunction& fn);
public:
    bool run(IRModule& mod) override;
    const char* name() const override { return "dead-store"; }
};

class ControlFlowSimplifyPass : public IRPass {
    bool run_on_function(IRFunction& fn);
public:
    bool run(IRModule& mod) override;
    const char* name() const override { return "cf-simplify"; }
};

#endif
