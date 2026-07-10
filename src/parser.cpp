#include "parser.h"
#include "lexer.h"
#include <iostream>
#include <sstream>
#include <cctype>

namespace cotton {

Parser::Parser(std::vector<Token> t) : tokens(std::move(t)) {}

Token Parser::peek() const { return tokens[current]; }
Token Parser::previous() const { return tokens[current>0?current-1:0]; }
Token Parser::peekNext() const { if (current+1 < tokens.size()) return tokens[current+1]; return tokens.back(); }
bool Parser::isAtEnd() const { return peek().type == TokenType::END_OF_FILE; }
bool Parser::check(TokenType type) const { if (isAtEnd()) return false; return peek().type == type; }
bool Parser::checkNext(TokenType type) const { if (current+1>=tokens.size()) return false; return tokens[current+1].type == type; }

Token Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}
bool Parser::match(TokenType type) {
    if (check(type)) { advance(); return true; }
    return false;
}
bool Parser::match(std::vector<TokenType> types) {
    for (auto t: types) if (check(t)) { advance(); return true; }
    return false;
}
void Parser::skipNewlines() {
    while (check(TokenType::NEWLINE) || check(TokenType::SEMICOLON)) advance();
}

Token Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    error(peek(), message);
    // return dummy
    return Token(type, "", peek().line, peek().col);
}
void Parser::error(const Token& tok, const std::string& msg) {
    hadError = true;
    std::cerr << "[Parse Error] line " << tok.line << " col " << tok.col << " at '" << tok.lexeme << "': " << msg << "\n";
}
void Parser::synchronize() {
    advance();
    while (!isAtEnd()) {
        if (previous().type == TokenType::SEMICOLON || previous().type == TokenType::NEWLINE) return;
        switch(peek().type) {
            case TokenType::FN: case TokenType::LET: case TokenType::CONST:
            case TokenType::STRUCT: case TokenType::ENUM: case TokenType::IMPL:
            case TokenType::FOR: case TokenType::IF: case TokenType::WHILE:
            case TokenType::RETURN: case TokenType::IMPORT: return;
            default: break;
        }
        advance();
    }
}

bool Parser::isLabel() {
    if (check(TokenType::IDENTIFIER) && checkNext(TokenType::COLON)) {
        // need third token to be FOR, WHILE, LOOP
        if (current+2 < tokens.size()) {
            auto t = tokens[current+2].type;
            if (t==TokenType::FOR || t==TokenType::WHILE || t==TokenType::LOOP) return true;
        }
    }
    return false;
}
std::optional<std::string> Parser::parseOptionalLabel() {
    skipNewlines();
    if (isLabel()) {
        std::string lbl = advance().lexeme; // identifier
        consume(TokenType::COLON, "expected ':' after label");
        return lbl;
    }
    return std::nullopt;
}

std::string Parser::parseTypeName() {
    // parse type name including generics like [Point] or (Point, Point) etc
    // For simplicity, collect tokens until we hit delimiter that ends type: , ) } { newline etc but handle brackets.
    // We'll parse raw string for now.
    if (isAtEnd()) return "";
    // tuple type (A, B)
    if (check(TokenType::LEFT_PAREN)) {
        std::string s;
        int depth=0;
        while (!isAtEnd()) {
            Token t = advance();
            if (t.type==TokenType::LEFT_PAREN) depth++;
            if (t.type==TokenType::RIGHT_PAREN) { depth--; s+=t.lexeme; if (depth==0) break; else continue; }
            s+=t.lexeme;
            if (depth==0 && (check(TokenType::COMMA) || check(TokenType::RIGHT_PAREN))) { }
        }
        return s;
    }
    if (check(TokenType::LEFT_BRACKET)) {
        std::string s="[";
        advance();
        // parse inner type
        std::string inner = parseTypeName();
        s+=inner;
        if (check(TokenType::RIGHT_BRACKET)) { s+="]"; advance(); }
        return s;
    }
    // identifier with possible <T>
    if (check(TokenType::IDENTIFIER) || check(TokenType::BOX) || check(TokenType::SELF) || check(TokenType::SELF_TYPE)) {
        std::string name = advance().lexeme;
        if (check(TokenType::LESS)) {
            // generics
            name += "<";
            advance();
            while (!isAtEnd() && !check(TokenType::GREATER)) {
                name += parseTypeName();
                if (check(TokenType::COMMA)) { name+= ","; advance(); }
            }
            if (check(TokenType::GREATER)) { name+=">"; advance(); }
        }
        // handle ?, etc not needed
        return name;
    }
    return "";
}

std::vector<std::string> Parser::parseGenericParams() {
    std::vector<std::string> gens;
    if (!check(TokenType::LESS)) return gens;
    advance(); // <
    skipNewlines();
    while (!isAtEnd() && !check(TokenType::GREATER)) {
        if (check(TokenType::IDENTIFIER)) {
            gens.push_back(advance().lexeme);
        } else {
            break;
        }
        if (!match(TokenType::COMMA)) break;
        skipNewlines();
    }
    consume(TokenType::GREATER, "expected '>' after generic params");
    return gens;
}

std::vector<FnParam> Parser::parseFnParams() {
    std::vector<FnParam> params;
    consume(TokenType::LEFT_PAREN, "expected '(' for params");
    skipNewlines();
    while (!isAtEnd() && !check(TokenType::RIGHT_PAREN)) {
        FnParam p;
        if (check(TokenType::MUT)) { advance(); p.isMut=true; }
        if (check(TokenType::SELF)) { p.name="self"; advance(); }
        else {
            if (!check(TokenType::IDENTIFIER)) { error(peek(), "expected param name"); break; }
            p.name = advance().lexeme;
        }
        if (check(TokenType::COLON)) {
            advance();
            p.typeName = parseTypeName();
        }
        params.push_back(p);
        if (!match(TokenType::COMMA)) break;
        skipNewlines();
    }
    consume(TokenType::RIGHT_PAREN, "expected ')' after params");
    return params;
}

// Patterns
PatternPtr Parser::parsePattern() {
    return parseOrPattern();
}
PatternPtr Parser::parseOrPattern() {
    auto left = parseSinglePattern();
    if (!left) return nullptr;
    // handle | for OR pattern (but need to avoid confusion with closure? In match arms, "1 | 2" means or pattern. We'll treat PIPE as OR in pattern context)
    std::vector<PatternPtr> ors;
    ors.push_back(left);
    while (check(TokenType::PIPE)) {
        // check if next is not start of closure? In pattern, pipe separates patterns. We'll consume.
        advance();
        auto right = parseSinglePattern();
        if (right) ors.push_back(right);
        else break;
    }
    if (ors.size()>1) {
        auto p = std::make_shared<Pattern>(PatternKind::OR);
        p->sub = ors;
        return p;
    }
    // range check
    if (check(TokenType::DOT_DOT) || check(TokenType::DOT_DOT_EQUAL)) {
        bool inclusive = check(TokenType::DOT_DOT_EQUAL);
        advance();
        auto right = parseSinglePattern();
        auto rp = std::make_shared<Pattern>(PatternKind::RANGE);
        rp->rangeStart = left;
        rp->rangeEnd = right;
        rp->rangeInclusive = inclusive;
        return rp;
    }
    return left;
}
PatternPtr Parser::parseSinglePattern() {
    skipNewlines();
    if (match(TokenType::DOT_DOT_DOT)) {
        auto p = std::make_shared<Pattern>(PatternKind::REST);
        p->isRest = true;
        if (check(TokenType::IDENTIFIER)) {
            p->ident = advance().lexeme;
        }
        return p;
    }
    if (check(TokenType::LEFT_PAREN)) {
        return parseTupleOrParenPattern();
    }
    if (check(TokenType::LEFT_BRACKET)) {
        return parseArrayPattern();
    }
    if (check(TokenType::IDENTIFIER)) {
        Token t = advance();
        if (t.lexeme == "_") {
            return std::make_shared<Pattern>(PatternKind::WILDCARD);
        }
        auto p = std::make_shared<Pattern>(PatternKind::IDENT);
        p->ident = t.lexeme;
        // check if enum variant pattern: IDENT ( patterns )
        if (check(TokenType::LEFT_PAREN)) {
            // Convert to ENUM_VARIANT
            advance();
            auto vp = std::make_shared<Pattern>(PatternKind::ENUM_VARIANT);
            vp->ident = p->ident;
            skipNewlines();
            while (!check(TokenType::RIGHT_PAREN) && !isAtEnd()) {
                auto sub = parsePattern();
                if (sub) vp->sub.push_back(sub);
                if (!match(TokenType::COMMA)) break;
                skipNewlines();
            }
            consume(TokenType::RIGHT_PAREN, "expected ')' after variant pattern");
            return vp;
        }
        if (check(TokenType::LEFT_BRACE)) {
            // struct pattern { field, ... }
            advance();
            auto sp = std::make_shared<Pattern>(PatternKind::STRUCT);
            sp->ident = p->ident;
            skipNewlines();
            while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
                std::string fname;
                if (check(TokenType::IDENTIFIER)) fname = advance().lexeme;
                else { error(peek(), "expected field name in struct pattern"); break; }
                PatternPtr fieldPat = nullptr;
                if (match(TokenType::COLON)) {
                    fieldPat = parsePattern();
                } else {
                    // shorthand: field name as ident pattern
                    auto f = std::make_shared<Pattern>(PatternKind::IDENT);
                    f->ident = fname;
                    fieldPat = f;
                }
                sp->fields[fname] = fieldPat;
                if (!match(TokenType::COMMA)) break;
                skipNewlines();
            }
            consume(TokenType::RIGHT_BRACE, "expected '}' for struct pattern");
            return sp;
        }
        return p;
    }
    if (check(TokenType::NUMBER) || check(TokenType::STRING) || check(TokenType::TRUE) || check(TokenType::FALSE) || check(TokenType::NONE) || check(TokenType::MULTILINE_STRING)) {
        auto p = std::make_shared<Pattern>(PatternKind::LITERAL);
        p->literalToken = advance();
        return p;
    }
    // wildcard via ?
    error(peek(), "expected pattern");
    return std::make_shared<Pattern>(PatternKind::WILDCARD);
}
PatternPtr Parser::parseTupleOrParenPattern() {
    consume(TokenType::LEFT_PAREN, "expected '('");
    skipNewlines();
    std::vector<PatternPtr> elems;
    if (!check(TokenType::RIGHT_PAREN)) {
        while (true) {
            auto pat = parsePattern();
            if (pat) elems.push_back(pat);
            if (!match(TokenType::COMMA)) break;
            skipNewlines();
            if (check(TokenType::RIGHT_PAREN)) break;
        }
    }
    consume(TokenType::RIGHT_PAREN, "expected ')' after tuple pattern");
    if (elems.size()==1 && false) {
        // single paren grouping? We'll return it directly if no comma was used. But we don't have comma flag; assume if 1 element and original had no comma? We'll check if we had comma? For simplicity, if size==1, return that element as grouping.
        // However to detect whether comma existed, we'd need flag. We'll assume if size==1 return it.
        return elems[0];
    }
    if (elems.size()==1) {
        // To keep tuple vs grouping distinction, if original source had trailing comma? We'll treat (a) as just a for patterns.
        // We'll return the single element for simplicity unless it was intended as tuple with one element (needs trailing comma).
        // Let's return single.
        return elems[0];
    }
    auto p = std::make_shared<Pattern>(PatternKind::TUPLE);
    p->sub = elems;
    return p;
}
PatternPtr Parser::parseArrayPattern() {
    consume(TokenType::LEFT_BRACKET, "expected '['");
    skipNewlines();
    std::vector<PatternPtr> elems;
    while (!check(TokenType::RIGHT_BRACKET) && !isAtEnd()) {
        auto pat = parsePattern();
        if (pat) elems.push_back(pat);
        if (!match(TokenType::COMMA)) break;
        skipNewlines();
    }
    consume(TokenType::RIGHT_BRACKET, "expected ']'");
    auto p = std::make_shared<Pattern>(PatternKind::ARRAY);
    p->sub = elems;
    return p;
}

// Parsing program
Program Parser::parseProgram() {
    Program prog;
    skipNewlines();
    while (!isAtEnd()) {
        auto decl = parseDeclaration();
        if (decl) prog.statements.push_back(decl);
        else {
            // error recovery
            synchronize();
        }
        skipNewlines();
    }
    return prog;
}

StmtPtr Parser::parseDeclaration() {
    skipNewlines();
    if (isAtEnd()) return nullptr;

    bool isPub = false;
    bool isAsync = false;
    if (check(TokenType::PUB)) { advance(); isPub = true; skipNewlines(); }
    if (check(TokenType::ASYNC)) { advance(); isAsync = true; skipNewlines(); }

    if (check(TokenType::FN)) {
        return parseFnDecl(isPub, isAsync);
    }
    if (check(TokenType::STRUCT)) {
        return parseStructDecl(isPub);
    }
    if (check(TokenType::ENUM)) {
        return parseEnumDecl(isPub);
    }
    if (check(TokenType::IMPL)) {
        return parseImplDecl();
    }
    if (check(TokenType::TRAIT)) {
        return parseTraitDecl(isPub);
    }
    if (check(TokenType::MODULE)) {
        return parseModuleDecl();
    }
    if (check(TokenType::IMPORT) || check(TokenType::FROM)) {
        return parseImportDecl();
    }
    if (check(TokenType::LET)) {
        return parseLetDecl();
    }
    if (check(TokenType::CONST)) {
        return parseConstDecl();
    }
    // Fall back to statement
    return parseStatement();
}

StmtPtr Parser::parseStatement() {
    skipNewlines();
    // label?
    auto label = parseOptionalLabel();

    if (check(TokenType::LET)) {
        auto s = parseLetDecl();
        if (label) s->breakLabelOpt = label;
        return s;
    }
    if (check(TokenType::CONST)) {
        return parseConstDecl();
    }
    if (check(TokenType::RETURN)) {
        Token t = advance();
        auto stmt = std::make_shared<Stmt>(StmtKind::RETURN);
        stmt->line = t.line;
        if (!check(TokenType::NEWLINE) && !check(TokenType::SEMICOLON) && !check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            stmt->returnExpr = parseExpression();
        }
        // consume terminator
        if (check(TokenType::SEMICOLON) || check(TokenType::NEWLINE)) advance();
        return stmt;
    }
    if (check(TokenType::BREAK)) {
        Token t = advance();
        auto stmt = std::make_shared<Stmt>(StmtKind::BREAK);
        stmt->line = t.line;
        if (check(TokenType::IDENTIFIER)) {
            stmt->breakLabelOpt = advance().lexeme;
        }
        if (!isAtEnd() && !check(TokenType::NEWLINE) && !check(TokenType::SEMICOLON) && !check(TokenType::RIGHT_BRACE)) {
            // break with value: break label value or break value
            // If we already consumed label and next is not terminator, parse value
            // If label not consumed as label but as identifier? ambiguous.
            // Simplistic:
            // stmt->returnExpr = parseExpression();
        }
        if (check(TokenType::SEMICOLON) || check(TokenType::NEWLINE)) advance();
        return stmt;
    }
    if (check(TokenType::CONTINUE)) {
        Token t = advance();
        auto stmt = std::make_shared<Stmt>(StmtKind::CONTINUE);
        stmt->line = t.line;
        if (check(TokenType::IDENTIFIER)) stmt->breakLabelOpt = advance().lexeme;
        if (check(TokenType::SEMICOLON) || check(TokenType::NEWLINE)) advance();
        return stmt;
    }
    if (check(TokenType::FOR) || check(TokenType::WHILE) || check(TokenType::LOOP) || check(TokenType::IF) || check(TokenType::MATCH)) {
        // expression statement that is control flow
        auto expr = parseExpression();
        auto stmt = std::make_shared<Stmt>(StmtKind::EXPR);
        stmt->expr = expr;
        // optional semicolon
        if (check(TokenType::SEMICOLON)) advance();
        return stmt;
    }

    // Otherwise expression statement
    auto expr = parseExpression();
    if (!expr) return nullptr;
    auto stmt = std::make_shared<Stmt>(StmtKind::EXPR);
    stmt->expr = expr;
    stmt->line = expr->line;
    // consume optional semicolon/newline? newline handled by outer loop
    if (check(TokenType::SEMICOLON)) { advance(); }
    // don't consume newline here, let outer skip
    return stmt;
}

StmtPtr Parser::parseLetDecl() {
    Token letTok = consume(TokenType::LET, "expected 'let'");
    bool isMut = false;
    if (check(TokenType::MUT)) { advance(); isMut = true; }
    skipNewlines();
    auto pat = parsePattern();
    std::string name;
    // For simple case, if pattern is IDENT, store name
    if (pat && pat->kind == PatternKind::IDENT) name = pat->ident;
    std::optional<std::string> typeAnn;
    if (check(TokenType::COLON)) {
        advance();
        typeAnn = parseTypeName();
    }
    ExprPtr init = nullptr;
    if (match(TokenType::EQUAL)) {
        init = parseExpression();
    }
    auto stmt = std::make_shared<Stmt>(StmtKind::LET);
    stmt->line = letTok.line;
    stmt->isMut = isMut;
    stmt->letPattern = pat;
    stmt->letName = name;
    stmt->typeAnnotation = typeAnn;
    stmt->init = init;
    // optional terminator
    if (check(TokenType::SEMICOLON)) advance();
    return stmt;
}

StmtPtr Parser::parseConstDecl() {
    Token tok = consume(TokenType::CONST, "expected 'const'");
    if (!check(TokenType::IDENTIFIER)) { error(peek(),"expected const name"); return nullptr; }
    std::string name = advance().lexeme;
    std::optional<std::string> typeAnn;
    if (check(TokenType::COLON)) { advance(); typeAnn = parseTypeName(); }
    consume(TokenType::EQUAL, "expected '=' in const");
    auto val = parseExpression();
    auto stmt = std::make_shared<Stmt>(StmtKind::CONST);
    stmt->line = tok.line;
    stmt->constName = name;
    stmt->typeAnnotation = typeAnn;
    stmt->constValue = val;
    if (check(TokenType::SEMICOLON)) advance();
    return stmt;
}

StmtPtr Parser::parseFnDecl(bool isPub, bool isAsync) {
    Token fnTok = consume(TokenType::FN, "expected 'fn'");
    if (!check(TokenType::IDENTIFIER)) { error(peek(),"expected function name"); return nullptr; }
    std::string fname = advance().lexeme;
    auto generics = parseGenericParams();
    auto params = parseFnParams();
    std::optional<std::string> retType;
    if (check(TokenType::ARROW)) {
        advance();
        retType = parseTypeName();
    }
    skipNewlines();
    ExprPtr body = parseBlock();
    if (!body) { error(peek(),"expected function body"); }
    auto stmt = std::make_shared<Stmt>(StmtKind::FN);
    stmt->line = fnTok.line;
    stmt->fnName = fname;
    stmt->isPub = isPub;
    stmt->isAsync = isAsync;
    stmt->fnParams = params;
    stmt->returnType = retType;
    stmt->genericParams = generics;
    stmt->fnBody = body;
    return stmt;
}

StmtPtr Parser::parseStructDecl(bool isPub) {
    Token tok = consume(TokenType::STRUCT, "expected 'struct'");
    if (!check(TokenType::IDENTIFIER)) { error(peek(),"expected struct name"); return nullptr; }
    std::string sname = advance().lexeme;
    auto generics = parseGenericParams();
    consume(TokenType::LEFT_BRACE, "expected '{' for struct");
    std::vector<StructFieldDef> fields;
    skipNewlines();
    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        bool fpub = false;
        if (check(TokenType::PUB)) { advance(); fpub=true; }
        if (!check(TokenType::IDENTIFIER)) break;
        std::string fname = advance().lexeme;
        consume(TokenType::COLON, "expected ':' after field name");
        std::string ftype = parseTypeName();
        fields.push_back({fname, ftype, fpub});
        if (!match(TokenType::COMMA)) {
            skipNewlines();
            // allow newline as separator
        }
        skipNewlines();
    }
    consume(TokenType::RIGHT_BRACE, "expected '}' after struct");
    auto def = std::make_shared<StructDef>();
    def->name = sname;
    def->pub = isPub;
    def->fields = fields;
    def->genericParams = generics;
    auto stmt = std::make_shared<Stmt>(StmtKind::STRUCT);
    stmt->line = tok.line;
    stmt->structDef = def;
    return stmt;
}

StmtPtr Parser::parseEnumDecl(bool isPub) {
    Token tok = consume(TokenType::ENUM, "expected 'enum'");
    std::string ename = "";
    if (check(TokenType::IDENTIFIER)) ename = advance().lexeme;
    auto generics = parseGenericParams();
    consume(TokenType::LEFT_BRACE, "expected '{' for enum");
    std::vector<EnumVariantDef> vars;
    skipNewlines();
    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        if (!check(TokenType::IDENTIFIER)) break;
        std::string vname = advance().lexeme;
        EnumVariantDef vd; vd.name = vname;
        if (check(TokenType::LEFT_PAREN)) {
            advance();
            while (!check(TokenType::RIGHT_PAREN) && !isAtEnd()) {
                std::string tname = parseTypeName();
                vd.fieldTypes.push_back(tname);
                if (!match(TokenType::COMMA)) break;
            }
            consume(TokenType::RIGHT_PAREN, "expected ')'");
        } else if (check(TokenType::LEFT_BRACE)) {
            // struct-like variant
            advance();
            while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
                std::string fname = "";
                if (check(TokenType::IDENTIFIER)) fname = advance().lexeme;
                consume(TokenType::COLON, "expected ':'");
                std::string ftype = parseTypeName();
                vd.namedFields.push_back({fname, ftype});
                if (!match(TokenType::COMMA)) break;
            }
            consume(TokenType::RIGHT_BRACE, "expected '}'");
        }
        vars.push_back(vd);
        if (!match(TokenType::COMMA)) { skipNewlines(); }
        skipNewlines();
    }
    consume(TokenType::RIGHT_BRACE, "expected '}'");
    auto def = std::make_shared<EnumDef>();
    def->name = ename;
    def->pub = isPub;
    def->variants = vars;
    def->genericParams = generics;
    auto stmt = std::make_shared<Stmt>(StmtKind::ENUM);
    stmt->line = tok.line;
    stmt->enumDef = def;
    return stmt;
}

StmtPtr Parser::parseImplDecl() {
    Token tok = consume(TokenType::IMPL, "expected 'impl'");
    auto generics = parseGenericParams();
    std::string traitName = "";
    std::string targetName = "";
    // Could be impl Trait for Type or impl Type
    // Peek identifier
    if (check(TokenType::IDENTIFIER)) {
        std::string first = advance().lexeme;
        // Check if next is FOR? Actually keyword "for" used for trait impl? Cotton uses "impl Trait for Type"? but spec shows "impl Area for Shape"
        // However our FOR token same as keyword "for". So we might have parsed "for" keyword as FOR token.
        // So check if next token is FOR (keyword for) or "for"? But our trait example: "impl Area for Shape" - second word "for" is same as loop for keyword, lexed as FOR.
        // So detect FOR token.
        if (check(TokenType::FOR)) {
            // first is trait
            traitName = first;
            advance(); // for
            if (check(TokenType::IDENTIFIER)) targetName = advance().lexeme;
        } else {
            targetName = first;
        }
    }
    if (targetName.empty()) {
        targetName = parseTypeName();
    }
    consume(TokenType::LEFT_BRACE, "expected '{' for impl");
    std::vector<StmtPtr> methods;
    skipNewlines();
    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        bool isPub = false;
        bool isAsync = false;
        if (check(TokenType::PUB)) { advance(); isPub=true; skipNewlines(); }
        if (check(TokenType::ASYNC)) { advance(); isAsync=true; }
        if (check(TokenType::FN)) {
            auto m = parseFnDecl(isPub, isAsync);
            if (m) methods.push_back(m);
        } else {
            // skip?
            advance();
        }
        skipNewlines();
    }
    consume(TokenType::RIGHT_BRACE, "expected '}' for impl");
    auto stmt = std::make_shared<Stmt>(StmtKind::IMPL);
    stmt->line = tok.line;
    stmt->implTarget = targetName;
    stmt->implTrait = traitName;
    stmt->implMethods = methods;
    stmt->genericParams = generics;
    return stmt;
}

StmtPtr Parser::parseTraitDecl(bool isPub) {
    Token tok = consume(TokenType::TRAIT, "expected 'trait'");
    std::string tname = "";
    if (check(TokenType::IDENTIFIER)) tname = advance().lexeme;
    auto generics = parseGenericParams();
    consume(TokenType::LEFT_BRACE, "expected '{' for trait");
    std::vector<StmtPtr> methods;
    skipNewlines();
    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        bool isPubM = false;
        bool isAsync = false;
        if (check(TokenType::PUB)) { advance(); isPubM=true; }
        if (check(TokenType::ASYNC)) { advance(); isAsync=true; }
        if (check(TokenType::FN)) {
            auto m = parseFnDecl(isPubM, isAsync);
            // If trait method has no body, its body may be missing. Our parseFnDecl expects block, so allow optional block?
            methods.push_back(m);
        } else {
            advance();
        }
        skipNewlines();
    }
    consume(TokenType::RIGHT_BRACE, "expected '}'");
    auto stmt = std::make_shared<Stmt>(StmtKind::TRAIT);
    stmt->line = tok.line;
    stmt->traitName = tname;
    stmt->implMethods = methods; // reuse
    stmt->genericParams = generics;
    stmt->isPub = isPub;
    return stmt;
}

StmtPtr Parser::parseImportDecl() {
    // import math   or from collections import Stack, Queue
    Token tok = peek();
    bool isFrom = false;
    std::string path;
    std::vector<std::string> names;
    if (check(TokenType::FROM)) {
        isFrom = true;
        advance();
        if (check(TokenType::IDENTIFIER)) path = advance().lexeme;
        consume(TokenType::IMPORT, "expected 'import' after from");
        // list
        while (check(TokenType::IDENTIFIER)) {
            names.push_back(advance().lexeme);
            if (!match(TokenType::COMMA)) break;
        }
    } else {
        consume(TokenType::IMPORT, "expected 'import'");
        if (check(TokenType::IDENTIFIER)) path = advance().lexeme;
        // Could be dotted path? We'll handle dots
        while (check(TokenType::DOT) && checkNext(TokenType::IDENTIFIER)) {
            advance(); // .
            path += "." + advance().lexeme;
        }
    }
    auto stmt = std::make_shared<Stmt>(StmtKind::IMPORT);
    stmt->line = tok.line;
    stmt->importPath = path;
    stmt->importNames = names;
    stmt->isFromImport = isFrom;
    if (check(TokenType::SEMICOLON) || check(TokenType::NEWLINE)) advance();
    return stmt;
}

StmtPtr Parser::parseModuleDecl() {
    Token tok = consume(TokenType::MODULE, "expected 'module'");
    std::string mname = "";
    if (check(TokenType::IDENTIFIER)) mname = advance().lexeme;
    consume(TokenType::LEFT_BRACE, "expected '{' for module");
    std::vector<StmtPtr> stmts;
    skipNewlines();
    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        auto d = parseDeclaration();
        if (d) stmts.push_back(d);
        skipNewlines();
    }
    consume(TokenType::RIGHT_BRACE, "expected '}' for module");
    auto stmt = std::make_shared<Stmt>(StmtKind::MODULE);
    stmt->line = tok.line;
    stmt->moduleName = mname;
    stmt->moduleStmts = stmts;
    return stmt;
}

// Expressions
ExprPtr Parser::parseExpression() {
    return parseAssignment();
}

ExprPtr Parser::parseAssignment() {
    auto left = parseCoalesce();
    if (!left) return nullptr;
    // check for assignment operators: =, +=, -= etc (lexeme contains = but not ==)
    if (check(TokenType::EQUAL)) {
        Token op = advance();
        auto right = parseAssignment();
        auto e = std::make_shared<Expr>(ExprKind::ASSIGN);
        e->line = op.line;
        e->assignOp = op;
        e->assignTarget = left;
        e->assignValue = right;
        return e;
    }
    // handle += etc - we stored lexeme as "+=" but token type PLUS etc
    // Let's check lexeme of peek
    if (!isAtEnd()) {
        std::string lex = peek().lexeme;
        if (lex=="+=" || lex=="-=" || lex=="*=" || lex=="/=" || lex=="%=") {
            Token op = advance();
            auto right = parseAssignment();
            auto e = std::make_shared<Expr>(ExprKind::ASSIGN);
            e->line = op.line;
            e->assignOp = op;
            e->assignTarget = left;
            e->assignValue = right;
            return e;
        }
    }
    return left;
}

ExprPtr Parser::parseCoalesce() {
    auto left = parseOr();
    while (check(TokenType::QUESTION_QUESTION)) {
        Token op = advance();
        auto right = parseOr();
        auto e = std::make_shared<Expr>(ExprKind::COALESCE);
        e->line = op.line;
        e->op = op;
        e->left = left;
        e->right = right;
        left = e;
    }
    return left;
}
ExprPtr Parser::parseOr() {
    auto left = parseAnd();
    while (check(TokenType::OR)) {
        Token op = advance();
        auto right = parseAnd();
        auto e = std::make_shared<Expr>(ExprKind::BINARY);
        e->line = op.line;
        e->op = op;
        e->left = left;
        e->right = right;
        left = e;
    }
    return left;
}
ExprPtr Parser::parseAnd() {
    auto left = parseEquality();
    while (check(TokenType::AND)) {
        Token op = advance();
        auto right = parseEquality();
        auto e = std::make_shared<Expr>(ExprKind::BINARY);
        e->line = op.line;
        e->op = op;
        e->left = left;
        e->right = right;
        left = e;
    }
    return left;
}
ExprPtr Parser::parseEquality() {
    auto left = parseComparison();
    while (check(TokenType::EQUAL_EQUAL) || check(TokenType::BANG_EQUAL)) {
        Token op = advance();
        auto right = parseComparison();
        auto e = std::make_shared<Expr>(ExprKind::BINARY);
        e->line = op.line;
        e->op = op;
        e->left = left;
        e->right = right;
        left = e;
    }
    return left;
}
ExprPtr Parser::parseComparison() {
    auto left = parseRange();
    while (check(TokenType::LESS) || check(TokenType::GREATER) || check(TokenType::LESS_EQUAL) || check(TokenType::GREATER_EQUAL)) {
        Token op = advance();
        auto right = parseRange();
        auto e = std::make_shared<Expr>(ExprKind::BINARY);
        e->line = op.line;
        e->op = op;
        e->left = left;
        e->right = right;
        left = e;
    }
    return left;
}
ExprPtr Parser::parseRange() {
    auto left = parseTerm();
    if (check(TokenType::DOT_DOT) || check(TokenType::DOT_DOT_EQUAL)) {
        Token op = advance();
        auto right = parseTerm();
        auto e = std::make_shared<Expr>(ExprKind::RANGE);
        e->line = op.line;
        e->rangeStart = left;
        e->rangeEnd = right;
        e->rangeInclusive = (op.type == TokenType::DOT_DOT_EQUAL);
        return e;
    }
    return left;
}
ExprPtr Parser::parseTerm() {
    auto left = parseFactor();
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        // need to ensure not arrow etc
        // PLUS token might be "+=" but we already handled assignment before; here + should be plus.
        if (peek().lexeme == "+=" || peek().lexeme == "-=") break;
        Token op = advance();
        auto right = parseFactor();
        auto e = std::make_shared<Expr>(ExprKind::BINARY);
        e->line = op.line;
        e->op = op;
        e->left = left;
        e->right = right;
        left = e;
    }
    return left;
}
ExprPtr Parser::parseFactor() {
    auto left = parsePower();
    while (check(TokenType::STAR) || check(TokenType::SLASH) || check(TokenType::PERCENT)) {
        Token op = advance();
        auto right = parsePower();
        auto e = std::make_shared<Expr>(ExprKind::BINARY);
        e->line = op.line;
        e->op = op;
        e->left = left;
        e->right = right;
        left = e;
    }
    return left;
}
ExprPtr Parser::parsePower() {
    auto left = parseUnary();
    if (check(TokenType::STAR_STAR)) {
        Token op = advance();
        auto right = parsePower(); // right associative
        auto e = std::make_shared<Expr>(ExprKind::BINARY);
        e->line = op.line;
        e->op = op;
        e->left = left;
        e->right = right;
        return e;
    }
    return left;
}
ExprPtr Parser::parseUnary() {
    if (check(TokenType::BANG) || check(TokenType::MINUS) || check(TokenType::NOT)) {
        Token op = advance();
        auto right = parseUnary();
        auto e = std::make_shared<Expr>(ExprKind::UNARY);
        e->line = op.line;
        e->op = op;
        e->unaryExpr = right;
        return e;
    }
    if (check(TokenType::AMP)) {
        Token op = advance();
        bool isMut = false;
        if (check(TokenType::MUT)) { advance(); isMut=true; }
        auto right = parseUnary();
        auto e = std::make_shared<Expr>(ExprKind::VAR_REF);
        e->line = op.line;
        e->isMutRef = isMut;
        e->unaryExpr = right;
        return e;
    }
    if (check(TokenType::AWAIT)) {
        Token op = advance();
        auto right = parseUnary();
        auto e = std::make_shared<Expr>(ExprKind::AWAIT);
        e->line = op.line;
        e->unaryExpr = right;
        return e;
    }
    return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
    auto expr = parseCallChain();
    // handle postfix ?
    while (check(TokenType::QUESTION)) {
        // distinguish ? as propagation (single ?)
        // Ensure not ?? or ?. (already handled)
        Token op = advance();
        auto e = std::make_shared<Expr>(ExprKind::RESULT_PROPAGATION);
        e->line = op.line;
        e->unaryExpr = expr;
        expr = e;
    }
    return expr;
}

ExprPtr Parser::parseCallChain() {
    auto expr = parsePrimary();
    if (!expr) return nullptr;
    while (true) {
        if (check(TokenType::LEFT_PAREN)) {
            // call
            advance();
            std::vector<ExprPtr> args;
            skipNewlines();
            while (!check(TokenType::RIGHT_PAREN) && !isAtEnd()) {
                auto arg = parseExpression();
                if (arg) args.push_back(arg);
                if (!match(TokenType::COMMA)) break;
                skipNewlines();
            }
            consume(TokenType::RIGHT_PAREN, "expected ')' after arguments");
            auto call = std::make_shared<Expr>(ExprKind::CALL);
            call->line = expr->line;
            call->callee = expr;
            call->args = args;
            expr = call;
        } else if (check(TokenType::LEFT_BRACKET)) {
            // index
            advance();
            auto idx = parseExpression();
            consume(TokenType::RIGHT_BRACKET, "expected ']' after index");
            auto ix = std::make_shared<Expr>(ExprKind::INDEX);
            ix->line = expr->line;
            ix->object = expr;
            ix->index = idx;
            expr = ix;
        } else if (check(TokenType::DOT)) {
            advance();
            if (!check(TokenType::IDENTIFIER)) { error(peek(), "expected field name after '.'"); break; }
            std::string field = advance().lexeme;
            auto fe = std::make_shared<Expr>(ExprKind::FIELD);
            fe->line = expr->line;
            fe->object = expr;
            fe->fieldName = field;
            expr = fe;
        } else if (check(TokenType::QUESTION_DOT)) {
            Token op = advance();
            if (!check(TokenType::IDENTIFIER)) { error(peek(), "expected field after '?.'"); break; }
            std::string field = advance().lexeme;
            // For simplicity, parse as optional chain field, but we will create OPTIONAL_CHAIN node containing object and field
            auto e = std::make_shared<Expr>(ExprKind::OPTIONAL_CHAIN);
            e->line = op.line;
            e->object = expr;
            e->fieldName = field;
            // Check if call follows: ?.method() or field call? Could be method call via ?.len()
            // We'll handle call in next loop iteration? Actually need to handle if next is '(' then it's optional call.
            // We'll leave and let next iteration handle if '('?
            expr = e;
        } else if (check(TokenType::TEMPLATE_STRING)) {
            // tagged template: identifier `...` => treat as call with one arg template
            Token t = advance();
            auto tmpl = parseTemplateString(t);
            auto call = std::make_shared<Expr>(ExprKind::CALL);
            call->line = expr->line;
            call->callee = expr;
            call->args = {tmpl};
            expr = call;
        } else {
            break;
        }
    }
    return expr;
}

// Helpers for template
static std::vector<std::pair<std::string, std::string>> splitTemplate(const std::string& inner) {
    // Returns vector of parts: either literal or expr string
    // We'll parse manually: literal segments and ${expr}
    std::vector<std::pair<std::string, std::string>> res; // {type, content} type "text" or "expr"
    std::string curText;
    for (size_t i=0; i<inner.size();) {
        if (inner[i]=='$' && i+1<inner.size() && inner[i+1]=='{') {
            if (!curText.empty()) { res.push_back({"text", curText}); curText.clear(); }
            i+=2;
            int depth=1;
            std::string expr;
            while (i<inner.size() && depth>0) {
                char c = inner[i];
                if (c=='{') depth++;
                else if (c=='}') { depth--; if (depth==0) { i++; break; } }
                expr.push_back(c);
                i++;
            }
            res.push_back({"expr", expr});
        } else {
            curText.push_back(inner[i]);
            i++;
        }
    }
    if (!curText.empty()) res.push_back({"text", curText});
    return res;
}

ExprPtr Parser::parseTemplateString(const Token& tok) {
    std::string inner = tok.lexeme;
    auto partsRaw = splitTemplate(inner);
    auto expr = std::make_shared<Expr>(ExprKind::TEMPLATE_STRING);
    expr->line = tok.line;
    for (auto& pr: partsRaw) {
        typename Expr::TemplatePart tp;
        if (pr.first=="text") {
            tp.isExpr = false;
            tp.text = pr.second;
        } else {
            tp.isExpr = true;
            tp.text = pr.second;
            // Parse inner expression string into AST
            // Create lexer and parser recursively
            Lexer lex(pr.second);
            auto toks = lex.tokenize();
            Parser p(toks);
            auto innerExpr = p.parseExpression();
            if (p.hadError || !innerExpr) {
                // fallback literal?
                innerExpr = std::make_shared<Expr>(ExprKind::LITERAL);
                innerExpr->literal = Token(TokenType::STRING, pr.second, tok.line, tok.col);
            }
            tp.expr = innerExpr;
        }
        expr->templateParts.push_back(tp);
    }
    return expr;
}

ExprPtr Parser::parsePrimary() {
    skipNewlines();
    if (isAtEnd()) return nullptr;
    Token t = peek();

    // literals
    if (match(TokenType::NUMBER) || match(TokenType::STRING) || match(TokenType::MULTILINE_STRING) || match(TokenType::TRUE) || match(TokenType::FALSE) || match(TokenType::NONE)) {
        auto e = std::make_shared<Expr>(ExprKind::LITERAL);
        e->line = t.line;
        e->literal = previous();
        e->strValue = previous().lexeme;
        return e;
    }
    if (check(TokenType::TEMPLATE_STRING)) {
        Token tmpl = advance();
        return parseTemplateString(tmpl);
    }
    if (check(TokenType::LEFT_BRACE)) {
        // Could be block or dict
        // Lookahead to decide: if next non-newline token is IDENTIFIER/STRING followed by COLON, treat as dict
        bool isDict = false;
        if (current+1 < tokens.size()) {
            // look ahead one
            Token n1 = tokens[current+1];
            if (n1.type==TokenType::RIGHT_BRACE) {
                // empty, treat as block? but could be empty dict. We'll treat as dict? Choose block empty returning dict? Let's treat as ARRAY? Actually empty {} could be empty dict or empty block. We'll treat as block returning empty dict for now => DICT empty.
                // We'll let parseDict handle empty.
            }
            // If after '{', skip newlines, see if next is }? Then ambiguous.
            size_t idx = current+1;
            while (idx < tokens.size() && (tokens[idx].type==TokenType::NEWLINE || tokens[idx].type==TokenType::SEMICOLON)) idx++;
            if (idx < tokens.size()) {
                Token a = tokens[idx];
                size_t idx2 = idx+1;
                while (idx2 < tokens.size() && tokens[idx2].type==TokenType::NEWLINE) idx2++; // not needed
                Token b = (idx2 < tokens.size()) ? tokens[idx2] : Token(TokenType::END_OF_FILE,"",0,0);
                if ((a.type==TokenType::IDENTIFIER || a.type==TokenType::STRING) && b.type==TokenType::COLON) {
                    isDict = true;
                }
                // also if a is string literal? already
                // If we see something like "a: expression" but inside block you could have "if"? The presence of colon after identifier in block would be unusual (maybe type annotation? but field?).
                // So treat as dict if colon.
            }
        }
        if (isDict) {
            return parseDictLiteralOrComprehensionOrStruct();
        } else {
            return parseBlock();
        }
    }
    if (check(TokenType::LEFT_BRACKET)) {
        return parseArrayLiteralOrComprehension();
    }
    if (check(TokenType::LEFT_PAREN)) {
        return parseParenOrTuple();
    }
    if (check(TokenType::IF)) {
        return parseIfExpr();
    }
    if (check(TokenType::FOR)) {
        // for expr without label (label case handled in parseStatement that would have called parseExpression which calls parsePrimary? So label must be parsed before? We'll handle label inside for parser if present as optional before? Actually label detection earlier in parseStatement wraps optional label. For expression inside like nested for loops without label, this works.
        return parseForExpr(std::nullopt);
    }
    if (check(TokenType::WHILE)) {
        return parseWhileExpr(std::nullopt);
    }
    if (check(TokenType::LOOP)) {
        return parseLoopExpr(std::nullopt);
    }
    if (check(TokenType::MATCH)) {
        return parseMatchExpr();
    }
    if (check(TokenType::PIPE)) {
        return parseClosureOrPipe();
    }
    if (check(TokenType::BOX)) {
        Token boxTok = advance();
        // Box ( expr ) or Box Struct{...}
        auto e = std::make_shared<Expr>(ExprKind::BOX);
        e->line = boxTok.line;
        if (check(TokenType::LEFT_PAREN)) {
            advance();
            e->unaryExpr = parseExpression();
            consume(TokenType::RIGHT_PAREN, "expected ')' after Box");
        } else {
            e->unaryExpr = parsePrimary();
        }
        return e;
    }
    if (check(TokenType::UNSAFE)) {
        Token uns = advance();
        auto block = parseBlock();
        auto e = std::make_shared<Expr>(ExprKind::UNSAFE_BLOCK);
        e->line = uns.line;
        e->unaryExpr = block;
        return e;
    }
    if (check(TokenType::SPAWN)) {
        Token sp = advance();
        // spawn { expr } or spawn expr
        ExprPtr body;
        if (check(TokenType::LEFT_BRACE)) body = parseBlock();
        else body = parseExpression();
        auto e = std::make_shared<Expr>(ExprKind::SPAWN);
        e->line = sp.line;
        e->unaryExpr = body;
        return e;
    }
    if (check(TokenType::IDENTIFIER) || check(TokenType::SELF) || check(TokenType::OK) || check(TokenType::ERR) || check(TokenType::SOME)) {
        // Lookahead for struct literal: IDENT { ... } where inside first token is IDENT COLON or RIGHT_BRACE
        bool isStructLiteral = false;
        if (checkNext(TokenType::LEFT_BRACE)) {
            size_t idx = current + 2; // after IDENT and {
            while (idx < tokens.size() && (tokens[idx].type == TokenType::NEWLINE || tokens[idx].type == TokenType::SEMICOLON)) idx++;
            if (idx < tokens.size()) {
                if (tokens[idx].type == TokenType::RIGHT_BRACE) {
                    isStructLiteral = true; // empty struct
                } else if (tokens[idx].type == TokenType::IDENTIFIER) {
                    size_t idx2 = idx + 1;
                    while (idx2 < tokens.size() && (tokens[idx2].type == TokenType::NEWLINE)) idx2++;
                    if (idx2 < tokens.size() && tokens[idx2].type == TokenType::COLON) {
                        isStructLiteral = true;
                    }
                }
            }
        }
        Token idTok = advance();
        std::string name = idTok.lexeme;
        // Check if struct literal: Name { fields }
        if (isStructLiteral && check(TokenType::LEFT_BRACE)) {
            // struct literal
            advance(); // {
            std::vector<std::pair<std::string, ExprPtr>> fields;
            skipNewlines();
            while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
                std::string fname;
                if (check(TokenType::IDENTIFIER)) fname = advance().lexeme;
                else { error(peek(), "expected field name"); break; }
                ExprPtr fval = nullptr;
                if (match(TokenType::COLON)) {
                    fval = parseExpression();
                } else {
                    // shorthand {x} means x: x
                    fval = std::make_shared<Expr>(ExprKind::IDENT);
                    fval->identName = fname;
                    fval->line = previous().line;
                }
                fields.push_back({fname, fval});
                if (!match(TokenType::COMMA)) {
                    skipNewlines();
                }
                skipNewlines();
            }
            consume(TokenType::RIGHT_BRACE, "expected '}' for struct literal");
            auto e = std::make_shared<Expr>(ExprKind::STRUCT_LITERAL);
            e->line = idTok.line;
            e->structName = name;
            e->structFields = fields;
            e->isAnonymousStruct = false;
            return e;
        }
        auto e = std::make_shared<Expr>(ExprKind::IDENT);
        e->line = idTok.line;
        e->identName = name;
        return e;
    }

    error(peek(), "unexpected token in expression: " + tokenTypeToString(peek().type) + " '" + peek().lexeme + "'");
    advance();
    return nullptr;
}

ExprPtr Parser::parseBlock() {
    Token lb = consume(TokenType::LEFT_BRACE, "expected '{'");
    std::vector<StmtPtr> stmts;
    skipNewlines();
    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        auto decl = parseDeclaration();
        if (decl) stmts.push_back(decl);
        else { synchronize(); }
        skipNewlines();
    }
    consume(TokenType::RIGHT_BRACE, "expected '}' after block");
    auto e = std::make_shared<Expr>(ExprKind::BLOCK);
    e->line = lb.line;
    e->stmts = stmts;
    return e;
}

ExprPtr Parser::parseIfExpr() {
    Token ifTok = consume(TokenType::IF, "expected 'if'");
    auto cond = parseExpression();
    ExprPtr thenBranch = nullptr;
    if (check(TokenType::LEFT_BRACE)) thenBranch = parseBlock();
    else thenBranch = parseExpression();
    ExprPtr elseBranch = nullptr;
    if (check(TokenType::ELSE)) {
        advance();
        if (check(TokenType::IF)) elseBranch = parseIfExpr();
        else if (check(TokenType::LEFT_BRACE)) elseBranch = parseBlock();
        else elseBranch = parseExpression();
    }
    auto e = std::make_shared<Expr>(ExprKind::IF);
    e->line = ifTok.line;
    e->cond = cond;
    e->thenBranch = thenBranch;
    e->elseBranch = elseBranch;
    return e;
}

ExprPtr Parser::parseForExpr(std::optional<std::string> label) {
    Token forTok = consume(TokenType::FOR, "expected 'for'");
    auto pat = parsePattern();
    consume(TokenType::IN, "expected 'in' after for pattern");
    auto iter = parseExpression();
    ExprPtr body = nullptr;
    if (check(TokenType::LEFT_BRACE)) body = parseBlock();
    else body = parseExpression();
    auto e = std::make_shared<Expr>(ExprKind::FOR);
    e->line = forTok.line;
    e->label = label;
    e->forPattern = pat;
    e->forIter = iter;
    e->forBody = body;
    return e;
}
ExprPtr Parser::parseWhileExpr(std::optional<std::string> label) {
    Token w = consume(TokenType::WHILE, "expected 'while'");
    auto cond = parseExpression();
    ExprPtr body = nullptr;
    if (check(TokenType::LEFT_BRACE)) body = parseBlock();
    else body = parseExpression();
    auto e = std::make_shared<Expr>(ExprKind::WHILE);
    e->line = w.line;
    e->label = label;
    e->cond = cond;
    e->forBody = body;
    return e;
}
ExprPtr Parser::parseLoopExpr(std::optional<std::string> label) {
    Token l = consume(TokenType::LOOP, "expected 'loop'");
    ExprPtr body = nullptr;
    if (check(TokenType::LEFT_BRACE)) body = parseBlock();
    else body = parseExpression();
    auto e = std::make_shared<Expr>(ExprKind::LOOP);
    e->line = l.line;
    e->label = label;
    e->forBody = body;
    return e;
}

ExprPtr Parser::parseMatchExpr() {
    Token m = consume(TokenType::MATCH, "expected 'match'");
    auto target = parseExpression();
    consume(TokenType::LEFT_BRACE, "expected '{' after match target");
    std::vector<MatchArm> arms;
    skipNewlines();
    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        auto pat = parsePattern();
        ExprPtr guard = nullptr;
        if (check(TokenType::IF)) {
            advance();
            guard = parseExpression();
        }
        consume(TokenType::FAT_ARROW, "expected '=>' after pattern");
        auto body = parseExpression();
        // allow trailing comma
        arms.push_back({pat, guard, body});
        if (!match(TokenType::COMMA)) {
            skipNewlines();
        }
        skipNewlines();
    }
    consume(TokenType::RIGHT_BRACE, "expected '}' for match");
    auto e = std::make_shared<Expr>(ExprKind::MATCH);
    e->line = m.line;
    e->matchTarget = target;
    e->matchArms = arms;
    return e;
}

ExprPtr Parser::parseClosureOrPipe() {
    Token firstPipe = consume(TokenType::PIPE, "expected '|'");
    std::vector<std::string> params;
    skipNewlines();
    while (!check(TokenType::PIPE) && !isAtEnd()) {
        if (check(TokenType::IDENTIFIER)) params.push_back(advance().lexeme);
        // type annotation skip
        if (check(TokenType::COLON)) { advance(); parseTypeName(); }
        if (!match(TokenType::COMMA)) break;
    }
    consume(TokenType::PIPE, "expected '|' after closure params");
    ExprPtr body = nullptr;
    if (check(TokenType::LEFT_BRACE)) body = parseBlock();
    else body = parseExpression();
    auto e = std::make_shared<Expr>(ExprKind::CLOSURE);
    e->line = firstPipe.line;
    e->params = params;
    e->closureBody = body;
    return e;
}

ExprPtr Parser::parseArrayLiteralOrComprehension() {
    Token lb = consume(TokenType::LEFT_BRACKET, "expected '['");
    skipNewlines();
    if (check(TokenType::RIGHT_BRACKET)) {
        advance();
        auto e = std::make_shared<Expr>(ExprKind::ARRAY);
        e->line = lb.line;
        return e;
    }
    // parse first expr
    auto first = parseExpression();
    if (!first) {
        consume(TokenType::RIGHT_BRACKET, "expected ']'");
        auto e = std::make_shared<Expr>(ExprKind::ARRAY);
        e->line = lb.line;
        return e;
    }
    // check if for => comprehension
    if (check(TokenType::FOR)) {
        // comprehension
        std::vector<ComprehensionClause> clauses;
        while (check(TokenType::FOR)) {
            advance();
            // pattern could be identifier or pattern? Use pattern parsing for var
            auto pat = parsePattern();
            std::string varName;
            if (pat && pat->kind==PatternKind::IDENT) varName = pat->ident;
            // else? for simplicity take ident from pattern's first ident
            if (varName.empty() && pat && !pat->sub.empty() && pat->sub[0]->kind==PatternKind::IDENT) varName = pat->sub[0]->ident;
            consume(TokenType::IN, "expected 'in' in comprehension");
            auto iter = parseExpression();
            ExprPtr cond = nullptr;
            if (check(TokenType::IF)) { advance(); cond = parseExpression(); }
            clauses.push_back({varName, pat, iter, cond});
        }
        consume(TokenType::RIGHT_BRACKET, "expected ']' after comprehension");
        auto e = std::make_shared<Expr>(ExprKind::COMPREHENSION);
        e->line = lb.line;
        e->compExpr = first;
        e->compClauses = clauses;
        e->isDictComp = false;
        return e;
    } else {
        // array elements
        std::vector<ExprPtr> elems;
        elems.push_back(first);
        while (match(TokenType::COMMA)) {
            skipNewlines();
            if (check(TokenType::RIGHT_BRACKET)) break;
            // spread?
            if (check(TokenType::DOT_DOT_DOT)) {
                advance();
                auto spreadExpr = parseExpression();
                auto se = std::make_shared<Expr>(ExprKind::STRUCT_LITERAL); // reuse? Better create spread node
                // We'll mark as maybe binary? Actually create expression with kind? Let's create a UNARY spread? For simplicity, we'll treat ...x as a special expression
                auto spre = std::make_shared<Expr>(ExprKind::ARRAY);
                spre->elements = {spreadExpr}; // not ideal
                // We'll encode spread as a closure? Instead create array with spread flag via struct literal? Let's create a dummy struct literal for spread handling in interpreter: it checks field?
                // Simpler: wrap in a special node: we create an Expr of kind STRUCT_LITERAL with isAnonymous and field "spread" ??? Hack.
                // Instead we will create an Expr of kind BINARY with op DOT_DOT_DOT and right = spreadExpr. Interpreter will handle.
                auto wrapper = std::make_shared<Expr>(ExprKind::UNARY);
                Token dot; dot.type = TokenType::DOT_DOT_DOT; dot.lexeme="...";
                wrapper->op = dot;
                wrapper->unaryExpr = spreadExpr;
                elems.push_back(wrapper);
            } else {
                auto el = parseExpression();
                if (el) elems.push_back(el);
            }
            skipNewlines();
        }
        consume(TokenType::RIGHT_BRACKET, "expected ']' after array");
        auto e = std::make_shared<Expr>(ExprKind::ARRAY);
        e->line = lb.line;
        e->elements = elems;
        return e;
    }
}

ExprPtr Parser::parseDictLiteralOrComprehensionOrStruct() {
    Token lb = consume(TokenType::LEFT_BRACE, "expected '{'");
    skipNewlines();
    if (check(TokenType::RIGHT_BRACE)) {
        advance();
        auto e = std::make_shared<Expr>(ExprKind::DICT);
        e->line = lb.line;
        return e;
    }
    // try parse dict entries
    std::vector<std::pair<ExprPtr, ExprPtr>> entries;
    ExprPtr firstKey = nullptr;
    ExprPtr firstVal = nullptr;
    bool sawColon = false;

    // Look ahead: first entry should be key: value
    // Parse key
    auto k = parseExpression();
    if (check(TokenType::COLON)) {
        sawColon = true;
        advance();
        auto v = parseExpression();
        firstKey = k;
        firstVal = v;
        entries.push_back({k,v});
    } else {
        // not dict? Could be block that we mis-detected? But we already decided dict.
        // If not colon, treat as block? For now fallback to block parsing: we have already consumed '{' and parsed one expr as statement? We'll try to recover as block.
        // We'll parse rest as block statements and return block.
        // To avoid complexity, if not colon, we reinterpret as block: put back? Simpler return dict with empty.
    }

    if (sawColon) {
        // Check comprehension: key: value for ...
        if (check(TokenType::FOR)) {
            std::vector<ComprehensionClause> clauses;
            while (check(TokenType::FOR)) {
                advance();
                auto pat = parsePattern();
                std::string varName;
                if (pat && pat->kind==PatternKind::IDENT) varName = pat->ident;
                consume(TokenType::IN, "expected 'in' in dict comprehension");
                auto iter = parseExpression();
                ExprPtr cond = nullptr;
                if (check(TokenType::IF)) { advance(); cond = parseExpression(); }
                clauses.push_back({varName, pat, iter, cond});
            }
            consume(TokenType::RIGHT_BRACE, "expected '}' after dict comprehension");
            auto e = std::make_shared<Expr>(ExprKind::COMPREHENSION);
            e->line = lb.line;
            e->isDictComp = true;
            e->compKey = firstKey;
            e->compExpr = firstVal;
            e->compClauses = clauses;
            return e;
        }
        // regular dict continued
        while (match(TokenType::COMMA)) {
            skipNewlines();
            if (check(TokenType::RIGHT_BRACE)) break;
            auto kk = parseExpression();
            consume(TokenType::COLON, "expected ':' in dict");
            auto vv = parseExpression();
            entries.push_back({kk, vv});
        }
        consume(TokenType::RIGHT_BRACE, "expected '}' after dict");
        auto e = std::make_shared<Expr>(ExprKind::DICT);
        e->line = lb.line;
        e->dictEntries = entries;
        return e;
    } else {
        // fallback block reparsing? We'll treat as block with one expr stmt already parsed, continue parsing rest as block.
        // We have already consumed '{' and parsed k as expression. Let's build a block from it.
        std::vector<StmtPtr> stmts;
        auto st = std::make_shared<Stmt>(StmtKind::EXPR);
        st->expr = k;
        stmts.push_back(st);
        skipNewlines();
        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            auto d = parseDeclaration();
            if (d) stmts.push_back(d);
            skipNewlines();
        }
        consume(TokenType::RIGHT_BRACE, "expected '}'");
        auto e = std::make_shared<Expr>(ExprKind::BLOCK);
        e->line = lb.line;
        e->stmts = stmts;
        return e;
    }
}

ExprPtr Parser::parseParenOrTuple() {
    Token lp = consume(TokenType::LEFT_PAREN, "expected '('");
    skipNewlines();
    if (check(TokenType::RIGHT_PAREN)) {
        advance();
        // empty tuple -> maybe unit
        auto e = std::make_shared<Expr>(ExprKind::TUPLE);
        e->line = lp.line;
        return e;
    }
    auto first = parseExpression();
    if (!first) {
        consume(TokenType::RIGHT_PAREN, "expected ')'");
        auto e = std::make_shared<Expr>(ExprKind::TUPLE);
        e->line = lp.line;
        return e;
    }
    if (check(TokenType::COMMA)) {
        // tuple
        std::vector<ExprPtr> elems;
        elems.push_back(first);
        while (match(TokenType::COMMA)) {
            skipNewlines();
            if (check(TokenType::RIGHT_PAREN)) break;
            auto nxt = parseExpression();
            if (nxt) elems.push_back(nxt);
            skipNewlines();
        }
        consume(TokenType::RIGHT_PAREN, "expected ')' after tuple");
        auto e = std::make_shared<Expr>(ExprKind::TUPLE);
        e->line = lp.line;
        e->elements = elems;
        return e;
    } else {
        // grouping
        consume(TokenType::RIGHT_PAREN, "expected ')' after grouping");
        auto e = std::make_shared<Expr>(ExprKind::GROUPING);
        e->line = lp.line;
        e->left = first;
        return e;
    }
}

} // namespace cotton
