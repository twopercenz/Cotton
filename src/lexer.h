#pragma once
#include "token.h"
#include <string>
#include <vector>

namespace cotton {

class Lexer {
public:
    explicit Lexer(std::string source);
    std::vector<Token> tokenize();

private:
    std::string src;
    size_t pos = 0;
    int line = 1;
    int col = 1;
    int parenDepth = 0, bracketDepth = 0, braceDepth = 0;
    std::vector<Token> tokens;

    char peek(int offset = 0) const;
    char advance();
    bool match(char expected);
    bool isAtEnd() const;
    void addToken(TokenType type, const std::string& lexeme = "");
    void addToken(TokenType type, std::string lexeme, int line, int col);

    void scanToken();
    void scanString(char quote);
    void scanTemplateString(); // backtick
    void scanMultilineString(); // """
    void scanNumber();
    void scanIdentifier();
    void skipLineComment();
    void skipBlockComment();

    bool isAlpha(char c) const;
    bool isDigit(char c) const;
    bool isAlphaNum(char c) const;
};

} // namespace cotton
