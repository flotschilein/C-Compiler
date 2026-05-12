#include "preprocessor.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <filesystem>

preprocessor::preprocessor() {
    include_paths.push_back(".");
    
    // Add C++26 predefined macros
    macros["__STDC_HOSTED__"] = {false, {}, false, {{TokenType::NUMBER, "1", "", 0, 0}}};
    macros["__cplusplus"] = {false, {}, false, {{TokenType::NUMBER, "202601L", "", 0, 0}}}; // Placeholder for C++26
    
    // __has_cpp_attribute support
    macros["__has_cpp_attribute"] = {true, {"attr"}, false, {}}; // Special handling needed
}

std::string preprocessor::read_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string preprocessor::phase1_2(const std::string& source) {
    std::string result;
    result.reserve(source.size());
    
    for (size_t i = 0; i < source.size(); ++i) {
        // Phase 2: Line splicing (backslash-newline)
        if (source[i] == '\\' && i + 1 < source.size()) {
            if (source[i+1] == '\n') {
                i++; // Skip backslash and newline
                continue;
            } else if (source[i+1] == '\r' && i + 2 < source.size() && source[i+2] == '\n') {
                i += 2; // Skip backslash and \r\n
                continue;
            }
        }
        result += source[i];
    }
    
    // C++ requires the file to end with a newline if it's not empty
    if (!result.empty() && result.back() != '\n') {
        result += '\n';
    }
    
    return result;
}

std::vector<Token> preprocessor::tokenize(const std::string& source, const std::string& filename) {
    std::vector<Token> tokens;
    int line = 1;
    int col = 1;
    
    for (size_t i = 0; i < source.size(); ) {
        char c = source[i];
        
        Token token;
        token.filename = filename;
        token.line = line;
        token.column = col;
        
        if (std::isspace(static_cast<unsigned char>(c))) {
            bool has_ws = !tokens.empty() && (tokens.back().type == TokenType::WHITESPACE || tokens.back().type == TokenType::NEWLINE);
            token.type = TokenType::WHITESPACE;
            token.value = c;
            token.has_whitespace_before = true;
            if (c == '\n') {
                token.type = TokenType::NEWLINE;
                line++;
                col = 1;
            } else {
                col++;
            }
            tokens.push_back(token);
            i++;
            continue;
        }
        
        // Comments (Phase 3: replace each comment with one space)
        if (c == '/' && i + 1 < source.size()) {
            if (source[i+1] == '/') {
                // Line comment
                i += 2;
                while (i < source.size() && source[i] != '\n') i++;
                token.type = TokenType::WHITESPACE;
                token.value = " ";
                token.has_whitespace_before = true;
                tokens.push_back(token);
                continue;
            } else if (source[i+1] == '*') {
                // Block comment
                i += 2;
                while (i + 1 < source.size() && !(source[i] == '*' && source[i+1] == '/')) {
                    if (source[i] == '\n') line++;
                    i++;
                }
                i += 2;
                token.type = TokenType::WHITESPACE;
                token.value = " ";
                token.has_whitespace_before = true;
                tokens.push_back(token);
                continue;
            }
        }

        // String literals
        if (c == '"') {
            token.type = TokenType::STRING_LITERAL;
            token.has_whitespace_before = !tokens.empty() && (tokens.back().type == TokenType::WHITESPACE || tokens.back().type == TokenType::NEWLINE);
            token.value += source[i++];
            while (i < source.size() && source[i] != '"') {
                if (source[i] == '\\' && i + 1 < source.size()) {
                    token.value += source[i++];
                }
                token.value += source[i++];
            }
            if (i < source.size()) token.value += source[i++];
            tokens.push_back(token);
            col += token.value.size();
            continue;
        }

        // Identifiers
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            token.type = TokenType::IDENTIFIER;
            token.has_whitespace_before = !tokens.empty() && (tokens.back().type == TokenType::WHITESPACE || tokens.back().type == TokenType::NEWLINE);
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) {
                token.value += source[i++];
                col++;
            }
            tokens.push_back(token);
            continue;
        }

        // Simple single-character punctuators for now
        token.type = TokenType::PUNCTUATOR;
        token.has_whitespace_before = !tokens.empty() && (tokens.back().type == TokenType::WHITESPACE || tokens.back().type == TokenType::NEWLINE);
        token.value = c;
        tokens.push_back(token);
        i++;
        col++;
    }
    
    tokens.push_back({TokenType::END_OF_FILE, "", filename, line, col});
    return tokens;
}

std::vector<Token> preprocessor::preprocess(const std::string& filename) {
    std::string source = read_file(filename);
    std::string processed_source = phase1_2(source);
    std::vector<Token> tokens = tokenize(processed_source, filename);
    
    return run_phases_4(tokens);
}

Token preprocessor::peek(const PreprocessorState& state, int offset) {
    if (state.pos + offset >= state.tokens.size()) {
        return {TokenType::END_OF_FILE, "", state.filename, 0, 0};
    }
    return state.tokens[state.pos + offset];
}

Token preprocessor::consume(PreprocessorState& state) {
    Token t = peek(state);
    if (t.type != TokenType::END_OF_FILE) {
        state.pos++;
    }
    return t;
}

void preprocessor::skip_whitespace(PreprocessorState& state) {
    while (peek(state).type == TokenType::WHITESPACE) {
        consume(state);
    }
}

bool preprocessor::is_directive(const std::vector<Token>& tokens, size_t pos) {
    // A directive starts with # at the beginning of a line (or after whitespace on a line)
    if (pos >= tokens.size()) return false;
    if (tokens[pos].value != "#") return false;
    
    // Check if it's the start of the line
    if (pos == 0) return true;
    for (int i = static_cast<int>(pos) - 1; i >= 0; --i) {
        if (tokens[i].type == TokenType::NEWLINE) return true;
        if (tokens[i].type != TokenType::WHITESPACE) return false;
    }
    return true;
}

void preprocessor::handle_include(PreprocessorState& state, std::vector<Token>& output) {
    consume(state); // #
    skip_whitespace(state);
    consume(state); // include
    skip_whitespace(state);
    
    Token t = consume(state);
    std::string filename;
    
    if (t.value == "<") {
        while (peek(state).value != ">" && peek(state).type != TokenType::END_OF_FILE) {
            filename += consume(state).value;
        }
        consume(state); // >
    } else if (t.type == TokenType::STRING_LITERAL) {
        filename = t.value.substr(1, t.value.size() - 2);
    } else {
        throw std::runtime_error("Invalid #include directive");
    }
    
    // Search for file
    std::string found_path;
    for (const auto& path : include_paths) {
        std::string p = path + "\\" + filename;
        std::ifstream f(p);
        if (f.good()) {
            found_path = p;
            break;
        }
    }
    
    if (found_path.empty()) {
        throw std::runtime_error("File not found: " + filename);
    }
    
    std::vector<Token> included_tokens = preprocess(found_path);
    
    for (const auto& it : included_tokens) {
        if (it.type != TokenType::END_OF_FILE) {
            output.push_back(it);
        }
    }
}

void preprocessor::handle_directive(PreprocessorState& state, std::vector<Token>& output) {
    size_t hash_pos = state.pos;
    size_t next_pos = hash_pos + 1;
    while (next_pos < state.tokens.size() && state.tokens[next_pos].type == TokenType::WHITESPACE) next_pos++;
    
    if (next_pos < state.tokens.size()) {
        std::string dir = state.tokens[next_pos].value;
        if (dir == "define") {
            handle_define(state);
        } else if (dir == "include") {
            handle_include(state, output);
        } else if (dir == "undef") {
            handle_undef(state);
        } else if (dir == "embed") {
            handle_embed(state, output);
        } else if (dir == "ifdef") {
            handle_ifdef(state, output, false);
        } else if (dir == "ifndef") {
            handle_ifdef(state, output, true);
        } else if (dir == "if") {
            handle_if(state, output);
        } else if (dir == "else" || dir == "elif" || dir == "endif") {
            // These should be handled by the branch skipping logic, if they appear here they are unmatched
            consume(state);
        } else {
            output.push_back(consume(state));
        }
    } else {
        output.push_back(consume(state));
    }
    
    // Ensure we consume until the end of the line
    while (state.pos < state.tokens.size() && peek(state).type != TokenType::NEWLINE) {
        consume(state);
    }
    if (peek(state).type == TokenType::NEWLINE) {
        consume(state);
    }
}

void preprocessor::handle_undef(PreprocessorState& state) {
    consume(state); // #
    skip_whitespace(state);
    consume(state); // undef
    skip_whitespace(state);
    Token name = consume(state);
    if (name.type == TokenType::IDENTIFIER) {
        macros.erase(name.value);
    }
}

void preprocessor::handle_embed(PreprocessorState& state, std::vector<Token>& output) {
    consume(state); // #
    skip_whitespace(state);
    consume(state); // embed
    skip_whitespace(state);
    
    Token header = consume(state);
    std::string filename;
    if (header.type == TokenType::STRING_LITERAL || header.type == TokenType::HEADER_NAME) {
        filename = header.value.substr(1, header.value.size() - 2);
    } else {
        throw std::runtime_error("Expected header name or string literal after #embed");
    }
    
    std::string path;
    for (const auto& p : include_paths) {
        std::filesystem::path full_path = std::filesystem::path(p) / filename;
        if (std::filesystem::exists(full_path)) {
            path = full_path.string();
            break;
        }
    }
    
    if (path.empty()) {
        throw std::runtime_error("Could not find embed file: " + filename);
    }
    
    std::ifstream file(path, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    
    for (size_t i = 0; i < data.size(); ++i) {
        output.push_back({TokenType::NUMBER, std::to_string((int)data[i]), state.filename, header.line, header.column});
        if (i + 1 < data.size()) {
            output.push_back({TokenType::PUNCTUATOR, ",", state.filename, header.line, header.column});
        }
    }
}

void preprocessor::handle_ifdef(PreprocessorState& state, std::vector<Token>& output, bool negate) {
    consume(state); // #
    skip_whitespace(state);
    consume(state); // ifdef/ifndef
    skip_whitespace(state);
    
    Token name = consume(state);
    bool exists = macros.count(name.value) > 0;
    bool condition = negate ? !exists : exists;
    
    if (!condition) {
        skip_failed_branch(state);
    }
}

void preprocessor::handle_if(PreprocessorState& state, std::vector<Token>& output) {
    consume(state); // #
    skip_whitespace(state);
    consume(state); // if
    
    std::vector<Token> condition_tokens;
    while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
        condition_tokens.push_back(consume(state));
    }
    
    if (!evaluate_condition(condition_tokens)) {
        skip_failed_branch(state);
    }
}

bool preprocessor::evaluate_condition(const std::vector<Token>& tokens) {
    // Better evaluator: handle defined(X)
    if (tokens.empty()) return false;
    
    std::vector<Token> processed;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].value == "defined") {
            i++;
            while (i < tokens.size() && tokens[i].type == TokenType::WHITESPACE) i++;
            bool has_paren = false;
            if (i < tokens.size() && tokens[i].value == "(") {
                has_paren = true;
                i++;
                while (i < tokens.size() && tokens[i].type == TokenType::WHITESPACE) i++;
            }
            if (i < tokens.size() && tokens[i].type == TokenType::IDENTIFIER) {
                bool exists = false;
                if (tokens[i].value == "__has_include") {
                    // Very basic __has_include(X)
                    i++;
                    while (i < tokens.size() && tokens[i].value != "(") i++;
                    if (i < tokens.size() && tokens[i].value == "(") {
                        i++;
                        std::string inc;
                        while (i < tokens.size() && tokens[i].value != ")") {
                            if (tokens[i].type == TokenType::STRING_LITERAL) {
                                inc = tokens[i].value.substr(1, tokens[i].value.size() - 2);
                            }
                            i++;
                        }
                        if (!inc.empty()) {
                            for (const auto& path : include_paths) {
                                std::string p = path + "\\" + inc;
                                std::ifstream f(p);
                                if (f.good()) {
                                    exists = true;
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    exists = macros.count(tokens[i].value) > 0;
                }
                processed.push_back({TokenType::NUMBER, exists ? "1" : "0", "", 0, 0});
                i++;
            }
            if (has_paren) {
                while (i < tokens.size() && tokens[i].value != ")") i++;
            }
        } else if (tokens[i].type == TokenType::IDENTIFIER) {
            // Unbound identifiers in #if are 0
            processed.push_back({TokenType::NUMBER, "0", "", 0, 0});
        } else if (tokens[i].type != TokenType::WHITESPACE) {
            processed.push_back(tokens[i]);
        }
    }
    
    // For now, if anything in processed is "1" it's true
    for (const auto& t : processed) {
        if (t.value == "1") return true;
    }
    return false;
}

void preprocessor::skip_failed_branch(PreprocessorState& state) {
    int depth = 1;
    while (depth > 0 && state.pos < state.tokens.size()) {
        if (is_directive(state.tokens, state.pos)) {
            size_t hash_pos = state.pos;
            size_t next_pos = hash_pos + 1;
            while (next_pos < state.tokens.size() && state.tokens[next_pos].type == TokenType::WHITESPACE) next_pos++;
            
            if (next_pos < state.tokens.size()) {
                std::string dir = state.tokens[next_pos].value;
                if (dir == "if" || dir == "ifdef" || dir == "ifndef") {
                    depth++;
                } else if (dir == "endif") {
                    depth--;
                } else if (dir == "else" || dir == "elif") {
                    if (depth == 1) {
                        // We found an #else or #elif at our level
                        // If it's #else, we might want to start processing, 
                        // but handle_if/ifdef only skip IF the condition failed.
                        // This logic needs more care for #elif
                        if (dir == "else") {
                            state.pos = next_pos;
                            consume(state); // consume 'else'
                            return; 
                        }
                        // For #elif we'd need to re-evaluate...
                    }
                }
            }
        }
        consume(state);
    }
}

void preprocessor::handle_define(PreprocessorState& state) {
    consume(state); // #
    skip_whitespace(state);
    consume(state); // define
    skip_whitespace(state);
    
    Token name = consume(state);
    if (name.type != TokenType::IDENTIFIER) {
        throw std::runtime_error("Macro name must be an identifier");
    }
    
    Macro m;
    m.is_function_like = false;
    m.is_variadic = false;
    
    // Function-like macro: #define name(params)
    if (peek(state).value == "(" && !peek(state).has_whitespace_before) {
        m.is_function_like = true;
        consume(state); // (
        
        while (peek(state).value != ")" && peek(state).type != TokenType::END_OF_FILE) {
            Token p = consume(state);
            if (p.value == "...") {
                m.is_variadic = true;
                m.params.push_back("__VA_ARGS__");
            } else if (p.type == TokenType::IDENTIFIER) {
                if (peek(state).value == "...") {
                    m.is_variadic = true;
                    consume(state);
                    m.params.push_back(p.value);
                } else {
                    m.params.push_back(p.value);
                }
            }
            if (peek(state).value == ",") consume(state);
        }
        consume(state); // )
    }
    
    // Replacement list
    while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
        m.replacement_list.push_back(consume(state));
    }
    
    macros[name.value] = m;
}

std::vector<Token> preprocessor::run_phases_4(std::vector<Token>& tokens) {
    PreprocessorState state;
    state.tokens = tokens;
    state.filename = tokens.empty() ? "" : tokens[0].filename;
    
    std::vector<Token> output;
    
    while (state.pos < state.tokens.size()) {
        if (is_directive(state.tokens, state.pos)) {
            handle_directive(state, output);
        } else {
            Token t = consume(state);
            
            // Phase 6: String literal concatenation
            if (t.type == TokenType::STRING_LITERAL) {
                while (true) {
                    size_t save_pos = state.pos;
                    skip_whitespace(state);
                    Token next = peek(state);
                    if (next.type == TokenType::STRING_LITERAL) {
                        consume(state);
                        // Merge literals
                        std::string v1 = t.value.substr(0, t.value.size() - 1);
                        std::string v2 = next.value.substr(1);
                        t.value = v1 + v2;
                    } else {
                        state.pos = save_pos;
                        break;
                    }
                }
            }

            if (t.type == TokenType::IDENTIFIER) {
                if (t.value == "__LINE__") {
                    output.push_back({TokenType::NUMBER, std::to_string(t.line), t.filename, t.line, t.column});
                    continue;
                } else if (t.value == "__FILE__") {
                    output.push_back({TokenType::STRING_LITERAL, "\"" + t.filename + "\"", t.filename, t.line, t.column});
                    continue;
                }
                
                if (t.value == "__has_cpp_attribute") {
                    skip_whitespace(state);
                    if (peek(state).value == "(") {
                        consume(state); // (
                        skip_whitespace(state);
                        Token attr = consume(state);
                        skip_whitespace(state);
                        consume(state); // )
                        // Simple implementation: return 0 for all
                        output.push_back({TokenType::NUMBER, "0", state.filename, t.line, t.column});
                        continue;
                    }
                }
                
                if (macros.count(t.value)) {
                    // Expand macro
                    auto m = macros[t.value];
                    if (!m.is_function_like) {
                        for (auto rt : m.replacement_list) {
                            output.push_back(rt);
                        }
                    } else {
                        // Function-like expansion
                        skip_whitespace(state);
                        if (peek(state).value == "(") {
                            consume(state); // (
                            std::vector<std::vector<Token>> args;
                            std::vector<Token> current_arg;
                            int paren_depth = 0;
                            while (state.pos < state.tokens.size()) {
                                Token arg_token = peek(state);
                                if (arg_token.value == "(") paren_depth++;
                                if (arg_token.value == ")") {
                                    if (paren_depth == 0) break;
                                    paren_depth--;
                                }
                                if (arg_token.value == "," && paren_depth == 0) {
                                    args.push_back(current_arg);
                                    current_arg.clear();
                                    consume(state);
                                    skip_whitespace(state);
                                    continue;
                                }
                                current_arg.push_back(consume(state));
                            }
                            args.push_back(current_arg);
                            consume(state); // )
                            
                        // Replace params with args
                        for (size_t ri = 0; ri < m.replacement_list.size(); ++ri) {
                            const auto& rt = m.replacement_list[ri];
                            
                            // Stringification #
                            if (rt.value == "#" && ri + 1 < m.replacement_list.size()) {
                                const auto& next_rt = m.replacement_list[ri+1];
                                bool stringified = false;
                                for (size_t i = 0; i < m.params.size(); ++i) {
                                    if (next_rt.value == m.params[i]) {
                                        std::string s = "\"";
                                        if (i < args.size()) {
                                            for (const auto& at : args[i]) s += at.value;
                                        }
                                        s += "\"";
                                        output.push_back({TokenType::STRING_LITERAL, s, state.filename, rt.line, rt.column});
                                        ri++;
                                        stringified = true;
                                        break;
                                    }
                                }
                                if (stringified) continue;
                            }

                            // Token Pasting ##
                            if (ri + 1 < m.replacement_list.size() && m.replacement_list[ri+1].value == "##") {
                                // Simple implementation of ##
                                Token left = rt;
                                // Expand if left is param
                                for (size_t i = 0; i < m.params.size(); ++i) {
                                    if (left.value == m.params[i] && i < args.size() && !args[i].empty()) {
                                        left = args[i][0]; // Take first token of arg
                                        break;
                                    }
                                }
                                
                                ri += 2; // skip ## and right token
                                if (ri < m.replacement_list.size()) {
                                    Token right = m.replacement_list[ri];
                                    for (size_t i = 0; i < m.params.size(); ++i) {
                                        if (right.value == m.params[i] && i < args.size() && !args[i].empty()) {
                                            right = args[i][0];
                                            break;
                                        }
                                    }
                                    output.push_back({left.type, left.value + right.value, state.filename, left.line, left.column});
                                } else {
                                    output.push_back(left);
                                }
                                continue;
                            }
                            
                            if (rt.value == "__VA_OPT__") {
                                if (ri + 2 < m.replacement_list.size() && 
                                    m.replacement_list[ri+1].value == "(") {
                                    ri += 2;
                                    int v_depth = 1;
                                    std::vector<Token> opt_content;
                                    while (ri < m.replacement_list.size() && v_depth > 0) {
                                        if (m.replacement_list[ri].value == "(") v_depth++;
                                        if (m.replacement_list[ri].value == ")") v_depth--;
                                        if (v_depth > 0) opt_content.push_back(m.replacement_list[ri]);
                                        ri++;
                                    }
                                    ri--; // point to ')'
                                    
                                    // If __VA_ARGS__ is not empty
                                    bool va_empty = true;
                                    if (m.is_variadic) {
                                        size_t va_idx = m.params.size() - 1;
                                        if (va_idx < args.size() && !args[va_idx].empty()) {
                                            va_empty = false;
                                        }
                                    }
                                    
                                    if (!va_empty) {
                                        for (const auto& ot : opt_content) {
                                            // Recursively expand if needed, but for now just add
                                            output.push_back(ot);
                                        }
                                    }
                                    continue;
                                }
                            }
                            
                            bool found_param = false;
                            if (rt.type == TokenType::IDENTIFIER) {
                                for (size_t i = 0; i < m.params.size(); ++i) {
                                    if (rt.value == m.params[i]) {
                                        if (i < args.size()) {
                                            for (auto arg_t : args[i]) output.push_back(arg_t);
                                        }
                                        found_param = true;
                                        break;
                                    }
                                }
                            }
                            if (!found_param) {
                                output.push_back(rt);
                            }
                        }
                        } else {
                            output.push_back(t);
                        }
                    }
                } else {
                    output.push_back(t);
                }
            } else {
                output.push_back(t);
            }
        }
    }
    
    return output;
}