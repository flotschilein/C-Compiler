#include <deque>
#include <stdio.h>
#include <iostream>
#include <vector>
#include "preprocessor.h"

int main(int ac, char* av[]) {
    if (ac == 1) {
        std::cout << "usage: compiler <file>" << std::endl;
        return 1;
    }
    
    preprocessor pp;
    for (int i = 1; i < ac; i++) {
        try {
            std::cout << "preprocessing " << av[i] << std::endl;
            std::vector<Token> tokens = pp.preprocess(av[i]);
            for (const auto& token : tokens) {
                if (token.type != TokenType::END_OF_FILE) {
                    std::cout << token.value;
                }
            }
            std::cout << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    return 0;
}