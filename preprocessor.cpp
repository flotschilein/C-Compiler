#include "preprocessor.h"
#include "lexer.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cstdlib>
#include <functional>

static bool is_reserved_macro_name(const std::string& name) {
    if (name == "defined") return true;
    return name == "__cplusplus" ||
           name == "__STDC__" ||
           name == "__STDC_VERSION__" ||
           name == "__STDC_HOSTED__" ||
           name == "__STDC_ISO_10646__" ||
           name == "__STDC_MB_MIGHT_NEQ_WC__" ||
           name == "__DATE__" ||
           name == "__TIME__" ||
           name == "__FILE__" ||
           name == "__LINE__" ||
           name == "__has_include" ||
           name == "__has_include_next" ||
           name == "__has_cpp_attribute" ||
           name == "__has_c_attribute" ||
           name == "__has_embed";
}

static bool is_valid_pp_token(const std::string& s) {
    if (s.empty()) return true;
    if (std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_') {
        for (char c : s)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                return false;
        return true;
    }
    if (std::isdigit(static_cast<unsigned char>(s[0])) || (s[0] == '.' && s.size() > 1 && std::isdigit(static_cast<unsigned char>(s[1])))) {
        for (char c : s)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '+' && c != '-' && c != '\'')
                return false;
        return true;
    }
    if (s.size() == 1) {
        char c = s[0];
        return c != '\'' && c != '"' && c != ' ';
    }
    static const std::set<std::string> punctuators = {
        "##", "<<", ">>", "::", "->", "++", "--",
        "==", "!=", "<=", ">=", "&&", "||",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
        "<<=", ">>=", "->*", ".*",
        "%:", "<:", ":>", "<%", "%>",
        "...", ".."
    };
    return punctuators.count(s) > 0;
}

preprocessor::preprocessor() {
    include_paths.push_back(".");
    include_next_index = 0;
    
    macros["__STDC_HOSTED__"] = {false, {}, false, {{TokenType::NUMBER, "1", "", 0, 0}}};
    macros["__cplusplus"] = {false, {}, false, {{TokenType::NUMBER, "202601L", "", 0, 0}}};
    macros["__STDC__"] = {false, {}, false, {{TokenType::NUMBER, "1", "", 0, 0}}};
    macros["__STDC_VERSION__"] = {false, {}, false, {{TokenType::NUMBER, "202311L", "", 0, 0}}};
    macros["__STDC_ISO_10646__"] = {false, {}, false, {{TokenType::NUMBER, "202401L", "", 0, 0}}};
    macros["__STDC_MB_MIGHT_NEQ_WC__"] = {false, {}, false, {{TokenType::NUMBER, "1", "", 0, 0}}};
    
    macros["__has_cpp_attribute"] = {true, {"attr"}, false, {}};
    macros["__has_c_attribute"] = {true, {"attr"}, false, {}};
    macros["__has_include"] = {true, {"header"}, false, {}};
    macros["__has_include_next"] = {true, {"header"}, false, {}};
    macros["__has_embed"] = {true, {"header"}, false, {}};
    
    std::time_t now = std::time(nullptr);
    char buf[12];
    std::strftime(buf, sizeof(buf), "%b %d %Y", std::localtime(&now));
    std::string date = buf;
    if (date[4] == '0') date[4] = ' ';
    macros["__DATE__"] = {false, {}, false, {{TokenType::STRING_LITERAL, "\"" + date + "\"", "", 0, 0}}};
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now));
    macros["__TIME__"] = {false, {}, false, {{TokenType::STRING_LITERAL, "\"" + std::string(buf) + "\"", "", 0, 0}}};
}





bool preprocessor::has_include(const std::string& filename) {
    for (const auto& path : include_paths) {
        std::filesystem::path p = std::filesystem::path(path) / filename;
        std::ifstream f(p);
        if (f.good()) return true;
    }
    return false;
}

bool preprocessor::has_include_next(const std::string& filename) {
    for (size_t idx = include_next_index; idx < include_paths.size(); ++idx) {
        std::filesystem::path p = std::filesystem::path(include_paths[idx]) / filename;
        std::ifstream f(p);
        if (f.good()) return true;
    }
    return false;
}

long long preprocessor::lookup_attribute(const std::string& name) {
    static const std::map<std::string, long long> cpp_attrs = {
        {"noreturn", 200809L},
        {"carries_dependency", 200809L},
        {"deprecated", 201309L},
        {"fallthrough", 201603L},
        {"nodiscard", 201603L},
        {"maybe_unused", 201603L},
        {"likely", 201803L},
        {"unlikely", 201803L},
        {"no_unique_address", 201803L},
        {"gnu::always_inline", 201803L},
        {"gnu::hot", 201803L},
        {"gnu::cold", 201803L},
        {"gnu::pure", 201803L},
        {"gnu::const", 201803L},
        {"gnu::flatten", 201803L},
        {"gnu::used", 201803L},
    };
    static const std::map<std::string, long long> c_attrs = {
        {"deprecated", 202311L},
        {"fallthrough", 202311L},
        {"nodiscard", 202311L},
        {"maybe_unused", 202311L},
    };
    auto it = cpp_attrs.find(name);
    if (it != cpp_attrs.end()) return it->second;
    it = c_attrs.find(name);
    if (it != c_attrs.end()) return it->second;
    // Handle scoped attributes like clang::*, gnu::*, etc. — unknown → 0
    return 0;
}

std::vector<Token> preprocessor::preprocess(const std::string& filename) {
    Lexer lexer;
    std::vector<Token> tokens = lexer.tokenize_file(filename);
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
    if (pos >= tokens.size()) return false;
    if (tokens[pos].value != "#") return false;
    
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
    
    std::string found_path;
    for (const auto& path : include_paths) {
        std::filesystem::path p = std::filesystem::path(path) / filename;
        std::ifstream f(p);
        if (f.good()) {
            found_path = p.string();
            break;
        }
    }
    
    if (found_path.empty()) {
        throw std::runtime_error("File not found: " + filename);
    }
    
    if (once_files.count(found_path)) return;
    
    std::vector<Token> included_tokens = preprocess(found_path);
    
    for (const auto& it : included_tokens) {
        if (it.type != TokenType::END_OF_FILE) {
            output.push_back(it);
        }
    }
}

void preprocessor::handle_include_next(PreprocessorState& state, std::vector<Token>& output) {
    consume(state); // #
    skip_whitespace(state);
    consume(state); // include_next
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
        throw std::runtime_error("Invalid #include_next directive");
    }
    
    std::string found_path;
    for (size_t idx = include_next_index; idx < include_paths.size(); ++idx) {
        std::filesystem::path p = std::filesystem::path(include_paths[idx]) / filename;
        std::ifstream f(p);
        if (f.good()) {
            found_path = p.string();
            break;
        }
    }
    
    if (found_path.empty()) {
        throw std::runtime_error("File not found: " + filename);
    }
    
    if (once_files.count(found_path)) return;
    
    std::vector<Token> included_tokens = preprocess(found_path);
    
    for (const auto& it : included_tokens) {
        if (it.type != TokenType::END_OF_FILE) {
            output.push_back(it);
        }
    }
}

void preprocessor::handle_error(PreprocessorState& state) {
    consume(state); // #
    skip_whitespace(state);
    consume(state); // error
    skip_whitespace(state);
    
    std::string msg;
    while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
        Token t = consume(state);
        if (t.type != TokenType::WHITESPACE || !msg.empty()) {
            if (!msg.empty() && t.type == TokenType::WHITESPACE) msg += ' ';
            else msg += t.value;
        }
    }
    throw std::runtime_error("#error: " + msg);
}

void preprocessor::handle_warning(PreprocessorState& state) {
    consume(state); // #
    skip_whitespace(state);
    consume(state); // warning
    skip_whitespace(state);
    
    std::string msg;
    while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
        Token t = consume(state);
        if (t.type != TokenType::WHITESPACE || !msg.empty()) {
            if (!msg.empty() && t.type == TokenType::WHITESPACE) msg += ' ';
            else msg += t.value;
        }
    }
    std::cerr << "warning: " << msg << std::endl;
}

void preprocessor::handle_line(PreprocessorState& state) {
    consume(state); // #
    skip_whitespace(state);
    consume(state); // line
    skip_whitespace(state);
    
    Token lineno = consume(state);
    if (lineno.type != TokenType::NUMBER) {
        throw std::runtime_error("#line requires a line number");
    }
    
    int new_line = std::stoi(lineno.value);
    if (new_line <= 0) {
        throw std::runtime_error("#line number must be a positive integer");
    }
    
    skip_whitespace(state);
    Token filename_token = peek(state);
    std::string new_filename;
    if (filename_token.type == TokenType::STRING_LITERAL) {
        consume(state);
        new_filename = filename_token.value.substr(1, filename_token.value.size() - 2);
    }
    
    // Update the line numbers of subsequent tokens
    // The next source line (at state.pos) should become new_line
    int next_line = lineno.line + 1;
    int delta = new_line - next_line;
    for (size_t j = state.pos; j < state.tokens.size(); ++j) {
        state.tokens[j].line += delta;
        if (!new_filename.empty()) {
            state.tokens[j].filename = new_filename;
        }
    }
}

void preprocessor::handle_pragma(PreprocessorState& state) {
    consume(state); // #
    skip_whitespace(state);
    consume(state); // pragma
    skip_whitespace(state);
    
    std::string content;
    while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
        Token t = consume(state);
        if (t.type != TokenType::WHITESPACE || !content.empty()) {
            if (!content.empty() && t.type == TokenType::WHITESPACE) content += ' ';
            else content += t.value;
        }
    }
    
    // Handle #pragma once
    if (content == "once") {
        once_files.insert(state.filename);
        return;
    }
    
    // Handle #pragma STDC
    if (content.compare(0, 5, "STDC ") == 0) {
        // STDC FP_CONTRACT, STDC FENV_ACCESS, STDC CX_LIMITED_RANGE
        // These are silently accepted (standard allows ignoring them)
        return;
    }
    
    // Other pragmas are silently ignored
}

void preprocessor::handle_directive(PreprocessorState& state, std::vector<Token>& output) {
    size_t hash_pos = state.pos;
    size_t next_pos = hash_pos + 1;
    while (next_pos < state.tokens.size() && state.tokens[next_pos].type == TokenType::WHITESPACE) next_pos++;
    
    // Empty directive: # alone on a line — silently ignore
    if (next_pos >= state.tokens.size() || state.tokens[next_pos].type == TokenType::NEWLINE) {
        consume(state);
        return;
    }
    
    if (next_pos < state.tokens.size()) {
        std::string dir = state.tokens[next_pos].value;
        if (dir == "define") {
            handle_define(state);
        } else if (dir == "include") {
            handle_include(state, output);
        } else if (dir == "include_next") {
            handle_include_next(state, output);
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
        } else if (dir == "elif" || dir == "elifdef" || dir == "elifndef" || dir == "else") {
            consume(state); // #
            skip_whitespace(state);
            consume(state); // directive name
            while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
                consume(state);
            }
            if (peek(state).type == TokenType::NEWLINE) {
                consume(state);
            }
            skip_failed_branch(state, output, true);
        } else if (dir == "endif") {
            consume(state);
        } else if (dir == "error") {
            handle_error(state);
        } else if (dir == "warning") {
            handle_warning(state);
        } else if (dir == "line") {
            handle_line(state);
        } else if (dir == "pragma") {
            handle_pragma(state);
        } else {
            output.push_back(consume(state));
        }
    } else if (peek(state).type == TokenType::NEWLINE || peek(state).type == TokenType::END_OF_FILE) {
        // empty directive (# alone) — silently ignore
        return;
    } else {
        output.push_back(consume(state));
    }
    
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
        if (name.value == "defined") {
            throw std::runtime_error("'defined' cannot be used as a macro name");
        }
        if (is_reserved_macro_name(name.value)) {
            std::cerr << "warning: undefining builtin macro '" << name.value << "'" << std::endl;
        }
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
    if (header.value == "<") {
        while (peek(state).value != ">" && peek(state).type != TokenType::END_OF_FILE) {
            filename += consume(state).value;
        }
        consume(state); // >
    } else if (header.type == TokenType::STRING_LITERAL) {
        filename = header.value.substr(1, header.value.size() - 2);
    } else {
        throw std::runtime_error("Expected header name or string literal after #embed");
    }
    
    // Parse optional embed parameters
    std::vector<Token> prefix_tokens, suffix_tokens, if_empty_tokens;
    long long limit_val = -1;
    
    while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
        Token kw = consume(state);
        if (kw.value == "prefix" && peek(state).value == "(") {
            consume(state); // (
            int depth = 1;
            while (depth > 0) {
                Token pt = consume(state);
                if (pt.type == TokenType::END_OF_FILE) break;
                if (pt.value == "(") depth++;
                else if (pt.value == ")") depth--;
                if (depth > 0) prefix_tokens.push_back(pt);
            }
        } else if (kw.value == "suffix" && peek(state).value == "(") {
            consume(state); // (
            int depth = 1;
            while (depth > 0) {
                Token pt = consume(state);
                if (pt.type == TokenType::END_OF_FILE) break;
                if (pt.value == "(") depth++;
                else if (pt.value == ")") depth--;
                if (depth > 0) suffix_tokens.push_back(pt);
            }
        } else if (kw.value == "if_empty" && peek(state).value == "(") {
            consume(state); // (
            int depth = 1;
            while (depth > 0) {
                Token pt = consume(state);
                if (pt.type == TokenType::END_OF_FILE) break;
                if (pt.value == "(") depth++;
                else if (pt.value == ")") depth--;
                if (depth > 0) if_empty_tokens.push_back(pt);
            }
        } else if (kw.value == "limit" && peek(state).value == "(") {
            consume(state); // (
            std::vector<Token> limit_tokens;
            while (peek(state).value != ")") {
                limit_tokens.push_back(consume(state));
            }
            consume(state); // )
            limit_val = evaluate_expression(limit_tokens);
        } else if (kw.type == TokenType::WHITESPACE) {
            continue;
        } else {
            // Unknown token, skip rest of line
            while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
                consume(state);
            }
            break;
        }
        skip_whitespace(state);
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
    
    // Apply limit parameter
    if (limit_val >= 0 && static_cast<size_t>(limit_val) < data.size()) {
        data.resize(static_cast<size_t>(limit_val));
    }
    
    // Apply if_empty parameter
    if (data.empty() && !if_empty_tokens.empty()) {
        for (const auto& t : if_empty_tokens) {
            output.push_back(t);
        }
        return;
    }
    
    // Output prefix tokens
    if (!prefix_tokens.empty()) {
        for (const auto& t : prefix_tokens) {
            output.push_back(t);
        }
        output.push_back({TokenType::PUNCTUATOR, ",", state.filename, header.line, header.column});
    }
    
    // Output data bytes
    for (size_t i = 0; i < data.size(); ++i) {
        output.push_back({TokenType::NUMBER, std::to_string((int)data[i]), state.filename, header.line, header.column});
        if (i + 1 < data.size()) {
            output.push_back({TokenType::PUNCTUATOR, ",", state.filename, header.line, header.column});
        }
    }
    
    // Output suffix tokens
    if (!suffix_tokens.empty()) {
        output.push_back({TokenType::PUNCTUATOR, ",", state.filename, header.line, header.column});
        for (const auto& t : suffix_tokens) {
            output.push_back(t);
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
        skip_failed_branch(state, output);
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
        skip_failed_branch(state, output);
    }
}

bool preprocessor::handle_elifdef(PreprocessorState& state, std::vector<Token>& output, bool negate) {
    // Check if this #elifdef is inside an active #if block
    bool matched = false;
    size_t saved = state.pos;
    // Find previous directive by scanning backwards
    for (int d = 1; saved > 0; ) {
        saved--;
        if (state.tokens[saved].type == TokenType::NEWLINE || saved == 0) {
            size_t check = saved;
            if (state.tokens[saved].type == TokenType::NEWLINE) check = saved + 1;
            while (check < state.tokens.size() && state.tokens[check].type == TokenType::WHITESPACE) check++;
            if (check < state.tokens.size() && state.tokens[check].value == "#") {
                size_t dir_pos = check + 1;
                while (dir_pos < state.tokens.size() && state.tokens[dir_pos].type == TokenType::WHITESPACE) dir_pos++;
                if (dir_pos < state.tokens.size()) {
                    std::string prev = state.tokens[dir_pos].value;
                    if (prev == "endif") d++;
                    else if (prev == "if" || prev == "ifdef" || prev == "ifndef") {
                        d--;
                        if (d == 0) { matched = true; break; }
                    }
                }
            }
        }
    }
    
    if (!matched) return false;
    
    consume(state); // #
    skip_whitespace(state);
    consume(state); // elifdef/elifndef
    skip_whitespace(state);
    
    Token name = consume(state);
    bool exists = macros.count(name.value) > 0;
    bool condition = negate ? !exists : exists;
    
    if (!condition) {
        while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
            consume(state);
        }
        if (peek(state).type == TokenType::NEWLINE) {
            consume(state);
        }
        skip_failed_branch(state, output);
    }
    return true;
}

bool preprocessor::handle_elif(PreprocessorState& state, std::vector<Token>& output) {
    Token t = peek(state);
    // Check if this #elif is inside an active #if block by looking for a preceding #if/#ifdef/#ifndef
    // For unmatched #elif, return false
    // Find the matching #if
    size_t pos = state.pos;
    for (int d = 1; pos > 0; ) {
        pos--;
        if (state.tokens[pos].type == TokenType::NEWLINE) {
            size_t check = pos + 1;
            while (check < state.tokens.size() && state.tokens[check].type == TokenType::WHITESPACE) check++;
            if (check < state.tokens.size() && state.tokens[check].value == "#") {
                size_t dir_pos = check + 1;
                while (dir_pos < state.tokens.size() && state.tokens[dir_pos].type == TokenType::WHITESPACE) dir_pos++;
                if (dir_pos < state.tokens.size()) {
                    std::string prev = state.tokens[dir_pos].value;
                    if (prev == "endif") d++;
                    else if (prev == "if" || prev == "ifdef" || prev == "ifndef") {
                        d--;
                        if (d == 0) {
                            // Found matching #if, process as normal
                            consume(state); // #
                            skip_whitespace(state);
                            consume(state); // elif
                            
                            std::vector<Token> condition_tokens;
                            while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
                                condition_tokens.push_back(consume(state));
                            }
                            
                            if (!evaluate_condition(condition_tokens)) {
                                skip_failed_branch(state, output);
                            }
                            return true;
                        }
                    }
                }
            }
        }
    }
    
    // Unmatched #elif
    return false;
}

long long preprocessor::evaluate_expression(const std::vector<Token>& tokens) {
    // Step 1: Handle defined(), __has_include(), __has_include_next(), __has_cpp_attribute(), __has_c_attribute()
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
                bool exists = macros.count(tokens[i].value) > 0;
                processed.push_back({TokenType::NUMBER, exists ? "1" : "0", "", 0, 0});
                i++;
            }
            if (has_paren) {
                while (i < tokens.size() && tokens[i].value != ")") i++;
            }
            continue;
        }
        
        if (tokens[i].value == "__has_include" || tokens[i].value == "__has_include_next") {
            bool is_next = (tokens[i].value == "__has_include_next");
            i++;
            while (i < tokens.size() && tokens[i].type == TokenType::WHITESPACE) i++;
            if (i < tokens.size() && tokens[i].value == "(") {
                i++;
                std::string inc;
                while (i < tokens.size() && tokens[i].value != ")") {
                    if (tokens[i].type == TokenType::STRING_LITERAL) {
                        inc = tokens[i].value.substr(1, tokens[i].value.size() - 2);
                    } else if (tokens[i].type == TokenType::HEADER_NAME) {
                        inc = tokens[i].value.substr(1, tokens[i].value.size() - 2);
                    }
                    i++;
                }
                bool found = is_next ? has_include_next(inc) : has_include(inc);
                processed.push_back({TokenType::NUMBER, found ? "1" : "0", "", 0, 0});
            }
            continue;
        }
        
        if (tokens[i].value == "__has_cpp_attribute" || tokens[i].value == "__has_c_attribute") {
            i++;
            while (i < tokens.size() && tokens[i].type == TokenType::WHITESPACE) i++;
            if (i < tokens.size() && tokens[i].value == "(") {
                i++;
                std::string attr_name;
                while (i < tokens.size() && tokens[i].value != ")") {
                    if (tokens[i].type == TokenType::IDENTIFIER) {
                        if (!attr_name.empty()) attr_name += "::";
                        attr_name += tokens[i].value;
                    }
                    i++;
                }
                processed.push_back({TokenType::NUMBER, std::to_string(lookup_attribute(attr_name)), "", 0, 0});
            }
            continue;
        }
        
        if (tokens[i].value == "__has_embed") {
            i++;
            while (i < tokens.size() && tokens[i].type == TokenType::WHITESPACE) i++;
            if (i < tokens.size() && tokens[i].value == "(") {
                i++;
                std::string inc;
                while (i < tokens.size() && tokens[i].value != ")") {
                    if (tokens[i].type == TokenType::STRING_LITERAL) {
                        inc = tokens[i].value.substr(1, tokens[i].value.size() - 2);
                    }
                    i++;
                }
                bool found = has_include(inc);
                processed.push_back({TokenType::NUMBER, found ? "1" : "0", "", 0, 0});
            }
            continue;
        }
        
        if (tokens[i].type == TokenType::IDENTIFIER && tokens[i].value != "true" && tokens[i].value != "false") {
            // Check if it's a predefined object-like macro
            auto mit = macros.find(tokens[i].value);
            if (mit != macros.end() && !mit->second.is_function_like && !mit->second.replacement_list.empty()) {
                for (const auto& rt : mit->second.replacement_list) {
                    if (rt.type != TokenType::WHITESPACE) {
                        processed.push_back(rt);
                    }
                }
            } else {
                // Unbound identifier resolves to 0
                processed.push_back({TokenType::NUMBER, "0", "", 0, 0});
            }
        } else if (tokens[i].type != TokenType::WHITESPACE) {
            processed.push_back(tokens[i]);
        }
    }
    
    // Step 2: Evaluate the constant expression
    // Simple recursive descent parser for integer constant expressions
    // Grammar:
    //   expr → or_expr
    //   or_expr → and_expr { '||' and_expr }
    //   and_expr → eq_expr { '&&' eq_expr }
    //   eq_expr → rel_expr { ('=='|'!=') rel_expr }
    //   rel_expr → add_expr { ('<'|'>'|'<='|'>=') add_expr }
    //   add_expr → mul_expr { ('+'|'-') mul_expr }
    //   mul_expr → unary_expr { ('*'|'/'|'%') unary_expr }
    //   unary_expr → ('!'|'~'|'-'|'+') unary_expr | primary_expr
    //   primary_expr → NUMBER | '(' expr ')'
    
    size_t pos = 0;
    
    auto peek_tok = [&]() -> Token {
        if (pos >= processed.size()) return {TokenType::END_OF_FILE, "", "", 0, 0};
        return processed[pos];
    };
    
    auto consume_tok = [&]() -> Token {
        if (pos >= processed.size()) return {TokenType::END_OF_FILE, "", "", 0, 0};
        return processed[pos++];
    };
    
    std::function<long long()> parse_or, parse_and, parse_eq, parse_rel, parse_add, parse_mul, parse_unary, parse_primary;
    
    parse_primary = [&]() -> long long {
        Token t = consume_tok();
        if (t.value == "(") {
            long long val = parse_or();
            if (peek_tok().value == ")") consume_tok();
            return val;
        }
        if (t.type == TokenType::NUMBER) {
            std::string val = t.value;
            bool is_char = false;
            if (val.size() >= 2 && val[0] == '\'' && val.back() == '\'') {
                is_char = true;
                val = std::to_string((unsigned char)val[1]);
            }
            if (val.size() > 2 && val.substr(0, 2) == "0x" || val.substr(0, 2) == "0X") {
                return std::strtoll(val.c_str(), nullptr, 16);
            }
            if (val.size() > 1 && val[0] == '0') {
                return std::strtoll(val.c_str(), nullptr, 8);
            }
            return std::strtoll(val.c_str(), nullptr, 10);
        }
        if (t.value == "true") return 1;
        if (t.value == "false") return 0;
        return 0;
    };
    
    parse_unary = [&]() -> long long {
        Token t = peek_tok();
        if (t.value == "!") { consume_tok(); return !parse_unary(); }
        if (t.value == "~") { consume_tok(); return ~parse_unary(); }
        if (t.value == "-") { consume_tok(); return -parse_unary(); }
        if (t.value == "+") { consume_tok(); return +parse_unary(); }
        return parse_primary();
    };
    
    parse_mul = [&]() -> long long {
        long long left = parse_unary();
        while (true) {
            Token t = peek_tok();
            if (t.value == "*") { consume_tok(); left *= parse_unary(); }
            else if (t.value == "/") { consume_tok(); long long right = parse_unary(); if (right == 0) throw std::runtime_error("Division by zero in #if"); left /= right; }
            else if (t.value == "%") { consume_tok(); long long right = parse_unary(); if (right == 0) throw std::runtime_error("Modulo by zero in #if"); left %= right; }
            else break;
        }
        return left;
    };
    
    parse_add = [&]() -> long long {
        long long left = parse_mul();
        while (true) {
            Token t = peek_tok();
            if (t.value == "+") { consume_tok(); left += parse_mul(); }
            else if (t.value == "-") { consume_tok(); left -= parse_mul(); }
            else break;
        }
        return left;
    };
    
    parse_rel = [&]() -> long long {
        long long left = parse_add();
        while (true) {
            Token t = peek_tok();
            if (t.value == "<") { consume_tok(); left = left < parse_add(); }
            else if (t.value == ">") { consume_tok(); left = left > parse_add(); }
            else if (t.value == "<=") { consume_tok(); left = left <= parse_add(); }
            else if (t.value == ">=") { consume_tok(); left = left >= parse_add(); }
            else break;
        }
        return left;
    };
    
    parse_eq = [&]() -> long long {
        long long left = parse_rel();
        while (true) {
            Token t = peek_tok();
            if (t.value == "==") { consume_tok(); left = left == parse_rel(); }
            else if (t.value == "!=") { consume_tok(); left = left != parse_rel(); }
            else break;
        }
        return left;
    };
    
    parse_and = [&]() -> long long {
        long long left = parse_eq();
        while (true) {
            Token t = peek_tok();
            if (t.value == "&&") { consume_tok(); left = left && parse_eq(); }
            else break;
        }
        return left;
    };
    
    parse_or = [&]() -> long long {
        long long left = parse_and();
        while (true) {
            Token t = peek_tok();
            if (t.value == "||") { consume_tok(); left = left || parse_and(); }
            else break;
        }
        return left;
    };
    
    return parse_or();
}

bool preprocessor::evaluate_condition(const std::vector<Token>& tokens) {
    return evaluate_expression(tokens) != 0;
}

void preprocessor::skip_failed_branch(PreprocessorState& state, std::vector<Token>& output, bool skip_all) {
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
                } else if (dir == "else" && depth == 1) {
                    if (!skip_all) {
                        state.pos = next_pos;
                        consume(state);
                        return;
                    }
                } else if (dir == "elif" && depth == 1) {
                    state.pos = next_pos;
                    consume(state);
                    std::vector<Token> condition_tokens;
                    while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
                        condition_tokens.push_back(consume(state));
                    }
                    if (!skip_all && evaluate_condition(condition_tokens)) {
                        return;
                    }
                } else if (dir == "elifdef" && depth == 1) {
                    state.pos = next_pos;
                    consume(state);
                    skip_whitespace(state);
                    Token name = consume(state);
                    if (!skip_all && macros.count(name.value) > 0) {
                        while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
                            consume(state);
                        }
                        return;
                    }
                } else if (dir == "elifndef" && depth == 1) {
                    state.pos = next_pos;
                    consume(state);
                    skip_whitespace(state);
                    Token name = consume(state);
                    if (!skip_all && macros.count(name.value) == 0) {
                        while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
                            consume(state);
                        }
                        return;
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
    
    if (name.value == "defined") {
        throw std::runtime_error("'defined' cannot be used as a macro name");
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

std::vector<Token> preprocessor::expand_tokens(std::vector<Token> tokens, std::set<std::string> expanding) {
    std::vector<Token> result;
    
    for (size_t i = 0; i < tokens.size(); ++i) {
        Token t = tokens[i];
        
        if (t.type == TokenType::IDENTIFIER && macros.count(t.value) && !expanding.count(t.value)) {
            Macro& m = macros[t.value];
            
            if (m.is_function_like) {
                // Check if followed by (
                size_t check = i + 1;
                while (check < tokens.size() && tokens[check].type == TokenType::WHITESPACE) check++;
                
                if (check < tokens.size() && tokens[check].value == "(") {
                    // Collect arguments
                    std::vector<std::vector<Token>> args;
                    std::vector<Token> current_arg;
                    int paren_depth = 0;
                    check++; // skip (
                    
                    while (check < tokens.size()) {
                        Token at = tokens[check];
                        if (at.value == "(") paren_depth++;
                        if (at.value == ")") {
                            if (paren_depth == 0) break;
                            paren_depth--;
                        }
                        if (at.value == "," && paren_depth == 0) {
                            args.push_back(current_arg);
                            current_arg.clear();
                            check++;
                            continue;
                        }
                        current_arg.push_back(at);
                        check++;
                    }
                    args.push_back(current_arg);
                    // Pad missing variadic args with empty
                    if (m.is_variadic && args.size() < m.params.size()) {
                        args.push_back({});
                    }
                    check++; // skip )
                    
                    i = check; // advance past the entire invocation
                    
                    auto expanded = expand_function_like(m, args, expanding);
                    for (auto& et : expanded) {
                        if (et.value == "__LINE__") {
                            result.push_back({TokenType::NUMBER, std::to_string(t.line), t.filename, t.line, t.column});
                        } else if (et.value == "__FILE__") {
                            result.push_back({TokenType::STRING_LITERAL, "\"" + t.filename + "\"", t.filename, t.line, t.column});
                        } else {
                            result.push_back(et);
                        }
                    }
                    continue;
                }
            }
            
            if (!m.is_function_like) {
                std::set<std::string> new_expanding = expanding;
                new_expanding.insert(t.value);
                auto further = expand_tokens(m.replacement_list, new_expanding);
                for (auto& ft : further) {
                    if (ft.value == "__LINE__") {
                        result.push_back({TokenType::NUMBER, std::to_string(t.line), t.filename, t.line, t.column});
                    } else if (ft.value == "__FILE__") {
                        result.push_back({TokenType::STRING_LITERAL, "\"" + t.filename + "\"", t.filename, t.line, t.column});
                    } else {
                        result.push_back(ft);
                    }
                }
                continue;
            }
        }
        
        // Handle _Pragma operator
        if (t.value == "_Pragma" && t.type == TokenType::IDENTIFIER) {
            size_t check = i + 1;
            while (check < tokens.size() && tokens[check].type == TokenType::WHITESPACE) check++;
            if (check < tokens.size() && tokens[check].value == "(") {
                check++;
                std::vector<Token> pragma_tokens;
                int p_depth = 1;
                while (check < tokens.size() && p_depth > 0) {
                    if (tokens[check].value == "(") p_depth++;
                    if (tokens[check].value == ")") p_depth--;
                    if (p_depth > 0) pragma_tokens.push_back(tokens[check]);
                    check++;
                }
                i = check;
                // Extract the string from _Pragma("string")
                if (!pragma_tokens.empty() && pragma_tokens[0].type == TokenType::STRING_LITERAL) {
                    std::string s = pragma_tokens[0].value;
                    // Remove quotes and process
                    s = s.substr(1, s.size() - 2);
                    // Unescape the string
                    std::string unescaped;
                    for (size_t si = 0; si < s.size(); ++si) {
                        if (s[si] == '\\' && si + 1 < s.size()) {
                            if (s[si+1] == 'n') { unescaped += '\n'; si++; }
                            else if (s[si+1] == '"') { unescaped += '"'; si++; }
                            else if (s[si+1] == '\\') { unescaped += '\\'; si++; }
                            else { unescaped += s[si]; }
                        } else {
                            unescaped += s[si];
                        }
                    }
                    // Re-tokenize the unescaped pragma content
                    Lexer pragma_lexer;
                    auto pragma_toks = pragma_lexer.tokenize_string(unescaped, t.filename);
                    for (auto& pt : pragma_toks) {
                        if (pt.type != TokenType::END_OF_FILE && pt.type != TokenType::NEWLINE) {
                            result.push_back(pt);
                        }
                    }
                }
                continue;
            }
        }
        
        result.push_back(t);
    }
    
    return result;
}

std::vector<Token> preprocessor::expand_function_like(Macro& m, std::vector<std::vector<Token>>& args,
                                                        std::set<std::string>& expanding) {
    // First, expand the arguments (for non-#, non-## params)
    // We need to know which params are used with # or ##
    std::set<size_t> stringify_params;
    std::set<size_t> paste_left_params;
    std::set<size_t> paste_right_params;
    
    auto skip_ws = [&](size_t idx) -> size_t {
        while (idx < m.replacement_list.size() && m.replacement_list[idx].type == TokenType::WHITESPACE) idx++;
        return idx;
    };
    
    for (size_t ri = 0; ri < m.replacement_list.size(); ++ri) {
        size_t next = skip_ws(ri + 1);
        if (m.replacement_list[ri].value == "#" && next < m.replacement_list.size()) {
            for (size_t pi = 0; pi < m.params.size(); ++pi) {
                if (m.replacement_list[next].value == m.params[pi]) {
                    stringify_params.insert(pi);
                }
            }
        }
        if (m.replacement_list[ri].value == "##") {
            size_t prev = ri;
            // find the previous non-ws token
            size_t prev_ri = ri;
            while (prev_ri > 0) {
                prev_ri--;
                if (m.replacement_list[prev_ri].type != TokenType::WHITESPACE) break;
            }
            if (m.replacement_list[prev_ri].type != TokenType::WHITESPACE) {
                for (size_t pi = 0; pi < m.params.size(); ++pi) {
                    if (m.replacement_list[prev_ri].value == m.params[pi]) {
                        paste_left_params.insert(pi);
                    }
                }
            }
            // Find the next non-ws token after ##
            size_t next_ri = skip_ws(ri + 1);
            if (next_ri < m.replacement_list.size()) {
                for (size_t pi = 0; pi < m.params.size(); ++pi) {
                    if (m.replacement_list[next_ri].value == m.params[pi]) {
                        paste_right_params.insert(pi);
                    }
                }
            }
        }
    }
    
    // Expand non-#, non-## args
    std::vector<std::vector<Token>> expanded_args = args;
    for (size_t ai = 0; ai < args.size(); ++ai) {
        if (!stringify_params.count(ai) && !paste_left_params.count(ai) && !paste_right_params.count(ai)) {
            expanded_args[ai] = expand_tokens(args[ai], expanding);
        }
    }
    
    std::vector<Token> result;
    
    for (size_t ri = 0; ri < m.replacement_list.size(); ++ri) {
        if (m.replacement_list[ri].type == TokenType::WHITESPACE) continue;
        const auto& rt = m.replacement_list[ri];
        
        // Stringification # (skip whitespace after #)
        if (rt.value == "#") {
            size_t str_next = ri + 1;
            while (str_next < m.replacement_list.size() && m.replacement_list[str_next].type == TokenType::WHITESPACE) str_next++;
            if (str_next < m.replacement_list.size()) {
                bool stringified = false;
                for (size_t i = 0; i < m.params.size(); ++i) {
                    if (m.replacement_list[str_next].value == m.params[i]) {
                        std::string s = "\"";
                        if (i < args.size()) {
                            for (const auto& at : args[i]) {
                                for (char c : at.value) {
                                    if (c == '"' || c == '\\') s += '\\';
                                    s += c;
                                }
                            }
                        }
                        s += "\"";
                        result.push_back({TokenType::STRING_LITERAL, s, rt.filename, rt.line, rt.column});
                        ri = str_next;
                        stringified = true;
                        break;
                    }
                }
                if (stringified) continue;
            }
        }
        
        // Token Pasting ## (handles chained: a ## b ## c)
        {
            // Find next non-whitespace token
            size_t next_ri = ri + 1;
            while (next_ri < m.replacement_list.size() && m.replacement_list[next_ri].type == TokenType::WHITESPACE) next_ri++;
            if (next_ri < m.replacement_list.size() && m.replacement_list[next_ri].value == "##") {
                // Helper: get the effective token from a parameter argument
                // Returns {found, empty, type, val} where:
                //   found = token matched a parameter
                //   empty = the argument exists but has no non-WS tokens
                //   type/val = the first or last non-WS token of the argument
                struct Resolved { bool found; bool empty; TokenType type; std::string val; };
                auto resolve_param = [&](const std::string& name, size_t& first_idx_out, bool want_last) -> Resolved {
                    for (size_t ai = 0; ai < m.params.size(); ++ai) {
                        if (name == m.params[ai] && ai < args.size()) {
                            first_idx_out = ai;
                            // Find the first/last non-WS token in the argument
                            if (want_last) {
                                for (int j = (int)args[ai].size() - 1; j >= 0; --j) {
                                    if (args[ai][j].type != TokenType::WHITESPACE) {
                                        return {true, false, args[ai][j].type, args[ai][j].value};
                                    }
                                }
                                return {true, true, TokenType::IDENTIFIER, ""};
                            } else {
                                for (size_t j = 0; j < args[ai].size(); ++j) {
                                    if (args[ai][j].type != TokenType::WHITESPACE) {
                                        return {true, false, args[ai][j].type, args[ai][j].value};
                                    }
                                }
                                return {true, true, TokenType::IDENTIFIER, ""};
                            }
                        }
                    }
                    first_idx_out = (size_t)-1;
                    return {false, false, TokenType::IDENTIFIER, ""};
                };
                
                // Start with the current token as the accumulated paste value
                size_t left_ai = (size_t)-1;
                auto left = resolve_param(rt.value, left_ai, true);
                bool left_effective = left.found ? !left.empty : true;
                TokenType acc_type = left.found ? left.type : rt.type;
                std::string acc_val = left.found ? left.val : rt.value;
                // Output non-last tokens of left param (excluding trailing WS)
                if (left_ai != (size_t)-1 && !left.empty) {
                    int last_nz = (int)args[left_ai].size() - 1;
                    while (last_nz >= 0 && args[left_ai][last_nz].type == TokenType::WHITESPACE) last_nz--;
                    for (int j = 0; j < last_nz; ++j) {
                        result.push_back(args[left_ai][j]);
                    }
                }
                if (!left_effective) acc_val = ""; // empty placemarker
                
                ri = next_ri; // ri is at first ##
                
                while (true) {
                    // Skip past ## and any whitespace to find the right token
                    size_t right_ri = ri + 1;
                    while (right_ri < m.replacement_list.size() && m.replacement_list[right_ri].type == TokenType::WHITESPACE) right_ri++;
                    if (right_ri >= m.replacement_list.size()) {
                        if (left_effective) {
                            if (!is_valid_pp_token(acc_val)) {
                                std::cerr << "warning: token pasting of '" << acc_val << "' is not a valid preprocessing token" << std::endl;
                            }
                            result.push_back({acc_type, acc_val, rt.filename, rt.line, rt.column});
                        }
                        ri = right_ri - 1;
                        break;
                    }
                    
                    Token right = m.replacement_list[right_ri];
                    size_t right_ai = (size_t)-1;
                    auto right_res = resolve_param(right.value, right_ai, false);
                    bool right_effective = right_res.found ? !right_res.empty : true;
                    std::string rt_val = right_res.found ? right_res.val : right.value;
                    // Output remaining right-arg tokens (after first, excluding leading WS)
                    if (right_ai != (size_t)-1 && !right_res.empty) {
                        size_t first_nz = 0;
                        while (first_nz < args[right_ai].size() && args[right_ai][first_nz].type == TokenType::WHITESPACE) first_nz++;
                        for (size_t j = first_nz + 1; j < args[right_ai].size(); ++j) {
                            result.push_back(args[right_ai][j]);
                        }
                    }
                    if (!right_effective) rt_val = "";
                    
                    // Special case: , ## empty_va_args → delete the comma too
                    if (right_res.found && right_res.empty && acc_val == ",") {
                        acc_val = "";
                        left_effective = false;
                    } else if (left_effective || right_effective) {
                        if (!left_effective) { acc_type = right_res.found ? right_res.type : right.type; }
                        acc_val = acc_val + rt_val;
                    } else {
                        acc_val = "";
                        left_effective = false;
                    }
                    if (right_effective && !acc_val.empty()) left_effective = true;
                    ri = right_ri; // advance past right token
                    
                    // Check if another ## follows
                    size_t next2 = ri + 1;
                    while (next2 < m.replacement_list.size() && m.replacement_list[next2].type == TokenType::WHITESPACE) next2++;
                    if (next2 < m.replacement_list.size() && m.replacement_list[next2].value == "##") {
                        ri = next2; // advance to next ##, loop again
                        continue;
                    }
                    
                    if (left_effective || (left.found && !left.empty)) {
                        if (!is_valid_pp_token(acc_val)) {
                            std::cerr << "warning: token pasting of '" << acc_val << "' is not a valid preprocessing token" << std::endl;
                        }
                        result.push_back({acc_type, acc_val, rt.filename, rt.line, rt.column});
                    }
                    break;
                }
                continue;
            }
        }
        
        // __VA_OPT__
        if (rt.value == "__VA_OPT__") {
            if (ri + 2 < m.replacement_list.size() && m.replacement_list[ri+1].value == "(") {
                ri += 2;
                int v_depth = 1;
                std::vector<Token> opt_content;
                while (ri < m.replacement_list.size() && v_depth > 0) {
                    if (m.replacement_list[ri].value == "(") v_depth++;
                    if (m.replacement_list[ri].value == ")") v_depth--;
                    if (v_depth > 0) opt_content.push_back(m.replacement_list[ri]);
                    ri++;
                }
                ri--;
                
                bool va_empty = true;
                if (m.is_variadic) {
                    size_t va_idx = m.params.size() - 1;
                    if (va_idx < args.size() && !args[va_idx].empty()) {
                        va_empty = false;
                    }
                }
                
                if (!va_empty) {
                    for (const auto& ot : opt_content) {
                        result.push_back(ot);
                    }
                }
                continue;
            }
        }
        
        // Parameter substitution
        bool found_param = false;
        if (rt.type == TokenType::IDENTIFIER) {
            for (size_t i = 0; i < m.params.size(); ++i) {
                if (rt.value == m.params[i]) {
                    if (i < expanded_args.size()) {
                        for (auto arg_t : expanded_args[i]) {
                            result.push_back(arg_t);
                        }
                    }
                    found_param = true;
                    break;
                }
            }
        }
        if (!found_param) {
            result.push_back(rt);
        }
    }
    
    // Re-scan the result with the current macro disabled
    std::set<std::string> new_expanding = expanding;
    // Add all currently expanding macros to prevent recursion
    // The macro being expanded should be in the set already
    return expand_tokens(result, new_expanding);
}

std::vector<Token> preprocessor::run_phases_4(std::vector<Token>& tokens) {
    PreprocessorState state;
    state.tokens = tokens;
    state.filename = tokens.empty() ? "" : tokens[0].filename;
    
    std::vector<Token> output;
    int phase4_iters = 0;
    
    while (state.pos < state.tokens.size()) {
        if (state.tokens[state.pos].type == TokenType::END_OF_FILE) break;
        if (is_directive(state.tokens, state.pos)) {
            handle_directive(state, output);
        } else {
            Token t = consume(state);
            
            // Phase 6: String literal concatenation
            if (t.type == TokenType::STRING_LITERAL) {
                std::string prefix_t;
                std::string body = t.value;
                // Extract prefix
                if (body.size() >= 3 && body.substr(0, 2) == "u8" && body[2] == '"') {
                    prefix_t = "u8";
                    body = body.substr(2);
                } else if (body.size() >= 2 && (body[0] == 'u' || body[0] == 'U' || body[0] == 'L') && body[1] == '"') {
                    prefix_t = body.substr(0, 1);
                    body = body.substr(1);
                }
                
                while (true) {
                    size_t save_pos = state.pos;
                    skip_whitespace(state);
                    Token next = peek(state);
                    if (next.type == TokenType::STRING_LITERAL) {
                        consume(state);
                        std::string next_prefix;
                        std::string next_body = next.value;
                        if (next_body.size() >= 3 && next_body.substr(0, 2) == "u8" && next_body[2] == '"') {
                            next_prefix = "u8";
                            next_body = next_body.substr(2);
                        } else if (next_body.size() >= 2 && (next_body[0] == 'u' || next_body[0] == 'U' || next_body[0] == 'L') && next_body[1] == '"') {
                            next_prefix = next_body.substr(0, 1);
                            next_body = next_body.substr(1);
                        }
                        
                        // Determine resulting prefix (take the wider one)
                        if (next_prefix.empty()) {
                            // no prefix is fine
                        } else if (prefix_t.empty()) {
                            prefix_t = next_prefix;
                        }
                        // If both have prefixes, they should match (for now, just use first)
                        
                        std::string v1 = body.substr(0, body.size() - 1);
                        std::string v2 = next_body.substr(1);
                        body = v1 + v2;
                    } else {
                        state.pos = save_pos;
                        break;
                    }
                }
                
                t.value = prefix_t + body;
            }
            
            if (t.type == TokenType::IDENTIFIER) {
                if (t.value == "__LINE__") {
                    output.push_back({TokenType::NUMBER, std::to_string(t.line), t.filename, t.line, t.column});
                    continue;
                } else if (t.value == "__FILE__") {
                    output.push_back({TokenType::STRING_LITERAL, "\"" + t.filename + "\"", t.filename, t.line, t.column});
                    continue;
                }
                
                if (t.value == "__has_cpp_attribute" || t.value == "__has_c_attribute") {
                    skip_whitespace(state);
                    if (peek(state).value == "(") {
                        consume(state);
                        std::string attr_name;
                        while (peek(state).value != ")") {
                            Token at = consume(state);
                            if (at.type == TokenType::IDENTIFIER) {
                                if (!attr_name.empty()) attr_name += "::";
                                attr_name += at.value;
                            }
                        }
                        consume(state); // )
                        long long ver = lookup_attribute(attr_name);
                        output.push_back({TokenType::NUMBER, std::to_string(ver), state.filename, t.line, t.column});
                        continue;
                    }
                }
                
                if (t.value == "__has_include" || t.value == "__has_include_next") {
                    bool is_next = (t.value == "__has_include_next");
                    skip_whitespace(state);
                    if (peek(state).value == "(") {
                        consume(state);
                        std::string inc;
                        while (peek(state).value != ")") {
                            Token ht = consume(state);
                            if (ht.type == TokenType::STRING_LITERAL || ht.type == TokenType::HEADER_NAME) {
                                inc = ht.value.substr(1, ht.value.size() - 2);
                            }
                        }
                        consume(state); // )
                        bool found = is_next ? has_include_next(inc) : has_include(inc);
                        output.push_back({TokenType::NUMBER, found ? "1" : "0", state.filename, t.line, t.column});
                        continue;
                    }
                }
                
                if (t.value == "__has_embed") {
                    skip_whitespace(state);
                    if (peek(state).value == "(") {
                        consume(state);
                        std::string inc;
                        while (peek(state).value != ")") {
                            Token ht = consume(state);
                            if (ht.type == TokenType::STRING_LITERAL || ht.type == TokenType::HEADER_NAME) {
                                inc = ht.value.substr(1, ht.value.size() - 2);
                            }
                        }
                        consume(state); // )
                        bool found = has_include(inc);
                        output.push_back({TokenType::NUMBER, found ? "1" : "0", state.filename, t.line, t.column});
                        continue;
                    }
                }
                
                if (t.value == "__COUNTER__") {
                    output.push_back({TokenType::NUMBER, std::to_string(counter_value++), state.filename, t.line, t.column});
                    continue;
                }
                
                if (macros.count(t.value)) {
                    auto& m = macros[t.value];
                    std::set<std::string> expanding;
                    expanding.insert(t.value);
                    
                    if (!m.is_function_like) {
                        auto expanded = expand_tokens(m.replacement_list, expanding);
                        for (auto& et : expanded) {
                            if (et.type != TokenType::END_OF_FILE) {
                                if (et.value == "__LINE__") {
                                    output.push_back({TokenType::NUMBER, std::to_string(t.line), t.filename, t.line, t.column});
                                } else if (et.value == "__FILE__") {
                                    output.push_back({TokenType::STRING_LITERAL, "\"" + t.filename + "\"", t.filename, t.line, t.column});
                                } else {
                                    output.push_back(et);
                                }
                            }
                        }
                    } else {
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
                            // Pad missing variadic args with empty
                            if (m.is_variadic && args.size() < m.params.size()) {
                                args.push_back({});
                            }
                            consume(state); // )
                            
                            auto expanded = expand_function_like(m, args, expanding);
                            for (auto& et : expanded) {
                                if (et.type != TokenType::END_OF_FILE) {
                                    if (et.value == "__LINE__") {
                                        output.push_back({TokenType::NUMBER, std::to_string(t.line), t.filename, t.line, t.column});
                                    } else if (et.value == "__FILE__") {
                                        output.push_back({TokenType::STRING_LITERAL, "\"" + t.filename + "\"", t.filename, t.line, t.column});
                                    } else {
                                        output.push_back(et);
                                    }
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
