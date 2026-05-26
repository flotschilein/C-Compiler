#include <iostream>
#include <vector>
#include "preprocessor.h"
#include "lexer.h"
#include "token.h"
#include "parser.h"
#include "semantic.h"
#include "ir.h"
#include "ir_builder.h"
#include "ir_printer.h"
#include "ir_instantiator.h"
#include "ir_verifier.h"
#include "ir_passes.h"

int main(int ac, char* av[]) {
    if (ac == 1) {
        std::cout << "usage: compiler [-O1] <file>" << std::endl;
        return 1;
    }

    int opt_level = 0;
    int file_start = 1;

    if (ac > 1 && std::string(av[1]) == "-O1") {
        opt_level = 1;
        file_start = 2;
    }

    if (file_start >= ac) {
        std::cout << "usage: compiler [-O1] <file>" << std::endl;
        return 1;
    }

    preprocessor pp;
    for (int i = file_start; i < ac; i++) {
        try {
            std::vector<Token> pp_tokens = pp.preprocess(av[i]);

            // Token refinement
            std::set<std::string> typedef_names;
            auto refined = refine_tokens(pp_tokens, typedef_names);

            // Parsing
            Parser parser(std::move(refined));
            auto ast = parser.parse();

            // AST dump before semantic analysis
            std::cout << "--- AST ---\n";
            ast->dump();

            // Semantic analysis
            std::cout << "--- Semantic Analysis ---\n";
            SemanticAnalyzer analyzer;
            analyzer.analyze(*ast);

            // IR Generation
            std::cout << "--- GIR ---\n";
            IRModule mod;
            IRBuilder builder(mod);
            builder.lower_translation_unit(*ast);
            std::cout << IRPrinter::print_module(mod);

            // IR Verification
            std::cout << "--- IR Verify ---\n";
            IRVerifier verifier;
            verifier.verify(mod);
            if (verifier.has_errors()) {
                verifier.print_report();
                std::cout << "IR verification FAILED\n";
            } else {
                std::cout << "  all checks passed\n";
            }

            // Optimizations
            if (opt_level >= 1) {
                std::cout << "--- Optimizing (O1) ---\n";
                PassManager pm;
                pm.add_pass(std::make_unique<SimplifyPass>());
                pm.add_pass(std::make_unique<DeadCodeEliminationPass>());
                pm.add_pass(std::make_unique<LoadForwardingPass>());
                pm.add_pass(std::make_unique<DeadStoreEliminationPass>());
                pm.add_pass(std::make_unique<ControlFlowSimplifyPass>());
                pm.add_pass(std::make_unique<SimplifyPass>());
                pm.add_pass(std::make_unique<DeadCodeEliminationPass>());
                bool changed = pm.run(mod);

                // Re-verify after optimization
                IRVerifier v2;
                v2.verify(mod);
                if (v2.has_errors()) {
                    std::cout << "IR verification after optimization FAILED\n";
                    v2.print_report();
                } else {
                    std::cout << "  verification passed\n";
                }

                // Print optimized IR
                std::cout << "--- Optimized IR ---\n";
                std::cout << IRPrinter::print_module(mod);
                if (changed)
                    std::cout << "  (optimizations applied)\n";
                else
                    std::cout << "  (no changes)\n";
            }

            std::cout << "--- Done ---\n";
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    return 0;
}
