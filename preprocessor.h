#ifndef C_COMPILER_PREPROCESSOR_H
#define C_COMPILER_PREPROCESSOR_H

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <map>
#include <memory>
#include <set>
#include <ctime>

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

    struct Macro {
        bool is_function_like;
        std::vector<std::string> params;
        bool is_variadic;
        std::vector<Token> replacement_list;
    };

    std::vector<Token> run_phases_4(std::vector<Token>& tokens);
    void handle_directive(PreprocessorState& state, std::vector<Token>& output);
    void handle_define(PreprocessorState& state);
    void handle_include(PreprocessorState& state, std::vector<Token>& output);
    void handle_include_next(PreprocessorState& state, std::vector<Token>& output);
    void handle_if(PreprocessorState& state, std::vector<Token>& output);
    void handle_ifdef(PreprocessorState& state, std::vector<Token>& output, bool negate);
    void handle_undef(PreprocessorState& state);
    void handle_embed(PreprocessorState& state, std::vector<Token>& output);
    void handle_error(PreprocessorState& state);
    void handle_warning(PreprocessorState& state);
    void handle_line(PreprocessorState& state);
    void handle_pragma(PreprocessorState& state);
    bool handle_elifdef(PreprocessorState& state, std::vector<Token>& output, bool negate);
    bool handle_elif(PreprocessorState& state, std::vector<Token>& output);
    
    bool evaluate_condition(const std::vector<Token>& tokens);
    long long evaluate_expression(const std::vector<Token>& tokens);
    void skip_failed_branch(PreprocessorState& state, std::vector<Token>& output, bool skip_all = false);
    
    bool is_directive(const std::vector<Token>& tokens, size_t pos);
    Token peek(const PreprocessorState& state, int offset = 0);
    Token consume(PreprocessorState& state);
    void skip_whitespace(PreprocessorState& state);

    bool has_include(const std::string& filename);
    bool has_include_next(const std::string& filename);
    
    long long lookup_attribute(const std::string& name);

    std::vector<Token> expand_tokens(std::vector<Token> tokens, std::set<std::string> expanding);
    std::vector<Token> expand_function_like(Macro& m, std::vector<std::vector<Token>>& args,
                                              std::set<std::string>& expanding);

    std::vector<Token> tokenize(const std::string& source, const std::string& filename);
    std::string read_file(const std::string& filename);
    std::string phase1_2(const std::string& source);
    
    std::map<std::string, Macro> macros;
    std::vector<std::string> include_paths;
    std::set<std::string> once_files;
    int include_next_index;
    int counter_value = 0;
};

#endif
