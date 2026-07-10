#include "lexer.h"
#include <unordered_map>
#include <cctype>

namespace cotton {

std::string tokenTypeToString(TokenType t) {
    switch(t){
        case TokenType::LEFT_PAREN: return "LEFT_PAREN";
        case TokenType::RIGHT_PAREN: return "RIGHT_PAREN";
        case TokenType::LEFT_BRACE: return "LEFT_BRACE";
        case TokenType::RIGHT_BRACE: return "RIGHT_BRACE";
        case TokenType::LEFT_BRACKET: return "LEFT_BRACKET";
        case TokenType::RIGHT_BRACKET: return "RIGHT_BRACKET";
        case TokenType::COMMA: return "COMMA";
        case TokenType::DOT: return "DOT";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::COLON: return "COLON";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::STAR: return "STAR";
        case TokenType::SLASH: return "SLASH";
        case TokenType::PERCENT: return "PERCENT";
        case TokenType::STAR_STAR: return "STAR_STAR";
        case TokenType::EQUAL: return "EQUAL";
        case TokenType::EQUAL_EQUAL: return "EQUAL_EQUAL";
        case TokenType::BANG_EQUAL: return "BANG_EQUAL";
        case TokenType::GREATER: return "GREATER";
        case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";
        case TokenType::LESS: return "LESS";
        case TokenType::LESS_EQUAL: return "LESS_EQUAL";
        case TokenType::ARROW: return "ARROW";
        case TokenType::FAT_ARROW: return "FAT_ARROW";
        case TokenType::DOT_DOT: return "DOT_DOT";
        case TokenType::DOT_DOT_EQUAL: return "DOT_DOT_EQUAL";
        case TokenType::DOT_DOT_DOT: return "DOT_DOT_DOT";
        case TokenType::QUESTION: return "QUESTION";
        case TokenType::QUESTION_QUESTION: return "QUESTION_QUESTION";
        case TokenType::QUESTION_DOT: return "QUESTION_DOT";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::STRING: return "STRING";
        case TokenType::TEMPLATE_STRING: return "TEMPLATE_STRING";
        case TokenType::MULTILINE_STRING: return "MULTILINE_STRING";
        default: return "TOKEN";
    }
}

static std::unordered_map<std::string, TokenType> keywords = {
    {"let", TokenType::LET},
    {"mut", TokenType::MUT},
    {"const", TokenType::CONST},
    {"fn", TokenType::FN},
    {"struct", TokenType::STRUCT},
    {"enum", TokenType::ENUM},
    {"impl", TokenType::IMPL},
    {"trait", TokenType::TRAIT},
    {"if", TokenType::IF},
    {"else", TokenType::ELSE},
    {"for", TokenType::FOR},
    {"in", TokenType::IN},
    {"while", TokenType::WHILE},
    {"loop", TokenType::LOOP},
    {"match", TokenType::MATCH},
    {"break", TokenType::BREAK},
    {"continue", TokenType::CONTINUE},
    {"return", TokenType::RETURN},
    {"import", TokenType::IMPORT},
    {"from", TokenType::FROM},
    {"module", TokenType::MODULE},
    {"pub", TokenType::PUB},
    {"as", TokenType::AS},
    {"self", TokenType::SELF},
    {"Self", TokenType::SELF_TYPE},
    {"and", TokenType::AND},
    {"or", TokenType::OR},
    {"not", TokenType::NOT},
    {"async", TokenType::ASYNC},
    {"await", TokenType::AWAIT},
    {"spawn", TokenType::SPAWN},
    {"channel", TokenType::CHANNEL},
    {"unsafe", TokenType::UNSAFE},
    {"Box", TokenType::BOX},
    {"true", TokenType::TRUE},
    {"false", TokenType::FALSE},
    {"None", TokenType::NONE},
    {"Some", TokenType::SOME},
    {"Ok", TokenType::OK},
    {"Err", TokenType::ERR},
};

Lexer::Lexer(std::string source) : src(std::move(source)) {}

char Lexer::peek(int offset) const {
    size_t p = pos + offset;
    if (p >= src.size()) return '\0';
    return src[p];
}
char Lexer::advance() {
    char c = src[pos++];
    if (c == '\n') { line++; col = 1; } else col++;
    return c;
}
bool Lexer::match(char expected) {
    if (isAtEnd()) return false;
    if (src[pos] != expected) return false;
    pos++; col++;
    return true;
}
bool Lexer::isAtEnd() const { return pos >= src.size(); }
bool Lexer::isAlpha(char c) const { return std::isalpha((unsigned char)c) || c == '_' ; }
bool Lexer::isDigit(char c) const { return std::isdigit((unsigned char)c); }
bool Lexer::isAlphaNum(char c) const { return isAlpha(c) || isDigit(c); }

void Lexer::addToken(TokenType type, const std::string& lexeme) {
    tokens.emplace_back(type, lexeme, line, col - (int)lexeme.size());
}
void Lexer::addToken(TokenType type, std::string lexeme, int ln, int c) {
    tokens.emplace_back(type, std::move(lexeme), ln, c);
}

void Lexer::skipLineComment() {
    while (!isAtEnd() && peek() != '\n') advance();
}

void Lexer::skipBlockComment() {
    while (!isAtEnd()) {
        if (peek() == '*' && peek(1) == '/') { advance(); advance(); break; }
        advance();
    }
}

void Lexer::scanString(char quote) {
    std::string val;
    int startLine = line;
    int startCol = col;
    while (!isAtEnd() && peek() != quote) {
        if (peek() == '\\') {
            advance();
            char esc = advance();
            switch(esc){
                case 'n': val.push_back('\n'); break;
                case 'r': val.push_back('\r'); break;
                case 't': val.push_back('\t'); break;
                case '\\': val.push_back('\\'); break;
                case '"': val.push_back('"'); break;
                case '\'': val.push_back('\''); break;
                default: val.push_back(esc); break;
            }
        } else {
            if (peek() == '\n') { line++; col=1; }
            val.push_back(advance());
        }
    }
    if (!isAtEnd()) advance(); // closing quote
    addToken(TokenType::STRING, val, startLine, startCol);
}

void Lexer::scanTemplateString() {
    // backtick string with ${} interpolation; we keep raw with separators
    std::string raw;
    int startLine = line;
    int startCol = col;
    raw.push_back('`'); // we'll parse interpolation in parser? For simplicity lex as raw content inclusive
    // Actually we lex whole template as one token with raw content containing ${}
    // The interpreter will handle interpolation by parsing inside? We'll store raw inner content
    std::string inner;
    while (!isAtEnd() && peek() != '`') {
        if (peek() == '$' && peek(1) == '{') {
            inner += "${"; advance(); advance();
            // we need to capture balanced braces? We'll include up to }
            int depth = 1;
            std::string expr;
            while (!isAtEnd() && depth > 0) {
                char c = peek();
                if (c == '{') depth++;
                else if (c == '}') { depth--; if (depth==0) break; }
                expr.push_back(c);
                advance();
            }
            inner += expr;
            inner += "}";
            if (!isAtEnd() && peek() == '}') { advance(); }
        } else if (peek() == '\\' && peek(1) == '`') {
            advance(); inner.push_back(advance());
        } else {
            inner.push_back(advance());
        }
    }
    if (!isAtEnd()) advance(); // closing `
    // Store inner as lexeme; parser will know it's template
    addToken(TokenType::TEMPLATE_STRING, inner, startLine, startCol);
}

void Lexer::scanMultilineString() {
    int startLine = line;
    int startCol = col;
    // we already consumed """, now collect until """
    std::string val;
    while (!isAtEnd()) {
        if (peek() == '"' && peek(1) == '"' && peek(2) == '"') {
            advance(); advance(); advance();
            break;
        }
        val.push_back(advance());
    }
    addToken(TokenType::MULTILINE_STRING, val, startLine, startCol);
}

void Lexer::scanNumber() {
    std::string num;
    int startCol = col-1;
    int startLine = line;
    // we've already advanced first char, need to track. We'll reconstruct via pos
    // Actually caller should have collected. Simpler: go back one?
    // Let's rewind one for simplicity: we called advance already for first digit, but num empty. We'll collect from pos-1
    pos--; col--;
    while (!isAtEnd() && (isDigit(peek()) || peek() == '.' || peek() == '_' || peek() == 'e' || peek() == 'E' || peek() == '+' || peek() == '-' )) {
        // need to avoid consuming .. as part of number. If we see .. break
        if (peek() == '.' && peek(1) == '.') break;
        // if + - not after e
        if ((peek() == '+' || peek() == '-') && !(num.size()>0 && (num.back()=='e' || num.back()=='E'))) {
            // if it's sign of number? break if not exponent
            // allow leading - handled by parser, so break here if num non-empty and not exponent
            if (!num.empty()) break;
        }
        char c = advance();
        if (c != '_') num.push_back(c);
    }
    addToken(TokenType::NUMBER, num, startLine, startCol);
}

void Lexer::scanIdentifier() {
    std::string id;
    int startCol = col-1;
    int startLine = line;
    pos--; col--;
    while (!isAtEnd() && isAlphaNum(peek())) {
        id.push_back(advance());
    }
    auto it = keywords.find(id);
    if (it != keywords.end()) {
        addToken(it->second, id, startLine, startCol);
    } else {
        addToken(TokenType::IDENTIFIER, id, startLine, startCol);
    }
}

void Lexer::scanToken() {
    char c = advance();
    switch(c) {
        case '(': addToken(TokenType::LEFT_PAREN, "("); parenDepth++; break;
        case ')': addToken(TokenType::RIGHT_PAREN, ")"); parenDepth--; break;
        case '{': addToken(TokenType::LEFT_BRACE, "{"); braceDepth++; break;
        case '}': addToken(TokenType::RIGHT_BRACE, "}"); braceDepth--; break;
        case '[': addToken(TokenType::LEFT_BRACKET, "["); bracketDepth++; break;
        case ']': addToken(TokenType::RIGHT_BRACKET, "]"); bracketDepth--; break;
        case ',': addToken(TokenType::COMMA, ","); break;
        case ';': addToken(TokenType::SEMICOLON, ";"); break;
        case ':': addToken(TokenType::COLON, ":"); break;
        case '|': addToken(TokenType::PIPE, "|"); break;
        case '@': addToken(TokenType::AT, "@"); break;
        case '+':
            if (match('=')) addToken(TokenType::PLUS, "+="); // we reuse PLUS but lexeme "+="
            else addToken(TokenType::PLUS, "+");
            break;
        case '-':
            if (match('>')) addToken(TokenType::ARROW, "->");
            else if (match('=')) addToken(TokenType::PLUS, "-="); // treat as PLUS for simplicity (handle in parser)
            else addToken(TokenType::MINUS, "-");
            break;
        case '*':
            if (match('*')) addToken(TokenType::STAR_STAR, "**");
            else addToken(TokenType::STAR, "*");
            break;
        case '/':
            if (peek() == '/') { advance(); skipLineComment(); }
            else if (peek() == '*') { advance(); skipBlockComment(); }
            else addToken(TokenType::SLASH, "/");
            break;
        case '%': addToken(TokenType::PERCENT, "%"); break;
        case '.':
            if (peek() == '.' && peek(1) == '.') { advance(); advance(); addToken(TokenType::DOT_DOT_DOT, "..."); }
            else if (peek() == '.' && peek(1) == '=') { advance(); advance(); addToken(TokenType::DOT_DOT_EQUAL, "..="); }
            else if (peek() == '.') { advance(); addToken(TokenType::DOT_DOT, ".."); }
            else addToken(TokenType::DOT, ".");
            break;
        case '=':
            if (match('=')) addToken(TokenType::EQUAL_EQUAL, "==");
            else if (match('>')) addToken(TokenType::FAT_ARROW, "=>");
            else addToken(TokenType::EQUAL, "=");
            break;
        case '!':
            if (match('=')) addToken(TokenType::BANG_EQUAL, "!=");
            else addToken(TokenType::BANG, "!");
            break;
        case '<':
            if (match('=')) addToken(TokenType::LESS_EQUAL, "<=");
            else addToken(TokenType::LESS, "<");
            break;
        case '>':
            if (match('=')) addToken(TokenType::GREATER_EQUAL, ">=");
            else addToken(TokenType::GREATER, ">");
            break;
        case '?':
            if (match('?')) addToken(TokenType::QUESTION_QUESTION, "??");
            else if (match('.')) addToken(TokenType::QUESTION_DOT, "?.");
            else addToken(TokenType::QUESTION, "?");
            break;
        case '&':
            addToken(TokenType::AMP, "&");
            break;
        case '"':
            if (peek() == '"' && peek(1) == '"') { advance(); advance(); scanMultilineString(); }
            else { scanString('"'); }
            break;
        case '\'':
            scanString('\'');
            break;
        case '`':
            scanTemplateString();
            break;
        case ' ': case '\r': case '\t':
            break;
        case '\n': {
            // only emit NEWLINE if not inside parens/brackets/braces
            if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
                addToken(TokenType::NEWLINE, "\n");
            }
            break;
        }
        default:
            if (isDigit(c)) { scanNumber(); }
            else if (isAlpha(c)) { scanIdentifier(); }
            else { addToken(TokenType::UNKNOWN, std::string(1,c)); }
            break;
    }
}

std::vector<Token> Lexer::tokenize() {
    while (!isAtEnd()) {
        scanToken();
    }
    tokens.emplace_back(TokenType::END_OF_FILE, "", line, col);
    return tokens;
}

} // namespace cotton
