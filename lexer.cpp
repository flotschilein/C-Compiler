#include "lexer.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <set>
#include <stdexcept>

std::string Lexer::read_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string Lexer::phase1_2(const std::string& source) {
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

std::vector<Token> Lexer::tokenize(const std::string& source, const std::string& filename) {
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

        // Detect string/char literal prefixes: L, u, U, u8
        std::string prefix;
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
                        token.value += content + delimiter + '"';
                        i = check + 1;
                        col += 1 + delimiter.size() + 1;
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
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
            token.type = TokenType::IDENTIFIER;
            token.has_whitespace_before = !tokens.empty() && (tokens.back().type == TokenType::WHITESPACE || tokens.back().type == TokenType::NEWLINE);
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_' || source[i] == '$')) {
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
                while (i < source.size()) {
                    char nc = source[i];
                    if (std::isalnum(static_cast<unsigned char>(nc)) || nc == '.' || nc == '\'') {
                        if (nc == '\'') { token.value += source[i++]; col++; continue; }
                        token.value += source[i++];
                        col++;
                        if ((nc == 'p' || nc == 'P') && i < source.size() && (source[i] == '+' || source[i] == '-')) {
                            token.value += source[i++];
                            col++;
                        }
                    } else {
                        break;
                    }
                }
            } else if (c == '0' && i + 1 < source.size() && (source[i+1] == 'b' || source[i+1] == 'B')) {
                token.value += source[i++];
                col++;
                token.value += source[i++];
                col++;
                while (i < source.size() && ((source[i] >= '0' && source[i] <= '1') || source[i] == '\'')) {
                    if (source[i] == '\'') { token.value += source[i++]; col++; continue; }
                    token.value += source[i++];
                    col++;
                }
            } else {
                while (i < source.size()) {
                    char nc = source[i];
                    if (std::isalnum(static_cast<unsigned char>(nc)) || nc == '.' || nc == '\'') {
                        if (nc == '\'') { token.value += source[i++]; col++; continue; }
                        token.value += source[i++];
                        col++;
                        // After e/E/p/P in a pp-number, consume optional sign
                        if ((nc == 'e' || nc == 'E' || nc == 'p' || nc == 'P') &&
                            i < source.size() && (source[i] == '+' || source[i] == '-')) {
                            token.value += source[i++];
                            col++;
                        }
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
            "==", "!=", "<=", ">=", "<=>", "&&", "||", "^^",
            "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
            "<<=", ">>=", "->*", ".*",
            "%:", "<:", ":>", "<%", "%>",
            "..."
        };

        std::string three_char = two_char;
        if (i + 2 < source.size()) three_char += source[i+2];

        if (i + 2 < source.size() && multi_char.count(three_char)) {
            token.type = TokenType::PUNCTUATOR;
            token.value = three_char;
            i += 3;
            col += 3;
        } else if (i + 3 < source.size() && source[i] == '%' && source[i+1] == ':' &&
                   source[i+2] == '%' && source[i+3] == ':') {
            // %:%: digraph → ##
            token.type = TokenType::PREPROCESSING_OP;
            token.value = "##";
            i += 4;
            col += 4;
        } else if (c == '<' && i + 2 < source.size() && source[i+1] == ':' && source[i+2] == ':') {
            // <:: in C++: if not followed by : or >, treat as < + :: not <: + :
            if (i + 3 >= source.size() || (source[i+3] != ':' && source[i+3] != '>')) {
                token.type = TokenType::PUNCTUATOR;
                token.value = "<";
                i += 1;
                col += 1;
                tokens.push_back(token);
                continue;
            }
        } else if (multi_char.count(two_char)) {
            token.type = two_char == "##" ? TokenType::PREPROCESSING_OP : TokenType::PUNCTUATOR;
            token.value = two_char;
            // Map digraphs to their equivalent primary tokens
            if (two_char == "<:") token.value = "[";
            else if (two_char == ":>") token.value = "]";
            else if (two_char == "<%") token.value = "{";
            else if (two_char == "%>") token.value = "}";
            else if (two_char == "%:") token.value = "#";
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

std::vector<Token> Lexer::tokenize_file(const std::string& filename) {
    std::string source = read_file(filename);
    std::string processed_source = phase1_2(source);
    return tokenize(processed_source, filename);
}

std::vector<Token> Lexer::tokenize_string(const std::string& source, const std::string& filename) {
    std::string processed_source = phase1_2(source);
    return tokenize(processed_source, filename);
}
