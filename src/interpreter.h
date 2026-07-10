#pragma once
#include "ast.h"
#include "value.h"
#include <unordered_map>
#include <vector>
#include <stdexcept>

namespace cotton {

struct BreakSignal : public std::exception {
    std::optional<std::string> label;
    ValuePtr value;
    BreakSignal(std::optional<std::string> l, ValuePtr v=nullptr): label(l), value(v) {}
};
struct ContinueSignal : public std::exception {
    std::optional<std::string> label;
    ContinueSignal(std::optional<std::string> l): label(l) {}
};
struct ReturnSignal : public std::exception {
    ValuePtr value;
    ReturnSignal(ValuePtr v): value(v) {}
};
struct PropagateErrSignal : public std::exception {
    ValuePtr err;
    PropagateErrSignal(ValuePtr e): err(e) {}
};

class Interpreter {
public:
    Interpreter();
    ValuePtr interpret(const Program& program);
    void registerBuiltin(const std::string& name, std::function<ValuePtr(std::vector<ValuePtr>, EnvPtr)> fn);

    // For REPL or testing
    ValuePtr evalExpr(ExprPtr expr, EnvPtr env);
    ValuePtr evalStmt(StmtPtr stmt, EnvPtr env);
    ValuePtr evalBlockExpr(ExprPtr block, EnvPtr env);
    ValuePtr evalBlockStmts(const std::vector<StmtPtr>& stmts, EnvPtr env);

    EnvPtr globalEnv;

private:
    // Type registries
    std::unordered_map<std::string, std::shared_ptr<StructDef>> structDefs;
    std::unordered_map<std::string, std::shared_ptr<EnumDef>> enumDefs;
    // typeName -> methodName -> function (UserFunction)
    std::unordered_map<std::string, std::unordered_map<std::string, std::shared_ptr<UserFunction>>> implMethods;
    // for trait default? ignore

    ValuePtr evalExpression(ExprPtr expr, EnvPtr env);
    ValuePtr evalIdentifier(const std::string& name, EnvPtr env, int line);
    ValuePtr callFunction(ValuePtr func, const std::vector<ExprPtr>& argExprs, std::vector<ValuePtr> evaluatedArgs, EnvPtr env, bool isMethodCall, ValuePtr selfObject);
    ValuePtr callUserFunction(std::shared_ptr<UserFunction> uf, std::vector<ValuePtr> args, EnvPtr callerEnv);

    // pattern matching
    bool matchPattern(PatternPtr pat, ValuePtr value, EnvPtr env, bool isMutBinding=false);
    void bindPattern(PatternPtr pat, ValuePtr value, EnvPtr env);

    // helpers
    ValuePtr evalArray(const std::vector<ExprPtr>& elems, EnvPtr env);
    ValuePtr evalTuple(const std::vector<ExprPtr>& elems, EnvPtr env);
    ValuePtr evalRange(ExprPtr expr, EnvPtr env);
    ValuePtr evalTemplate(ExprPtr expr, EnvPtr env);
    ValuePtr evalComprehension(ExprPtr expr, EnvPtr env);
    ValuePtr evalStructLiteral(ExprPtr expr, EnvPtr env);
    ValuePtr evalFieldAccess(ExprPtr expr, EnvPtr env, bool forCall, ValuePtr* outObject, std::string* outField);
    ValuePtr lookupFieldOrMethod(ValuePtr object, const std::string& fieldName);

    bool valuesEqual(ValuePtr a, ValuePtr b);
    ValuePtr evalBinary(ExprPtr expr, EnvPtr env);
    ValuePtr evalUnary(ExprPtr expr, EnvPtr env);
    ValuePtr evalIf(ExprPtr expr, EnvPtr env);
    ValuePtr evalFor(ExprPtr expr, EnvPtr env);
    ValuePtr evalWhile(ExprPtr expr, EnvPtr env);
    ValuePtr evalLoop(ExprPtr expr, EnvPtr env);
    ValuePtr evalMatch(ExprPtr expr, EnvPtr env);
    ValuePtr evalClosure(ExprPtr expr, EnvPtr env);

    void defineStruct(std::shared_ptr<StructDef> def, EnvPtr env);
    void defineEnum(std::shared_ptr<EnumDef> def, EnvPtr env);
    void defineImpl(StmtPtr implStmt, EnvPtr env);

    void installBuiltins();
    void runtimeError(int line, const std::string& msg);

    // ownership tracking
    bool isCopyValue(ValuePtr v) { return v && v->isCopyType(); }
    void maybeMoveFromIdentifier(ExprPtr expr, EnvPtr env);
};

} // namespace cotton
