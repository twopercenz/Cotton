#pragma once
#include <string>
#include <vector>

namespace cotton {

enum class TokenType {
    // Single-char
    LEFT_PAREN, RIGHT_PAREN,
    LEFT_BRACE, RIGHT_BRACE,
    LEFT_BRACKET, RIGHT_BRACKET,
    COMMA, DOT, SEMICOLON, COLON, PIPE, AT,
    
    // Operators
    PLUS, MINUS, STAR, SLASH, PERCENT, STAR_STAR,
    BANG, BANG_EQUAL,
    EQUAL, EQUAL_EQUAL,
    GREATER, GREATER_EQUAL,
    LESS, LESS_EQUAL,
    ARROW, FAT_ARROW,
    DOT_DOT, DOT_DOT_EQUAL,
    DOT_DOT_DOT,
    QUESTION, QUESTION_QUESTION, QUESTION_DOT,
    AMP, AMP_MUT, // & and &mut handled as tokens combination
    DOT_STAR, // not needed
    
    // Literals
    IDENTIFIER, NUMBER, STRING, TEMPLATE_STRING, MULTILINE_STRING,
    TRUE, FALSE, NIL, // nil for unit / None
    
    // Keywords
    LET, MUT, CONST, FN, STRUCT, ENUM, IMPL, TRAIT,
    IF, ELSE, FOR, IN, WHILE, LOOP, MATCH, BREAK, CONTINUE,
    RETURN, IMPORT, FROM, MODULE, PUB, AS, SELF, SELF_TYPE,
    AND, OR, NOT,
    ASYNC, AWAIT, SPAWN, CHANNEL, UNSAFE, BOX,
    OK, ERR, SOME, NONE,
    OUTER, // for labels? labels are identifier + colon
    
    NEWLINE,
    END_OF_FILE,
    UNKNOWN
};

struct Token {
    TokenType type = TokenType::UNKNOWN;
    std::string lexeme;
    int line = 0;
    int col = 0;
    // For template strings we need parts, but we will handle lexing as single token with raw
    Token() = default;
    Token(TokenType t, std::string l, int ln, int c) : type(t), lexeme(std::move(l)), line(ln), col(c) {}
};

std::string tokenTypeToString(TokenType t);

} // namespace cotton
