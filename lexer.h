#ifndef C_COMPILER_LEXER_H
#define C_COMPILER_LEXER_H

#include <string>
#include <vector>
#include "preprocessor.h"

class Lexer {
public:
    Lexer() = default;

    std::vector<Token> tokenize_file(const std::string& filename);
    std::vector<Token> tokenize_string(const std::string& source, const std::string& filename);

    static std::string read_file(const std::string& filename);
    static std::string phase1_2(const std::string& source);

private:
    std::vector<Token> tokenize(const std::string& source, const std::string& filename);
};

#endif
