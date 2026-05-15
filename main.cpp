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

int main(int ac, char* av[]) {
    if (ac == 1) {
        std::cout << "usage: compiler <file>" << std::endl;
        return 1;
    }

    preprocessor pp;
    for (int i = 1; i < ac; i++) {
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

            std::cout << "--- Done ---\n";
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    return 0;
}
