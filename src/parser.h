#pragma once
#include "token.h"
#include "ast.h"
#include <vector>
#include <string>

namespace cotton {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    Program parseProgram();

    bool hadError = false;

private:
    std::vector<Token> tokens;
    size_t current = 0;

    Token peek() const;
    Token previous() const;
    Token advance();
    bool isAtEnd() const;
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool match(std::vector<TokenType> types);
    Token consume(TokenType type, const std::string& message);
    void synchronize();
    void error(const Token& token, const std::string& message);
    void skipNewlines();

    // statements
    StmtPtr parseDeclaration();
    StmtPtr parseStatement();
    StmtPtr parseLetDecl();
    StmtPtr parseConstDecl();
    StmtPtr parseFnDecl(bool isPub=false, bool isAsync=false);
    StmtPtr parseStructDecl(bool isPub=false);
    StmtPtr parseEnumDecl(bool isPub=false);
    StmtPtr parseImplDecl();
    StmtPtr parseTraitDecl(bool isPub=false);
    StmtPtr parseImportDecl();
    StmtPtr parseModuleDecl();

    // patterns
    PatternPtr parsePattern();
    PatternPtr parseOrPattern();
    PatternPtr parseSinglePattern();
    PatternPtr parseTupleOrParenPattern();
    PatternPtr parseArrayPattern();

    // expressions
    ExprPtr parseExpression();
    ExprPtr parseAssignment();
    ExprPtr parseCoalesce(); // ??
    ExprPtr parseOr();
    ExprPtr parseAnd();
    ExprPtr parseEquality();
    ExprPtr parseComparison();
    ExprPtr parseRange();
    ExprPtr parseTerm();
    ExprPtr parseFactor();
    ExprPtr parsePower();
    ExprPtr parseUnary();
    ExprPtr parsePostfix();
    ExprPtr parseCallChain();
    ExprPtr parsePrimary();

    ExprPtr parseBlock(); // { stmts }
    ExprPtr parseIfExpr();
    ExprPtr parseForExpr(std::optional<std::string> label);
    ExprPtr parseWhileExpr(std::optional<std::string> label);
    ExprPtr parseLoopExpr(std::optional<std::string> label);
    ExprPtr parseMatchExpr();
    ExprPtr parseClosureOrPipe();
    ExprPtr parseArrayLiteralOrComprehension();
    ExprPtr parseDictLiteralOrComprehensionOrStruct();
    ExprPtr parseTemplateString(const Token& tok);
    ExprPtr parseParenOrTuple();

    std::vector<FnParam> parseFnParams();
    std::vector<std::string> parseGenericParams();
    std::string parseTypeName();

    // helpers
    bool checkNext(TokenType type) const;
    Token peekNext() const;
    bool isLabel(); // IDENT COLON followed by for/while/loop
    std::optional<std::string> parseOptionalLabel();
};

} // namespace cotton
