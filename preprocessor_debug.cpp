#include "preprocessor.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cstdlib>

preprocessor::preprocessor() {
    include_paths.push_back(".");
    include_next_index = 0;
    
    macros["__STDC_HOSTED__"] = {false, {}, false, {{TokenType::NUMBER, "1", "", 0, 0}}};
    macros["__cplusplus"] = {false, {}, false, {{TokenType::NUMBER, "202601L", "", 0, 0}}};
    macros["__STDC__"] = {false, {}, false, {{TokenType::NUMBER, "1", "", 0, 0}}};
    macros["__STDC_VERSION__"] = {false, {}, false, {{TokenType::NUMBER, "202311L", "", 0, 0}}};
    
    macros["__has_cpp_attribute"] = {true, {"attr"}, false, {}};
    macros["__has_c_attribute"] = {true, {"attr"}, false, {}};
    macros["__has_include"] = {true, {"header"}, false, {}};
    macros["__has_include_next"] = {true, {"header"}, false, {}};
    
    std::time_t now = std::time(nullptr);
    char buf[12];
    std::strftime(buf, sizeof(buf), "%b %d %Y", std::localtime(&now));
    std::string date = buf;
    if (date[4] == '0') date[4] = ' ';
    macros["__DATE__"] = {false, {}, false, {{TokenType::STRING_LITERAL, "\"" + date + "\"", "", 0, 0}}};
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now));
    macros["__TIME__"] = {false, {}, false, {{TokenType::STRING_LITERAL, "\"" + std::string(buf) + "\"", "", 0, 0}}};
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
        // Phase 1: Universal character names
        if (source[i] == '\\' && i + 1 < source.size()) {
            if (source[i+1] == 'u' && i + 5 < source.size()) {
                std::string hex = source.substr(i+2, 4);
                bool valid = true;
                for (char c : hex) {
                    if (!std::isxdigit(static_cast<unsigned char>(c))) { valid = false; break; }
                }
                if (valid) {
                    unsigned long cp = std::strtoul(hex.c_str(), nullptr, 16);
                    if (cp < 0x80) {
                        result += (char)cp;
                    } else if (cp < 0x800) {
                        result += (char)(0xC0 | (cp >> 6));
                        result += (char)(0x80 | (cp & 0x3F));
                    } else {
                        result += (char)(0xE0 | (cp >> 12));
                        result += (char)(0x80 | ((cp >> 6) & 0x3F));
                        result += (char)(0x80 | (cp & 0x3F));
                    }
                    i += 5;
                    continue;
                }
            } else if (source[i+1] == 'U' && i + 9 < source.size()) {
                std::string hex = source.substr(i+2, 8);
                bool valid = true;
                for (char c : hex) {
                    if (!std::isxdigit(static_cast<unsigned char>(c))) { valid = false; break; }
                }
                if (valid) {
                    unsigned long cp = std::strtoul(hex.c_str(), nullptr, 16);
                    if (cp < 0x80) {
                        result += (char)cp;
                    } else if (cp < 0x800) {
                        result += (char)(0xC0 | (cp >> 6));
                        result += (char)(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        result += (char)(0xE0 | (cp >> 12));
                        result += (char)(0x80 | ((cp >> 6) & 0x3F));
                        result += (char)(0x80 | (cp & 0x3F));
                    } else {
                        result += (char)(0xF0 | (cp >> 18));
                        result += (char)(0x80 | ((cp >> 12) & 0x3F));
                        result += (char)(0x80 | ((cp >> 6) & 0x3F));
                        result += (char)(0x80 | (cp & 0x3F));
                    }
                    i += 9;
                    continue;
                }
            }
        }
        
        // Phase 2: Line splicing (backslash-newline)
        if (source[i] == '\\' && i + 1 < source.size()) {
            if (source[i+1] == '\n') {
                i++;
                continue;
            } else if (source[i+1] == '\r' && i + 2 < source.size() && source[i+2] == '\n') {
                i += 2;
                continue;
            }
        }
        result += source[i];
    }
    
    if (!result.empty() && result.back() != '\n') {
        result += '\n';
    }
    
    return result;
}

std::vector<Token> preprocessor::tokenize(const std::string& source, const std::string& filename) {
    std::vector<Token> tokens;
    int line = 1;
    int col = 1;
    
    for (size_t i = 0; i < source.size(); ) { if (i % 1000 == 0) std::cerr << "DEBUG tokenize i=" << i << " c=" << source[i] << "
"; if (i > 100000) { std::cerr << "INFINITE LOOP in tokenizer
"; break; }
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
                i += 2;
                while (i < source.size() && source[i] != '\n') i++;
                token.type = TokenType::WHITESPACE;
                token.value = " ";
                token.has_whitespace_before = true;
                tokens.push_back(token);
                continue;
            } else if (source[i+1] == '*') {
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
        
        // Character/user-defined string literal prefix
        std::string prefix;
        if ((c == 'u' || c == 'U' || c == 'L') && i + 1 < source.size()) {
            if (c == 'u' && i + 2 < source.size() && source[i+1] == '8') {
                char next = source[i+2];
                if (next == '"' || next == '\'' || next == 'R') {
                    prefix = "u8";
                    i += 2;
                    col += 2;
                    c = source[i];
                }
            }
            if (prefix.empty()) {
                char next = source[i+1];
                if (next == '"' || next == '\'' || next == 'R') {
                    prefix = std::string(1, c);
                    i++;
                    col++;
                    c = source[i];
                }
            }
        }
        
        // Raw string literal: R"delimiter(content)delimiter"
        if (c == 'R' && i + 1 < source.size() && source[i+1] == '"') {
            // Actually, R is part of prefix, and the next char should be '"' or '(' after delimiter
            // But we already consumed the prefix, so c should be '"'
            // Let me rework this logic
        }
        
        // Raw string literal handling (after prefix consumption)
        // If we had u8R, uR, UR, LR, or just R and next char is '"'
        // Actually, we need to re-check. Let me handle raw strings first then fall through.
        
        // Reset and properly handle
        // We'll re-approach this: scan for prefixes including R
        size_t saved_i = i;
        int saved_col = col;
        prefix.clear();
        
        // Check for raw string R
        if (source[i] == 'R' && i + 1 < source.size() && source[i+1] == '"') {
            // Raw string without prefix: R"(...)"
            prefix = "";
            // Don't consume anything, handle below
        } else if (source[i] == 'R' && i + 2 < source.size() && source[i+1] == '"' && source[i+2] == '(') {
            // This case is handled below
        }
        
        // Let me redo this properly from the current position
        // Reset to saved_i
        i = saved_i;
        col = saved_col;
        c = source[i];
        prefix.clear();
        
        // Check for prefix letters
        if ((c == 'u' || c == 'U' || c == 'L') && i + 1 < source.size()) {
            char next = source[i+1];
            if (c == 'u' && i + 2 < source.size() && source[i+1] == '8') {
                char next2 = source[i+2];
                if (next2 == '"' || next2 == '\'' || next2 == 'R') {
                    prefix = "u8";
                    i += 2;
                    col += 2;
                    c = source[i];
                }
            }
            if (prefix.empty()) {
                if (next == '"' || next == '\'' || next == 'R') {
                    prefix = std::string(1, c);
                    i++;
                    col++;
                    c = source[i];
                }
            }
        }
        
        // Now handle raw string with any prefix: [prefix]R"delimiter(content)delimiter"
        if (c == 'R' && i + 1 < source.size() && source[i+1] == '"') {
            // Raw string literal
            token.type = TokenType::STRING_LITERAL;
            token.has_whitespace_before = !tokens.empty() && (tokens.back().type == TokenType::WHITESPACE || tokens.back().type == TokenType::NEWLINE);
            
            // Gather delimiter after R"
            i += 2; // skip R and "
            col += 2;
            std::string delimiter;
            while (i < source.size() && source[i] != '(') {
                delimiter += source[i];
                i++;
                col++;
            }
            if (i >= source.size()) {
                throw std::runtime_error("Unterminated raw string literal");
            }
            i++; // skip (
            col++;
            
            // Build the full token value
            token.value = prefix + "R\"" + delimiter + "(";
            
            // Read until )delimiter"
            std::string content;
            while (i < source.size()) {
                if (source[i] == ')') {
                    // Check if followed by delimiter and "
                    size_t check = i + 1;
                    bool match = true;
                    for (char d : delimiter) {
                        if (check >= source.size() || source[check] != d) { match = false; break; }
                        check++;
                    }
                    if (match && check < source.size() && source[check] == '"') {
                        content += ')';
                        token.value += content;
                        i = check + 1; // skip past final "
                        col += (check - i + 1); // approximate
                        tokens.push_back(token);
                        break;
                    }
                }
                if (source[i] == '\n') line++;
                content += source[i];
                i++;
                col++;
            }
            if (i >= source.size() && token.value.back() != '"') {
                throw std::runtime_error("Unterminated raw string literal");
            }
            continue;
        }
        
        // String literals: "..." or prefix"..."
        if (c == '"') {
            token.type = TokenType::STRING_LITERAL;
            token.has_whitespace_before = !tokens.empty() && (tokens.back().type == TokenType::WHITESPACE || tokens.back().type == TokenType::NEWLINE);
            token.value = prefix;
            token.value += source[i++];
            col++;
            while (i < source.size() && source[i] != '"') {
                if (source[i] == '\\' && i + 1 < source.size()) {
                    token.value += source[i++];
                    col++;
                }
                token.value += source[i++];
                col++;
            }
            if (i < source.size()) {
                token.value += source[i++];
                col++;
            }
            tokens.push_back(token);
            continue;
        }
        
        // Character literals: '...' or prefix'...'
        if (c == '\'') {
            token.type = TokenType::CHARACTER_LITERAL;
            token.has_whitespace_before = !tokens.empty() && (tokens.back().type == TokenType::WHITESPACE || tokens.back().type == TokenType::NEWLINE);
            token.value = prefix;
            token.value += source[i++];
            col++;
            while (i < source.size() && source[i] != '\'') {
                if (source[i] == '\\' && i + 1 < source.size()) {
                    token.value += source[i++];
                    col++;
                }
                token.value += source[i++];
                col++;
            }
            if (i < source.size()) {
                token.value += source[i++];
                col++;
            }
            tokens.push_back(token);
            continue;
        }
        
        // Identifiers and keywords
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
        
        // Numbers (including digit separators)
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[i+1])))) {
            token.type = TokenType::NUMBER;
            token.has_whitespace_before = !tokens.empty() && (tokens.back().type == TokenType::WHITESPACE || tokens.back().type == TokenType::NEWLINE);
            if (c == '0' && i + 1 < source.size() && (source[i+1] == 'x' || source[i+1] == 'X')) {
                token.value += source[i++];
                col++;
                token.value += source[i++];
                col++;
                while (i < source.size() && (std::isxdigit(static_cast<unsigned char>(source[i])) || source[i] == '\'')) {
                    if (source[i] == '\'') { i++; col++; continue; }
                    token.value += source[i++];
                    col++;
                }
            } else {
                while (i < source.size()) {
                    char nc = source[i];
                    if (std::isalnum(static_cast<unsigned char>(nc)) || nc == '.' || nc == '\'') {
                        if (nc == '\'') { i++; col++; continue; }
                        token.value += source[i++];
                        col++;
                    } else {
                        break;
                    }
                }
            }
            tokens.push_back(token);
            continue;
        }
        
        // Simple punctuators and preprocessing operators
        token.has_whitespace_before = !tokens.empty() && (tokens.back().type == TokenType::WHITESPACE || tokens.back().type == TokenType::NEWLINE);
        
        // Check for multi-character punctuators
        std::string two_char = std::string(1, c);
        if (i + 1 < source.size()) two_char += source[i+1];
        
        static const std::set<std::string> multi_char = {
            "##", "<<", ">>", "::", "->", "++", "--",
            "==", "!=", "<=", ">=", "&&", "||",
            "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
            "##", "..", "..."
        };
        
        std::string three_char = two_char;
        if (i + 2 < source.size()) three_char += source[i+2];
        
        if (multi_char.count(three_char)) {
            token.type = TokenType::PUNCTUATOR;
            token.value = three_char;
            i += 3;
            col += 3;
        } else if (multi_char.count(two_char)) {
            token.type = token.value == "##" ? TokenType::PREPROCESSING_OP : TokenType::PUNCTUATOR;
            token.value = two_char;
            i += 2;
            col += 2;
        } else {
            token.type = TokenType::PUNCTUATOR;
            token.value = c;
            i++;
            col++;
        }
        tokens.push_back(token);
    }
    
    tokens.push_back({TokenType::END_OF_FILE, "", filename, line, col});
    return tokens;
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
    
    skip_whitespace(state);
    Token filename_token = peek(state);
    std::string new_filename;
    if (filename_token.type == TokenType::STRING_LITERAL) {
        consume(state);
        new_filename = filename_token.value.substr(1, filename_token.value.size() - 2);
    }
    
    // Update the line numbers of subsequent tokens
    int delta = new_line - lineno.line;
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
        // Skip this file on subsequent includes - current implementation just ignores
        return;
    }
    
    // Other pragmas are silently ignored
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
        } else if (dir == "elifdef") {
            handle_elifdef(state, output, false);
        } else if (dir == "elifndef") {
            handle_elifdef(state, output, true);
        } else if (dir == "elif") {
            if (!handle_elif(state, output)) {
                // Unmatched #elif - consume rest of line
                while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
                    consume(state);
                }
            }
        } else if (dir == "else" || dir == "endif") {
            // Unmatched #else or #endif
            while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
                consume(state);
            }
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

void preprocessor::handle_elifdef(PreprocessorState& state, std::vector<Token>& output, bool negate) {
    consume(state); // #
    skip_whitespace(state);
    consume(state); // elifdef/elifndef
    skip_whitespace(state);
    
    Token name = consume(state);
    bool exists = macros.count(name.value) > 0;
    bool condition = negate ? !exists : exists;
    
    // If condition is false, skip this branch
    if (!condition) {
        // Consume rest of line
        while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
            consume(state);
        }
        if (peek(state).type == TokenType::NEWLINE) {
            consume(state);
        }
        skip_failed_branch(state, output);
    }
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

bool preprocessor::evaluate_condition(const std::vector<Token>& tokens) {
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
                while (i < tokens.size() && tokens[i].value != ")") i++;
                processed.push_back({TokenType::NUMBER, "0", "", 0, 0});
            }
            continue;
        }
        
        if (tokens[i].type == TokenType::IDENTIFIER && tokens[i].value != "true" && tokens[i].value != "false") {
            // Unbound identifier resolves to 0
            processed.push_back({TokenType::NUMBER, "0", "", 0, 0});
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
    
    return parse_or() != 0;
}

void preprocessor::skip_failed_branch(PreprocessorState& state, std::vector<Token>& output) {
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
                    state.pos = next_pos;
                    consume(state); // consume 'else'
                    return;
                } else if (dir == "elif" && depth == 1) {
                    state.pos = next_pos;
                    consume(state); // consume 'elif'
                    std::vector<Token> condition_tokens;
                    while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
                        condition_tokens.push_back(consume(state));
                    }
                    if (evaluate_condition(condition_tokens)) {
                        return;
                    }
                } else if (dir == "elifdef" && depth == 1) {
                    state.pos = next_pos;
                    consume(state); // consume 'elifdef'
                    skip_whitespace(state);
                    Token name = consume(state);
                    bool exists = macros.count(name.value) > 0;
                    if (exists) {
                        while (peek(state).type != TokenType::NEWLINE && peek(state).type != TokenType::END_OF_FILE) {
                            consume(state);
                        }
                        return;
                    }
                } else if (dir == "elifndef" && depth == 1) {
                    state.pos = next_pos;
                    consume(state); // consume 'elifndef'
                    skip_whitespace(state);
                    Token name = consume(state);
                    bool exists = macros.count(name.value) > 0;
                    if (!exists) {
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
                    check++; // skip )
                    
                    i = check; // advance past the entire invocation
                    
                    auto expanded = expand_function_like(m, args, expanding);
                    for (auto& et : expanded) {
                        result.push_back(et);
                    }
                    continue;
                }
            }
            
            if (!m.is_function_like) {
                std::set<std::string> new_expanding = expanding;
                new_expanding.insert(t.value);
                auto further = expand_tokens(m.replacement_list, new_expanding);
                for (auto& ft : further) {
                    result.push_back(ft);
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
                    // Output the pragma as a string literal
                    result.push_back({TokenType::STRING_LITERAL, "\"" + unescaped + "\"", t.filename, t.line, t.column});
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
    
    for (size_t ri = 0; ri < m.replacement_list.size(); ++ri) {
        if (m.replacement_list[ri].value == "#" && ri + 1 < m.replacement_list.size()) {
            for (size_t pi = 0; pi < m.params.size(); ++pi) {
                if (m.replacement_list[ri+1].value == m.params[pi]) {
                    stringify_params.insert(pi);
                }
            }
        }
        if (ri > 0 && m.replacement_list[ri].value == "##") {
            for (size_t pi = 0; pi < m.params.size(); ++pi) {
                if (m.replacement_list[ri-1].value == m.params[pi]) {
                    paste_left_params.insert(pi);
                }
            }
        }
        if (ri + 1 < m.replacement_list.size() && m.replacement_list[ri+1].value == "##") {
            for (size_t pi = 0; pi < m.params.size(); ++pi) {
                if (m.replacement_list[ri].value == m.params[pi]) {
                    paste_right_params.insert(pi);
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
        const auto& rt = m.replacement_list[ri];
        
        // Stringification #
        if (rt.value == "#" && ri + 1 < m.replacement_list.size()) {
            const auto& next_rt = m.replacement_list[ri+1];
            bool stringified = false;
            for (size_t i = 0; i < m.params.size(); ++i) {
                if (next_rt.value == m.params[i]) {
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
                    ri++;
                    stringified = true;
                    break;
                }
            }
            if (stringified) continue;
        }
        
        // Token Pasting ##
        if (ri + 1 < m.replacement_list.size() && m.replacement_list[ri+1].value == "##") {
            Token left = rt;
            for (size_t i = 0; i < m.params.size(); ++i) {
                if (left.value == m.params[i] && i < args.size() && !args[i].empty()) {
                    left = args[i][0];
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
                result.push_back({left.type, left.value + right.value, rt.filename, left.line, left.column});
            } else {
                result.push_back(left);
            }
            continue;
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
    
    while (state.pos < state.tokens.size()) {
            static int loop_count = 0; if (++loop_count % 1000 == 0) std::cerr << "DEBUG run_phases_4 pos=" << state.pos << "/" << state.tokens.size() << "
"; if (loop_count > 100000) { std::cerr << "INFINITE LOOP in run_phases_4
"; break; }
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
                        skip_whitespace(state);
                        Token attr = consume(state);
                        skip_whitespace(state);
                        consume(state); // )
                        output.push_back({TokenType::NUMBER, "0", state.filename, t.line, t.column});
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
                
                if (macros.count(t.value)) {
                    auto& m = macros[t.value];
                    std::set<std::string> expanding;
                    expanding.insert(t.value);
                    
                    if (!m.is_function_like) {
                        auto expanded = expand_tokens(m.replacement_list, expanding);
                        for (auto& et : expanded) {
                            if (et.type != TokenType::END_OF_FILE) {
                                output.push_back(et);
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
                            consume(state); // )
                            
                            auto expanded = expand_function_like(m, args, expanding);
                            for (auto& et : expanded) {
                                if (et.type != TokenType::END_OF_FILE) {
                                    output.push_back(et);
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
