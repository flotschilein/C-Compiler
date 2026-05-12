#ifndef C_COMPILER_PREPROCESSOR_H
#define C_COMPILER_PREPROCESSOR_H

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <map>
#include <memory>

enum class TokenType {
    IDENTIFIER,
    NUMBER,
    STRING_LITERAL,
    CHARACTER_LITERAL,
    PUNCTUATOR,
    HEADER_NAME,
    PREPROCESSING_OP,
    NEWLINE,
    WHITESPACE,
    END_OF_FILE
};

struct Token {
    TokenType type;
    std::string value;
    std::string filename;
    int line;
    int column;
    bool has_whitespace_before = false;
};

class preprocessor {
public:
    preprocessor();
    std::vector<Token> preprocess(const std::string& filename);

private:
    struct PreprocessorState {
        std::vector<Token> tokens;
        size_t pos = 0;
        std::string filename;
    };

    std::vector<Token> run_phases_4(std::vector<Token>& tokens);
    void handle_directive(PreprocessorState& state, std::vector<Token>& output);
    void handle_define(PreprocessorState& state);
    void handle_include(PreprocessorState& state, std::vector<Token>& output);
    void handle_if(PreprocessorState& state, std::vector<Token>& output);
    void handle_ifdef(PreprocessorState& state, std::vector<Token>& output, bool negate);
    void handle_undef(PreprocessorState& state);
    void handle_embed(PreprocessorState& state, std::vector<Token>& output);
    
    bool evaluate_condition(const std::vector<Token>& tokens);
    void skip_failed_branch(PreprocessorState& state);
    
    bool is_directive(const std::vector<Token>& tokens, size_t pos);
    Token peek(const PreprocessorState& state, int offset = 0);
    Token consume(PreprocessorState& state);
    void skip_whitespace(PreprocessorState& state);

    std::vector<Token> tokenize(const std::string& source, const std::string& filename);
    std::string read_file(const std::string& filename);
    
    // Translation phases
    std::string phase1_2(const std::string& source); // Line splicing and character mapping
    
    struct Macro {
        bool is_function_like;
        std::vector<std::string> params;
        bool is_variadic;
        std::vector<Token> replacement_list;
    };
    
    std::map<std::string, Macro> macros;
    std::vector<std::string> include_paths;
};

#endif //C_COMPILER_PREPROCESSOR_H