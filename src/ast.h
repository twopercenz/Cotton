#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include "token.h"

namespace cotton {

struct Expr;
struct Stmt;
using ExprPtr = std::shared_ptr<Expr>;
using StmtPtr = std::shared_ptr<Stmt>;

struct Pattern;
using PatternPtr = std::shared_ptr<Pattern>;

enum class PatternKind { WILDCARD, LITERAL, IDENT, TUPLE, ARRAY, STRUCT, ENUM_VARIANT, OR, RANGE, REST };

struct Pattern {
    PatternKind kind = PatternKind::WILDCARD;
    std::string ident; // for IDENT or ENUM_VARIANT name, struct name etc.
    std::vector<PatternPtr> sub; // for tuple/array/or
    std::unordered_map<std::string, PatternPtr> fields; // struct
    ExprPtr guardExpr; // for if guard in match (not pattern but separate)
    Token literalToken = Token(TokenType::UNKNOWN,"",0,0);
    bool isMut = false;
    bool isRest = false; // for ...rest
    // range
    PatternPtr rangeStart, rangeEnd;
    bool rangeInclusive = false;
    Pattern(PatternKind k): kind(k), literalToken(TokenType::UNKNOWN,"",0,0) {}
};

enum class ExprKind {
    LITERAL, IDENT, BINARY, UNARY, BLOCK, IF, FOR, WHILE, LOOP,
    BREAK, CONTINUE, RETURN, CALL, INDEX, FIELD, ARRAY, DICT, TUPLE,
    CLOSURE, MATCH, RANGE, TEMPLATE_STRING, COMPREHENSION,
    STRUCT_LITERAL, ASSIGN, GROUPING,
    VAR_REF, // &x &mut x
    BOX, UNSAFE_BLOCK, SPAWN, AWAIT, RESULT_PROPAGATION, OPTIONAL_CHAIN, COALESCE
};

struct MatchArm {
    PatternPtr pattern;
    ExprPtr guard; // optional if condition
    ExprPtr body;
};

struct ComprehensionClause {
    std::string var;
    PatternPtr pattern; // for for x in y could be pattern
    ExprPtr iterable;
    ExprPtr condition; // optional
};

struct Expr {
    ExprKind kind;
    int line = 0;
    // literal
    Token literal;
    std::string strValue;
    // ident
    std::string identName;
    // binary
    Token op;
    ExprPtr left, right;
    // unary
    ExprPtr unaryExpr;
    // block
    std::vector<StmtPtr> stmts;
    // if
    ExprPtr cond, thenBranch, elseBranch;
    // for
    std::optional<std::string> label;
    PatternPtr forPattern;
    ExprPtr forIter;
    ExprPtr forBody;
    // while - uses cond + forBody
    // loop - forBody
    // break/continue
    std::optional<std::string> breakLabel;
    ExprPtr breakValue;
    // call
    ExprPtr callee;
    std::vector<ExprPtr> args;
    // index - left = object, right = index etc? reuse object+args or index expr
    ExprPtr index;
    // field
    std::string fieldName;
    ExprPtr object;
    // array/dict/tuple
    std::vector<ExprPtr> elements;
    std::vector<std::pair<ExprPtr, ExprPtr>> dictEntries;
    // closure
    std::vector<std::string> params;
    ExprPtr closureBody;
    bool closureIsBlock = false;
    // match
    ExprPtr matchTarget;
    std::vector<MatchArm> matchArms;
    // range
    ExprPtr rangeStart, rangeEnd;
    bool rangeInclusive = false;
    // template string
    struct TemplatePart { bool isExpr; std::string text; ExprPtr expr; };
    std::vector<TemplatePart> templateParts;
    // comprehension
    ExprPtr compExpr;
    std::vector<ComprehensionClause> compClauses;
    bool isDictComp = false;
    ExprPtr compKey; // for dict
    // struct literal
    std::string structName;
    std::vector<std::pair<std::string, ExprPtr>> structFields;
    bool isAnonymousStruct = true;
    // assign
    Token assignOp;
    ExprPtr assignTarget;
    ExprPtr assignValue;
    // reference
    bool isMutRef = false;

    Expr(ExprKind k): kind(k), literal(TokenType::UNKNOWN,"",0,0), op(TokenType::UNKNOWN,"",0,0), assignOp(TokenType::UNKNOWN,"",0,0) {}
};

enum class StmtKind {
    LET, CONST, FN, STRUCT, ENUM, IMPL, TRAIT, IMPORT, MODULE, EXPR, RETURN, BREAK, CONTINUE
};

struct FnParam {
    std::string name;
    std::string typeName;
    bool isMut = false;
};

struct StructFieldDef {
    std::string name;
    std::string typeName;
    bool pub = false;
};

struct StructDef {
    std::string name;
    bool pub = false;
    std::vector<StructFieldDef> fields;
    std::vector<std::string> genericParams;
};

struct EnumVariantDef {
    std::string name;
    std::vector<std::string> fieldTypes;
    std::vector<std::pair<std::string, std::string>> namedFields; // optional
};

struct EnumDef {
    std::string name;
    bool pub = false;
    std::vector<EnumVariantDef> variants;
    std::vector<std::string> genericParams;
};

struct Stmt {
    StmtKind kind;
    int line=0;
    // let
    bool isMut = false;
    PatternPtr letPattern;
    std::string letName;
    std::optional<std::string> typeAnnotation;
    ExprPtr init;
    // const
    std::string constName;
    ExprPtr constValue;
    // fn
    std::string fnName;
    bool isPub = false;
    bool isAsync = false;
    std::vector<FnParam> fnParams;
    std::optional<std::string> returnType;
    std::vector<std::string> genericParams;
    ExprPtr fnBody;
    // struct
    std::shared_ptr<StructDef> structDef;
    // enum
    std::shared_ptr<EnumDef> enumDef;
    // impl
    std::string implTarget;
    std::string implTrait;
    std::vector<StmtPtr> implMethods;
    // trait
    std::string traitName;
    std::vector<StmtPtr> traitMethods;
    // import
    std::string importPath;
    std::vector<std::string> importNames;
    bool isFromImport = false;
    // module
    std::string moduleName;
    std::vector<StmtPtr> moduleStmts;
    // expr stmt
    ExprPtr expr;
    // return
    ExprPtr returnExpr;
    std::optional<std::string> breakLabelOpt;
    Stmt(StmtKind k): kind(k) {}
};

struct Program {
    std::vector<StmtPtr> statements;
};

} // namespace cotton
