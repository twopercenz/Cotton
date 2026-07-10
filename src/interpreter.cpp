#include "interpreter.h"
#include <iostream>
#include <cmath>
#include <algorithm>

namespace cotton {

Interpreter::Interpreter() {
    globalEnv = std::make_shared<Env>(nullptr);
    installBuiltins();
}

void Interpreter::runtimeError(int line, const std::string& msg) {
    std::cerr << "[Runtime Error] line " << line << ": " << msg << "\n";
    throw std::runtime_error(msg);
}

void Interpreter::registerBuiltin(const std::string& name, std::function<ValuePtr(std::vector<ValuePtr>, EnvPtr)> fn) {
    auto b = std::make_shared<BuiltinFunction>();
    b->name = name;
    b->fn = fn;
    auto v = std::make_shared<Value>(ValueKind::BUILTIN);
    v->builtinFn = b;
    globalEnv->define(name, v, false);
}

void Interpreter::installBuiltins() {
    registerBuiltin("print", [](std::vector<ValuePtr> args, EnvPtr) -> ValuePtr {
        for (size_t i=0;i<args.size();++i){
            if (i) std::cout << " ";
            std::cout << args[i]->toString();
        }
        std::cout << "\n";
        return Value::makeNil();
    });
    registerBuiltin("println", [](std::vector<ValuePtr> args, EnvPtr) -> ValuePtr {
        for (size_t i=0;i<args.size();++i){
            if (i) std::cout << " ";
            std::cout << args[i]->toString();
        }
        std::cout << "\n";
        return Value::makeNil();
    });
    registerBuiltin("len", [](std::vector<ValuePtr> args, EnvPtr) -> ValuePtr {
        if (args.empty()) return Value::makeInt(0);
        auto v = args[0];
        if (v->kind==ValueKind::ARRAY || v->kind==ValueKind::TUPLE) return Value::makeInt(v->elements.size());
        if (v->kind==ValueKind::STRING) return Value::makeInt(v->strVal.size());
        if (v->kind==ValueKind::DICT) return Value::makeInt(v->dictStringMap.size()+v->dictEntries.size());
        return Value::makeInt(0);
    });
    registerBuiltin("sqrt", [](std::vector<ValuePtr> args, EnvPtr) -> ValuePtr {
        if (args.empty()) return Value::makeFloat(0);
        double d = 0;
        if (args[0]->kind==ValueKind::FLOAT) d=args[0]->floatVal;
        else if (args[0]->kind==ValueKind::INT) d=(double)args[0]->intVal;
        return Value::makeFloat(std::sqrt(d));
    });
    // math module simulated via global? We'll allow math::sqrt via field lookup fallback? For now add "math" as struct with method? Simpler: ignore import, define math global dict?
    // We'll create a dummy math object with sqrt method?
    // We'll handle via builtin lookup for field "sqrt" on float value.

    registerBuiltin("Box", [](std::vector<ValuePtr> args, EnvPtr) -> ValuePtr {
        if (args.empty()) return Value::makeBox(Value::makeNil());
        return Value::makeBox(args[0]);
    });

    // Result helpers
    registerBuiltin("Ok", [](std::vector<ValuePtr> args, EnvPtr) -> ValuePtr {
        if (args.empty()) return Value::makeOk(Value::makeNil());
        return Value::makeOk(args[0]);
    });
    registerBuiltin("Err", [](std::vector<ValuePtr> args, EnvPtr) -> ValuePtr {
        if (args.empty()) return Value::makeErr("error");
        if (args[0]->kind==ValueKind::STRING) return Value::makeErr(args[0]->strVal);
        return Value::makeErr(args[0]);
    });
    registerBuiltin("Some", [](std::vector<ValuePtr> args, EnvPtr) -> ValuePtr {
        if (args.empty()) return Value::makeNone();
        return Value::makeSome(args[0]);
    });
}

ValuePtr Interpreter::interpret(const Program& program) {
    try {
        // First pass: register structs, enums, impls, functions
        for (auto& stmt : program.statements) {
            if (stmt->kind==StmtKind::STRUCT) {
                defineStruct(stmt->structDef, globalEnv);
            } else if (stmt->kind==StmtKind::ENUM) {
                defineEnum(stmt->enumDef, globalEnv);
            } else if (stmt->kind==StmtKind::FN) {
                // define function
                auto uf = std::make_shared<UserFunction>();
                uf->name = stmt->fnName;
                uf->params = stmt->fnParams;
                uf->body = stmt->fnBody;
                uf->closureEnv = globalEnv;
                uf->genericParams = stmt->genericParams;
                auto v = std::make_shared<Value>(ValueKind::FUNCTION);
                v->userFn = uf;
                globalEnv->define(stmt->fnName, v, false);
            } else if (stmt->kind==StmtKind::IMPL) {
                defineImpl(stmt, globalEnv);
            } else if (stmt->kind==StmtKind::CONST) {
                auto val = evalExpression(stmt->constValue, globalEnv);
                globalEnv->define(stmt->constName, val, false, true);
            } else if (stmt->kind==StmtKind::IMPORT) {
                // no-op for now
            } else if (stmt->kind==StmtKind::MODULE) {
                // todo: module env? For now evaluate inside global
                for (auto& sub : stmt->moduleStmts) {
                    if (sub->kind==StmtKind::STRUCT) defineStruct(sub->structDef, globalEnv);
                    else if (sub->kind==StmtKind::FN) {
                        auto uf = std::make_shared<UserFunction>();
                        uf->name = sub->fnName;
                        uf->params = sub->fnParams;
                        uf->body = sub->fnBody;
                        uf->closureEnv = globalEnv;
                        auto v = std::make_shared<Value>(ValueKind::FUNCTION);
                        v->userFn = uf;
                        globalEnv->define(sub->fnName, v, false);
                    }
                }
            }
        }
        // Second pass: evaluate let/expr statements and look for main
        ValuePtr result = Value::makeNil();
        for (auto& stmt : program.statements) {
            if (stmt->kind==StmtKind::LET || stmt->kind==StmtKind::EXPR || stmt->kind==StmtKind::RETURN) {
                result = evalStmt(stmt, globalEnv);
            }
        }
        // If there is main function, call it
        auto mainInfo = globalEnv->getVarInfo("main");
        if (mainInfo && mainInfo->value && mainInfo->value->kind==ValueKind::FUNCTION) {
            auto mainFn = mainInfo->value->userFn;
            result = callUserFunction(mainFn, {}, globalEnv);
        }
        return result;
    } catch (PropagateErrSignal& e) {
        std::cerr << "Unhandled error propagated: " << e.err->toString() << "\n";
        return e.err;
    } catch (std::runtime_error& e) {
        std::cerr << "Aborted due to error: " << e.what() << "\n";
        return Value::makeNil();
    }
}

void Interpreter::defineStruct(std::shared_ptr<StructDef> def, EnvPtr env) {
    structDefs[def->name] = def;
    // Also define a constructor function? So Point{...} works via struct literal, but Point(x,y) not needed.
    // We'll leave.
}
void Interpreter::defineEnum(std::shared_ptr<EnumDef> def, EnvPtr env) {
    enumDefs[def->name] = def;
    // Define variants as functions that construct enum variant values
    for (auto& variant : def->variants) {
        auto builtin = std::make_shared<BuiltinFunction>();
        std::string fullName = variant.name;
        // If enum has name, variant constructor accessible as VariantName? Or Enum::Variant? For Cotton syntax Shape::Circle or just Circle(...). The parser produces enum variant pattern but construction? In example they used Rectangle(w,h) etc inside match. Construction likely Shape::Circle(r) or Circle(r). We'll define global function for each variant that creates enum variant.
        builtin->name = fullName;
        std::string enumName = def->name;
        std::string varName = variant.name;
        builtin->fn = [enumName, varName](std::vector<ValuePtr> args, EnvPtr) -> ValuePtr {
            auto v = std::make_shared<Value>(ValueKind::ENUM_VARIANT);
            v->enumName = enumName;
            v->variantName = varName;
            v->variantFields = args;
            return v;
        };
        auto v = std::make_shared<Value>(ValueKind::BUILTIN);
        v->builtinFn = builtin;
        env->define(fullName, v, false);
        // Also define qualified name Enum::Variant? We'll define with ::? Env lookup doesn't support ::, so we skip. But interpreter field lookup might handle.
    }
}
void Interpreter::defineImpl(StmtPtr implStmt, EnvPtr env) {
    std::string target = implStmt->implTarget;
    if (target.empty()) return;
    for (auto& methodStmt : implStmt->implMethods) {
        if (methodStmt->kind!=StmtKind::FN) continue;
        auto uf = std::make_shared<UserFunction>();
        uf->name = methodStmt->fnName;
        uf->params = methodStmt->fnParams;
        uf->body = methodStmt->fnBody;
        uf->closureEnv = env;
        uf->genericParams = methodStmt->genericParams;
        uf->isMethod = true;
        implMethods[target][methodStmt->fnName] = uf;
    }
}

ValuePtr Interpreter::evalStmt(StmtPtr stmt, EnvPtr env) {
    if (!stmt) return Value::makeNil();
    switch(stmt->kind) {
        case StmtKind::LET: {
            ValuePtr initVal = Value::makeNil();
            if (stmt->init) {
                initVal = evalExpression(stmt->init, env);
            }
            // ownership move: if init is identifier that is not copy, mark moved
            if (stmt->letPattern) {
                // Try to bind pattern
                if (!matchPattern(stmt->letPattern, initVal, env, stmt->isMut)) {
                    // if pattern matching fails, attempt direct binding via bindPattern which will handle destructuring
                    bindPattern(stmt->letPattern, initVal, env);
                } else {
                    // In matchPattern we already bound? Let's also bind for simple ident case: if pattern is IDENT, define.
                    // Our matchPattern for IDENT does define.
                }
            } else if (!stmt->letName.empty()) {
                env->define(stmt->letName, initVal, stmt->isMut);
            }
            return Value::makeNil();
        }
        case StmtKind::CONST: {
            auto val = evalExpression(stmt->constValue, env);
            env->define(stmt->constName, val, false, true);
            return Value::makeNil();
        }
        case StmtKind::EXPR: {
            if (stmt->expr) {
                return evalExpression(stmt->expr, env);
            }
            return Value::makeNil();
        }
        case StmtKind::RETURN: {
            ValuePtr v = Value::makeNil();
            if (stmt->returnExpr) v = evalExpression(stmt->returnExpr, env);
            throw ReturnSignal(v);
        }
        case StmtKind::BREAK: {
            throw BreakSignal(stmt->breakLabelOpt, nullptr);
        }
        case StmtKind::CONTINUE: {
            throw ContinueSignal(stmt->breakLabelOpt);
        }
        default:
            // FN, STRUCT etc already handled
            return Value::makeNil();
    }
}

ValuePtr Interpreter::evalBlockExpr(ExprPtr block, EnvPtr env) {
    if (!block || block->kind!=ExprKind::BLOCK) return Value::makeNil();
    return evalBlockStmts(block->stmts, env);
}
ValuePtr Interpreter::evalBlockStmts(const std::vector<StmtPtr>& stmts, EnvPtr env) {
    EnvPtr blockEnv = std::make_shared<Env>(env);
    ValuePtr last = Value::makeNil();
    for (size_t i=0;i<stmts.size();++i) {
        auto s = stmts[i];
        try {
            auto v = evalStmt(s, blockEnv);
            // if EXPR stmt, its value is result of its expression
            if (s->kind==StmtKind::EXPR && s->expr) {
                last = v;
            } else if (s->kind==StmtKind::LET) {
                last = Value::makeNil();
            } else {
                // For other statements that return value? Keep last
            }
        } catch (BreakSignal& b) { throw; }
          catch (ContinueSignal& c) { throw; }
          catch (ReturnSignal& r) { throw; }
          catch (PropagateErrSignal& e) { throw; }
    }
    // For Cotton, block value is last expression's value. If stmts empty, nil.
    // Our loop captures last expr stmt value, but we also need to handle case where block contains only expression without let? We have last.
    // However if last statement was itself a block that returns value, we preserved?
    // Better: if stmts not empty and last stmt is EXPR, we already returned its eval as last. If block also has implicit return? We'll return last.
    return last;
}

ValuePtr Interpreter::evalExpr(ExprPtr expr, EnvPtr env) {
    return evalExpression(expr, env);
}

void Interpreter::maybeMoveFromIdentifier(ExprPtr expr, EnvPtr env) {
    if (!expr) return;
    if (expr->kind==ExprKind::IDENT) {
        auto info = env->getVarInfo(expr->identName);
        if (info && !isCopyValue(info->value) && info->value->kind!=ValueKind::REF && info->value->kind!=ValueKind::MUT_REF) {
            // mark moved
            env->markMoved(expr->identName);
        }
    }
}

ValuePtr Interpreter::evalExpression(ExprPtr expr, EnvPtr env) {
    if (!expr) return Value::makeNil();
    switch(expr->kind) {
        case ExprKind::LITERAL: {
            Token t = expr->literal;
            switch(t.type) {
                case TokenType::NUMBER: {
                    std::string s = t.lexeme;
                    if (s.find('.')!=std::string::npos || s.find('e')!=std::string::npos || s.find('E')!=std::string::npos) {
                        try { double d = std::stod(s); return Value::makeFloat(d); } catch(...) { return Value::makeFloat(0); }
                    } else {
                        try { int64_t i = std::stoll(s); return Value::makeInt(i); } catch(...) { return Value::makeInt(0); }
                    }
                }
                case TokenType::STRING: return Value::makeString(t.lexeme);
                case TokenType::MULTILINE_STRING: return Value::makeString(t.lexeme);
                case TokenType::TRUE: return Value::makeBool(true);
                case TokenType::FALSE: return Value::makeBool(false);
                case TokenType::NONE: return Value::makeNone();
                default: return Value::makeNil();
            }
        }
        case ExprKind::IDENT: {
            return evalIdentifier(expr->identName, env, expr->line);
        }
        case ExprKind::GROUPING: {
            return evalExpression(expr->left, env);
        }
        case ExprKind::BLOCK: {
            return evalBlockExpr(expr, env);
        }
        case ExprKind::ARRAY: {
            // check for spread handling: elements may contain UNARY with DOT_DOT_DOT
            std::vector<ValuePtr> elems;
            for (auto& el : expr->elements) {
                if (el->kind==ExprKind::UNARY && el->op.type==TokenType::DOT_DOT_DOT) {
                    auto spreadVal = evalExpression(el->unaryExpr, env);
                    if (spreadVal->kind==ValueKind::ARRAY || spreadVal->kind==ValueKind::TUPLE) {
                        for (auto& e : spreadVal->elements) elems.push_back(e);
                    } else {
                        elems.push_back(spreadVal);
                    }
                } else {
                    elems.push_back(evalExpression(el, env));
                }
            }
            return Value::makeArray(elems);
        }
        case ExprKind::TUPLE: {
            std::vector<ValuePtr> elems;
            for (auto& el : expr->elements) elems.push_back(evalExpression(el, env));
            return Value::makeTuple(elems);
        }
        case ExprKind::DICT: {
            auto dict = std::make_shared<Value>(ValueKind::DICT);
            for (auto& kv : expr->dictEntries) {
                auto k = evalExpression(kv.first, env);
                auto v = evalExpression(kv.second, env);
                // if key is string, put in string map for faster access
                if (k->kind==ValueKind::STRING) dict->dictStringMap[k->strVal]=v;
                else dict->dictEntries.push_back({k,v});
            }
            return dict;
        }
        case ExprKind::TEMPLATE_STRING: {
            return evalTemplate(expr, env);
        }
        case ExprKind::COMPREHENSION: {
            return evalComprehension(expr, env);
        }
        case ExprKind::STRUCT_LITERAL: {
            return evalStructLiteral(expr, env);
        }
        case ExprKind::RANGE: {
            return evalRange(expr, env);
        }
        case ExprKind::BINARY: {
            return evalBinary(expr, env);
        }
        case ExprKind::UNARY: {
            return evalUnary(expr, env);
        }
        case ExprKind::ASSIGN: {
            // assignTarget can be IDENT, FIELD, INDEX, or pattern? For tuple pattern assignment? For Cotton let mut present, but assignment to variable.
            auto target = expr->assignTarget;
            auto value = evalExpression(expr->assignValue, env);
            // handle operator like +=
            std::string opLex = expr->assignOp.lexeme;
            if (opLex!="=") {
                // need to read current value and apply op
                ValuePtr cur = nullptr;
                if (target->kind==ExprKind::IDENT) cur = evalIdentifier(target->identName, env, expr->line);
                else if (target->kind==ExprKind::FIELD) {
                    auto obj = evalExpression(target->object, env);
                    if (obj->kind==ValueKind::STRUCT_INSTANCE) {
                        auto it = obj->fields.find(target->fieldName);
                        if (it!=obj->fields.end()) cur = it->second;
                    }
                } else if (target->kind==ExprKind::INDEX) {
                    // todo
                }
                // apply
                if (cur) {
                    // crude: create binary expr
                    auto bin = std::make_shared<Expr>(ExprKind::BINARY);
                    bin->left = std::make_shared<Expr>(ExprKind::LITERAL); bin->left->literal = Token(TokenType::NUMBER, cur->toString(), 0,0);
                    bin->right = std::make_shared<Expr>(ExprKind::LITERAL);
                    bin->right->literal = Token(TokenType::NUMBER, value->toString(), 0,0);
                    // Instead just handle int/float addition directly
                    // We'll do manual addition for simplicity
                    if (opLex=="+=") {
                        if (cur->kind==ValueKind::INT && value->kind==ValueKind::INT) value = Value::makeInt(cur->intVal + value->intVal);
                        else if (cur->kind==ValueKind::FLOAT || value->kind==ValueKind::FLOAT) {
                            double a = cur->kind==ValueKind::FLOAT ? cur->floatVal : (double)cur->intVal;
                            double b = value->kind==ValueKind::FLOAT ? value->floatVal : (double)value->intVal;
                            value = Value::makeFloat(a+b);
                        } else if (cur->kind==ValueKind::STRING) {
                            value = Value::makeString(cur->strVal + value->toString());
                        }
                    } else if (opLex=="-=") {
                        if (cur->kind==ValueKind::INT && value->kind==ValueKind::INT) value = Value::makeInt(cur->intVal - value->intVal);
                        else {
                            double a = cur->kind==ValueKind::FLOAT ? cur->floatVal : (double)cur->intVal;
                            double b = value->kind==ValueKind::FLOAT ? value->floatVal : (double)value->intVal;
                            value = Value::makeFloat(a-b);
                        }
                    }
                }
            }

            if (target->kind==ExprKind::IDENT) {
                auto info = env->getVarInfo(target->identName);
                if (!info) runtimeError(expr->line, "Undefined variable '" + target->identName + "'");
                if (!info->isMut) runtimeError(expr->line, "Cannot assign to immutable variable '" + target->identName + "'");
                info->value = value;
                return value;
            } else if (target->kind==ExprKind::FIELD) {
                auto obj = evalExpression(target->object, env);
                if (obj->kind==ValueKind::STRUCT_INSTANCE) {
                    obj->fields[target->fieldName] = value;
                    return value;
                } else {
                    runtimeError(expr->line, "Field assignment on non-struct");
                }
            } else if (target->kind==ExprKind::INDEX) {
                auto obj = evalExpression(target->object, env);
                auto idx = evalExpression(target->index, env);
                if (obj->kind==ValueKind::ARRAY || obj->kind==ValueKind::TUPLE) {
                    int64_t i = idx->kind==ValueKind::INT ? idx->intVal : 0;
                    if (i<0) i = obj->elements.size() + i;
                    if (i>=0 && i < (int64_t)obj->elements.size()) {
                        obj->elements[i] = value;
                        return value;
                    } else runtimeError(expr->line, "Index out of bounds");
                }
            } else if (target->kind==ExprKind::TUPLE) {
                // destructuring assignment: (a,b)=...
                // Need to bind pattern already? We'll attempt to bind
                // For simplicity, eval RHS as tuple and assign to identifiers in tuple pattern
                auto rhs = value;
                if (rhs->kind!=ValueKind::TUPLE && rhs->kind!=ValueKind::ARRAY) runtimeError(expr->line, "Tuple assignment expected tuple");
                for (size_t i=0;i<target->elements.size() && i<rhs->elements.size();++i){
                    auto subTarget = target->elements[i];
                    if (subTarget->kind==ExprKind::IDENT) {
                        auto info = env->getVarInfo(subTarget->identName);
                        if (info) info->value = rhs->elements[i];
                        else env->define(subTarget->identName, rhs->elements[i], true);
                    }
                }
                return value;
            }
            runtimeError(expr->line, "Invalid assignment target");
            return Value::makeNil();
        }
        case ExprKind::CALL: {
            // evaluate callee: could be field access chain needed for method
            // For method call detection: if callee is FIELD expr
            if (expr->callee->kind==ExprKind::FIELD) {
                // method call
                auto objExpr = expr->callee->object;
                std::string methodName = expr->callee->fieldName;
                ValuePtr objVal = evalExpression(objExpr, env);
                // evaluate args
                std::vector<ValuePtr> argsVals;
                for (auto& a: expr->args) argsVals.push_back(evalExpression(a, env));

                // Check builtin handling for float sqrt etc
                if (objVal->kind==ValueKind::FLOAT && methodName=="sqrt") {
                    return Value::makeFloat(std::sqrt(objVal->floatVal));
                }
                if (objVal->kind==ValueKind::INT && methodName=="sqrt") {
                    return Value::makeFloat(std::sqrt((double)objVal->intVal));
                }
                if ((objVal->kind==ValueKind::ARRAY || objVal->kind==ValueKind::TUPLE) && methodName=="len") {
                    return Value::makeInt(objVal->elements.size());
                }
                if (objVal->kind==ValueKind::STRING && methodName=="len") {
                    return Value::makeInt(objVal->strVal.size());
                }
                // Check struct impl methods
                std::string typeName;
                if (objVal->kind==ValueKind::STRUCT_INSTANCE) typeName = objVal->structName;
                // For primitive, method lookup? For now only struct
                auto itType = implMethods.find(typeName);
                if (itType!=implMethods.end()) {
                    auto itMeth = itType->second.find(methodName);
                    if (itMeth!=itType->second.end()) {
                        // call with self as first arg
                        std::vector<ValuePtr> finalArgs;
                        finalArgs.push_back(objVal);
                        finalArgs.insert(finalArgs.end(), argsVals.begin(), argsVals.end());
                        return callUserFunction(itMeth->second, finalArgs, env);
                    }
                }
                // Check field that is function? For struct field that holds closure?
                if (objVal->kind==ValueKind::STRUCT_INSTANCE) {
                    auto fit = objVal->fields.find(methodName);
                    if (fit!=objVal->fields.end()) {
                        auto fnVal = fit->second;
                        if (fnVal->kind==ValueKind::FUNCTION || fnVal->kind==ValueKind::BUILTIN) {
                            // call it
                            if (fnVal->kind==ValueKind::BUILTIN) {
                                return fnVal->builtinFn->fn(argsVals, env);
                            } else {
                                return callUserFunction(fnVal->userFn, argsVals, env);
                            }
                        }
                    }
                }
                // If method not found, maybe object has field that is struct with methods? Could be type associated function?
                // Fallback: try to look up global function with name methodName? Not.

                // If we still not found, try to evaluate callee as normal field then call?
                ValuePtr calleeVal = lookupFieldOrMethod(objVal, methodName);
                if (calleeVal) {
                    if (calleeVal->kind==ValueKind::FUNCTION) {
                        return callUserFunction(calleeVal->userFn, argsVals, env);
                    } else if (calleeVal->kind==ValueKind::BUILTIN) {
                        return calleeVal->builtinFn->fn(argsVals, env);
                    }
                }

                runtimeError(expr->line, "Method '" + methodName + "' not found for type '" + typeName + "'");
                return Value::makeNil();
            } else {
                // regular call
                ValuePtr calleeVal = evalExpression(expr->callee, env);
                std::vector<ValuePtr> argsVals;
                for (auto& a: expr->args) {
                    argsVals.push_back(evalExpression(a, env));
                    // ownership move detection: if arg is identifier non-copy, mark moved
                    maybeMoveFromIdentifier(a, env);
                }
                if (!calleeVal) runtimeError(expr->line, "Call of non-function");
                if (calleeVal->kind==ValueKind::BUILTIN) {
                    return calleeVal->builtinFn->fn(argsVals, env);
                } else if (calleeVal->kind==ValueKind::FUNCTION) {
                    return callUserFunction(calleeVal->userFn, argsVals, env);
                } else {
                    runtimeError(expr->line, "Attempt to call non-function: " + calleeVal->toString());
                    return Value::makeNil();
                }
            }
        }
        case ExprKind::INDEX: {
            auto obj = evalExpression(expr->object, env);
            auto idx = evalExpression(expr->index, env);
            if (obj->kind==ValueKind::ARRAY || obj->kind==ValueKind::TUPLE) {
                int64_t i = 0;
                if (idx->kind==ValueKind::INT) i = idx->intVal;
                else runtimeError(expr->line, "Array index must be int");
                if (i < 0) i = obj->elements.size() + i;
                if (i <0 || i >= (int64_t)obj->elements.size()) runtimeError(expr->line, "Index out of bounds");
                auto val = obj->elements[i];
                // borrow tracking: if & borrowed? For now return value
                return val;
            } else if (obj->kind==ValueKind::DICT) {
                if (idx->kind==ValueKind::STRING) {
                    auto it = obj->dictStringMap.find(idx->strVal);
                    if (it!=obj->dictStringMap.end()) return it->second;
                    // search generic
                    for (auto& p: obj->dictEntries) {
                        if (valuesEqual(p.first, idx)) return p.second;
                    }
                    return Value::makeNone(); // not found => None? Could be error.
                } else {
                    for (auto& p: obj->dictEntries) if (valuesEqual(p.first, idx)) return p.second;
                    return Value::makeNone();
                }
            } else if (obj->kind==ValueKind::STRING) {
                int64_t i = idx->kind==ValueKind::INT ? idx->intVal : 0;
                if (i<0) i = obj->strVal.size()+i;
                if (i>=0 && i < (int64_t)obj->strVal.size()) return Value::makeString(std::string(1, obj->strVal[i]));
                runtimeError(expr->line, "String index out of bounds");
            }
            runtimeError(expr->line, "Indexing non-indexable type: " + obj->toString());
            return Value::makeNil();
        }
        case ExprKind::FIELD: {
            ValuePtr object = evalExpression(expr->object, env);
            ValuePtr res = lookupFieldOrMethod(object, expr->fieldName);
            if (res) return res;
            // If not found, try methods
            if (object->kind==ValueKind::STRUCT_INSTANCE) {
                auto itType = implMethods.find(object->structName);
                if (itType!=implMethods.end()) {
                    auto itMeth = itType->second.find(expr->fieldName);
                    if (itMeth!=itType->second.end()) {
                        auto v = std::make_shared<Value>(ValueKind::FUNCTION);
                        v->userFn = itMeth->second;
                        return v;
                    }
                }
            }
            // For float sqrt property? Return nil?
            runtimeError(expr->line, "Field '" + expr->fieldName + "' not found on " + object->toString());
            return Value::makeNil();
        }
        case ExprKind::IF: return evalIf(expr, env);
        case ExprKind::FOR: return evalFor(expr, env);
        case ExprKind::WHILE: return evalWhile(expr, env);
        case ExprKind::LOOP: return evalLoop(expr, env);
        case ExprKind::MATCH: return evalMatch(expr, env);
        case ExprKind::CLOSURE: return evalClosure(expr, env);
        case ExprKind::BOX: {
            auto inner = evalExpression(expr->unaryExpr, env);
            return Value::makeBox(inner);
        }
        case ExprKind::VAR_REF: {
            auto inner = evalExpression(expr->unaryExpr, env);
            return Value::makeRef(inner, expr->isMutRef);
        }
        case ExprKind::UNSAFE_BLOCK: {
            // just eval block
            return evalExpression(expr->unaryExpr, env);
        }
        case ExprKind::SPAWN: {
            // synchronous execution for now
            return evalExpression(expr->unaryExpr, env);
        }
        case ExprKind::AWAIT: {
            return evalExpression(expr->unaryExpr, env);
        }
        case ExprKind::RESULT_PROPAGATION: {
            auto val = evalExpression(expr->unaryExpr, env);
            if (val->kind==ValueKind::RESULT_ERR) {
                // propagate
                throw PropagateErrSignal(val);
            } else if (val->kind==ValueKind::RESULT_OK) {
                return val->inner ? val->inner : Value::makeNil();
            } else {
                // if not Result, return as is? But ? also works for Option?
                if (val->kind==ValueKind::OPTION_NONE) {
                    // For Option ?, propagate None? In Rust, ? on None returns None from function. For simplicity, propagate as None return.
                    throw PropagateErrSignal(Value::makeNone());
                }
                return val;
            }
        }
        case ExprKind::OPTIONAL_CHAIN: {
            auto obj = evalExpression(expr->object, env);
            if (obj->kind==ValueKind::OPTION_NONE || obj->kind==ValueKind::NIL) return Value::makeNone();
            if (obj->kind==ValueKind::OPTION_SOME) obj = obj->inner;
            ValuePtr res = lookupFieldOrMethod(obj, expr->fieldName);
            if (!res) {
                // method?
                // try impl
                if (obj->kind==ValueKind::STRUCT_INSTANCE) {
                    auto itType = implMethods.find(obj->structName);
                    if (itType!=implMethods.end()) {
                        auto itMeth = itType->second.find(expr->fieldName);
                        if (itMeth!=itType->second.end()) {
                            // for optional chaining of method call, the call handling will need special? For len? We'll handle len case separate
                            // For field chain alone, return nil? Let's return none? Actually a?.b if a is Some should return field
                            return Value::makeNone();
                        }
                    }
                }
                return Value::makeNone();
            }
            return res;
        }
        case ExprKind::COALESCE: {
            auto left = evalExpression(expr->left, env);
            if (left->kind==ValueKind::OPTION_NONE || left->kind==ValueKind::NIL || left->kind==ValueKind::RESULT_ERR) {
                return evalExpression(expr->right, env);
            }
            if (left->kind==ValueKind::RESULT_OK) return left->inner ? left->inner : left;
            if (left->kind==ValueKind::OPTION_SOME) return left->inner;
            return left;
        }
        default:
            runtimeError(expr->line, "Unimplemented expression kind");
            return Value::makeNil();
    }
}

ValuePtr Interpreter::evalIdentifier(const std::string& name, EnvPtr env, int line) {
    auto info = env->getVarInfo(name);
    if (!info) {
        // check if it's a struct name used as value? Or builtin?
        runtimeError(line, "Undefined variable '" + name + "'");
        return Value::makeNil();
    }
    if (info->isMoved) {
        runtimeError(line, "Use of moved value '" + name + "'");
        return Value::makeNil();
    }
    return info->value;
}

void Interpreter::bindPattern(PatternPtr pat, ValuePtr value, EnvPtr env) {
    if (!pat) return;
    switch(pat->kind) {
        case PatternKind::WILDCARD: return;
        case PatternKind::IDENT: {
            env->define(pat->ident, value, pat->isMut);
            return;
        }
        case PatternKind::TUPLE: {
            if (value->kind!=ValueKind::TUPLE && value->kind!=ValueKind::ARRAY) runtimeError(0, "Pattern mismatch: expected tuple/array for tuple pattern");
            for (size_t i=0;i<pat->sub.size() && i<value->elements.size();++i) {
                auto subPat = pat->sub[i];
                if (subPat->kind==PatternKind::REST) {
                    // rest binds remaining elements as array
                    std::vector<ValuePtr> rest;
                    for (size_t j=i;j<value->elements.size();++j) rest.push_back(value->elements[j]);
                    auto restVal = Value::makeArray(rest);
                    if (!subPat->ident.empty()) env->define(subPat->ident, restVal, subPat->isMut);
                    break;
                } else {
                    bindPattern(subPat, value->elements[i], env);
                }
            }
            return;
        }
        case PatternKind::ARRAY: {
            if (value->kind!=ValueKind::ARRAY && value->kind!=ValueKind::TUPLE) runtimeError(0, "Array pattern expects array");
            size_t valIdx=0;
            for (size_t patIdx=0; patIdx<pat->sub.size(); ++patIdx) {
                auto sub = pat->sub[patIdx];
                if (sub->kind==PatternKind::REST) {
                    // collect rest until remaining patterns fit
                    size_t remainingPats = pat->sub.size() - patIdx -1;
                    size_t remainingVals = value->elements.size() - valIdx - remainingPats;
                    std::vector<ValuePtr> rest;
                    for (size_t k=0;k<remainingVals;++k) rest.push_back(value->elements[valIdx++]);
                    if (!sub->ident.empty()) env->define(sub->ident, Value::makeArray(rest), sub->isMut);
                } else {
                    if (valIdx>=value->elements.size()) runtimeError(0, "Array pattern out of bounds");
                    bindPattern(sub, value->elements[valIdx++], env);
                }
            }
            return;
        }
        default:
            // For other patterns (literal, enum variant, struct) we only bind ident inside? We'll handle enum variant pattern in matching code, not here.
            if (pat->kind==PatternKind::IDENT) env->define(pat->ident, value, pat->isMut);
            return;
    }
}

bool Interpreter::matchPattern(PatternPtr pat, ValuePtr value, EnvPtr env, bool isMutBinding) {
    if (!pat) return false;
    switch(pat->kind) {
        case PatternKind::WILDCARD: return true;
        case PatternKind::IDENT: {
            env->define(pat->ident, value, isMutBinding || pat->isMut);
            return true;
        }
        case PatternKind::LITERAL: {
            Token t = pat->literalToken;
            if (t.type==TokenType::NUMBER) {
                // compare numbers
                if (value->kind==ValueKind::INT) {
                    try { int64_t i = std::stoll(t.lexeme); return value->intVal==i; } catch(...){return false;}
                } else if (value->kind==ValueKind::FLOAT) {
                    try { double d = std::stod(t.lexeme); return std::abs(value->floatVal-d)<1e-9; } catch(...){return false;}
                }
            } else if (t.type==TokenType::STRING) {
                return value->kind==ValueKind::STRING && value->strVal==t.lexeme;
            } else if (t.type==TokenType::TRUE) return value->kind==ValueKind::BOOL && value->boolVal==true;
            else if (t.type==TokenType::FALSE) return value->kind==ValueKind::BOOL && value->boolVal==false;
            else if (t.type==TokenType::NONE) return value->kind==ValueKind::OPTION_NONE || value->kind==ValueKind::NIL;
            return false;
        }
        case PatternKind::TUPLE: {
            if (value->kind!=ValueKind::TUPLE && value->kind!=ValueKind::ARRAY) return false;
            if (pat->sub.size()!=value->elements.size()) return false; // without rest handling for match?
            EnvPtr tmpEnv = std::make_shared<Env>(env);
            for (size_t i=0;i<pat->sub.size();++i) {
                if (!matchPattern(pat->sub[i], value->elements[i], tmpEnv)) return false;
            }
            // merge tmpEnv into env
            for (auto& kv : tmpEnv->vars) env->define(kv.first, kv.second.value, kv.second.isMut);
            return true;
        }
        case PatternKind::ARRAY: {
            if (value->kind!=ValueKind::ARRAY && value->kind!=ValueKind::TUPLE) return false;
            // with rest pattern handling
            size_t valIdx=0;
            EnvPtr tmp = std::make_shared<Env>(env);
            for (size_t patIdx=0; patIdx<pat->sub.size(); ++patIdx) {
                auto sub = pat->sub[patIdx];
                if (sub->kind==PatternKind::REST) {
                    size_t remaining = pat->sub.size()-patIdx-1;
                    size_t remainingVals = value->elements.size()-valIdx-remaining;
                    std::vector<ValuePtr> rest;
                    for (size_t k=0;k<remainingVals;++k) rest.push_back(value->elements[valIdx++]);
                    if (!sub->ident.empty()) tmp->define(sub->ident, Value::makeArray(rest), sub->isMut);
                } else {
                    if (valIdx>=value->elements.size()) return false;
                    if (!matchPattern(sub, value->elements[valIdx++], tmp)) return false;
                }
            }
            if (valIdx!=value->elements.size()) {
                // if no rest, must match all
                bool hasRest=false; for(auto& s:pat->sub) if(s->kind==PatternKind::REST) hasRest=true;
                if (!hasRest) return false;
            }
            for (auto& kv: tmp->vars) env->define(kv.first, kv.second.value, kv.second.isMut);
            return true;
        }
        case PatternKind::OR: {
            for (auto& sub: pat->sub) {
                EnvPtr tmp = std::make_shared<Env>(env);
                if (matchPattern(sub, value, tmp)) {
                    for (auto& kv: tmp->vars) env->define(kv.first, kv.second.value, kv.second.isMut);
                    return true;
                }
            }
            return false;
        }
        case PatternKind::RANGE: {
            // range pattern: check if value in range
            // Only for int/float patterns where start/end are literal patterns
            // We'll attempt to evaluate start/end as literals
            // Simplifying: if start and end are literal patterns with int values
            auto evalPatLiteral = [](PatternPtr p)->std::optional<int64_t>{
                if (p->kind==PatternKind::LITERAL && p->literalToken.type==TokenType::NUMBER) {
                    try { return std::stoll(p->literalToken.lexeme); } catch(...){return std::nullopt;}
                }
                if (p->kind==PatternKind::IDENT) {
                    try { return std::stoll(p->ident); } catch(...){return std::nullopt;}
                }
                return std::nullopt;
            };
            if (value->kind==ValueKind::INT) {
                auto s = evalPatLiteral(pat->rangeStart);
                auto e = evalPatLiteral(pat->rangeEnd);
                if (s && e) {
                    if (pat->rangeInclusive) return value->intVal>=*s && value->intVal<=*e;
                    else return value->intVal>=*s && value->intVal<*e;
                }
            }
            return false;
        }
        case PatternKind::ENUM_VARIANT: {
            if (value->kind!=ValueKind::ENUM_VARIANT) return false;
            if (value->variantName!=pat->ident && value->enumName+"::"+value->variantName!=pat->ident) return false;
            if (pat->sub.size()!=value->variantFields.size()) return false;
            EnvPtr tmp = std::make_shared<Env>(env);
            for (size_t i=0;i<pat->sub.size();++i) if (!matchPattern(pat->sub[i], value->variantFields[i], tmp)) return false;
            for (auto& kv: tmp->vars) env->define(kv.first, kv.second.value, kv.second.isMut);
            return true;
        }
        default: return false;
    }
}

ValuePtr Interpreter::callUserFunction(std::shared_ptr<UserFunction> uf, std::vector<ValuePtr> args, EnvPtr callerEnv) {
    EnvPtr funcEnv = std::make_shared<Env>(uf->closureEnv);
    // bind params
    for (size_t i=0;i<uf->params.size();++i) {
        std::string pname = uf->params[i].name;
        ValuePtr aval = (i < args.size()) ? args[i] : Value::makeNil();
        funcEnv->define(pname, aval, uf->params[i].isMut);
    }
    try {
        ValuePtr result;
        if (uf->body && uf->body->kind == ExprKind::BLOCK) result = evalBlockExpr(uf->body, funcEnv);
        else result = evalExpression(uf->body, funcEnv);
        return result ? result : Value::makeNil();
    } catch (ReturnSignal& ret) {
        return ret.value ? ret.value : Value::makeNil();
    } catch (PropagateErrSignal& pe) {
        // Propagate further? If function returns Result, we need to return Err? Actually ? handling already throws PropagateErrSignal which should propagate up until function that returns Result? For simplicity, if we catch PropagateErrSignal inside callUserFunction, we should convert to return value if function's return type is Result? But we don't know. For now, if ? propagates, we return Err value from function.
        // In Rust-like semantics, ? returns early from function with Err.
        // So here returning Err as function result.
        return pe.err;
    }
}

ValuePtr Interpreter::evalBinary(ExprPtr expr, EnvPtr env) {
    Token op = expr->op;
    // short-circuit for and/or
    if (op.type==TokenType::AND) {
        auto left = evalExpression(expr->left, env);
        if (!left->isTruthy()) return left;
        return evalExpression(expr->right, env);
    }
    if (op.type==TokenType::OR) {
        auto left = evalExpression(expr->left, env);
        if (left->isTruthy()) return left;
        return evalExpression(expr->right, env);
    }
    auto left = evalExpression(expr->left, env);
    auto right = evalExpression(expr->right, env);
    std::string lex = op.lexeme;
    if (lex=="+" || op.type==TokenType::PLUS) {
        if (left->kind==ValueKind::INT && right->kind==ValueKind::INT) return Value::makeInt(left->intVal + right->intVal);
        if (left->kind==ValueKind::FLOAT && right->kind==ValueKind::FLOAT) return Value::makeFloat(left->floatVal + right->floatVal);
        if (left->kind==ValueKind::FLOAT && right->kind==ValueKind::INT) return Value::makeFloat(left->floatVal + right->intVal);
        if (left->kind==ValueKind::INT && right->kind==ValueKind::FLOAT) return Value::makeFloat(left->intVal + right->floatVal);
        if (left->kind==ValueKind::STRING && right->kind==ValueKind::STRING) return Value::makeString(left->strVal + right->strVal);
        if (left->kind==ValueKind::STRING) return Value::makeString(left->strVal + right->toString());
        if (left->kind==ValueKind::ARRAY && right->kind==ValueKind::ARRAY) {
            std::vector<ValuePtr> combined = left->elements;
            combined.insert(combined.end(), right->elements.begin(), right->elements.end());
            return Value::makeArray(combined);
        }
    }
    if (lex=="-" || op.type==TokenType::MINUS) {
        if (left->kind==ValueKind::INT && right->kind==ValueKind::INT) return Value::makeInt(left->intVal - right->intVal);
        if (left->kind==ValueKind::FLOAT || right->kind==ValueKind::FLOAT) {
            double a = left->kind==ValueKind::FLOAT? left->floatVal : (double)left->intVal;
            double b = right->kind==ValueKind::FLOAT? right->floatVal : (double)right->intVal;
            return Value::makeFloat(a-b);
        }
    }
    if (lex=="*" || op.type==TokenType::STAR) {
        if (left->kind==ValueKind::INT && right->kind==ValueKind::INT) return Value::makeInt(left->intVal * right->intVal);
        if (left->kind==ValueKind::FLOAT || right->kind==ValueKind::FLOAT) {
            double a = left->kind==ValueKind::FLOAT? left->floatVal : (double)left->intVal;
            double b = right->kind==ValueKind::FLOAT? right->floatVal : (double)right->intVal;
            return Value::makeFloat(a*b);
        }
    }
    if (lex=="/" || op.type==TokenType::SLASH) {
        double a = left->kind==ValueKind::FLOAT? left->floatVal : (double)left->intVal;
        double b = right->kind==ValueKind::FLOAT? right->floatVal : (double)right->intVal;
        if (b==0) runtimeError(expr->line, "Division by zero");
        return Value::makeFloat(a/b);
    }
    if (lex=="%" || op.type==TokenType::PERCENT) {
        if (left->kind==ValueKind::INT && right->kind==ValueKind::INT) return Value::makeInt(left->intVal % right->intVal);
    }
    if (lex=="**" || op.type==TokenType::STAR_STAR) {
        double a = left->kind==ValueKind::FLOAT? left->floatVal : (double)left->intVal;
        double b = right->kind==ValueKind::FLOAT? right->floatVal : (double)right->intVal;
        return Value::makeFloat(std::pow(a,b));
    }
    if (op.type==TokenType::EQUAL_EQUAL) {
        return Value::makeBool(valuesEqual(left,right));
    }
    if (op.type==TokenType::BANG_EQUAL) {
        return Value::makeBool(!valuesEqual(left,right));
    }
    if (op.type==TokenType::LESS) {
        if (left->kind==ValueKind::INT && right->kind==ValueKind::INT) return Value::makeBool(left->intVal < right->intVal);
        double a = left->kind==ValueKind::FLOAT? left->floatVal : (double)left->intVal;
        double b = right->kind==ValueKind::FLOAT? right->floatVal : (double)right->intVal;
        return Value::makeBool(a<b);
    }
    if (op.type==TokenType::GREATER) {
        if (left->kind==ValueKind::INT && right->kind==ValueKind::INT) return Value::makeBool(left->intVal > right->intVal);
        double a = left->kind==ValueKind::FLOAT? left->floatVal : (double)left->intVal;
        double b = right->kind==ValueKind::FLOAT? right->floatVal : (double)right->intVal;
        return Value::makeBool(a>b);
    }
    if (op.type==TokenType::LESS_EQUAL) {
        if (left->kind==ValueKind::INT && right->kind==ValueKind::INT) return Value::makeBool(left->intVal <= right->intVal);
        double a = left->kind==ValueKind::FLOAT? left->floatVal : (double)left->intVal;
        double b = right->kind==ValueKind::FLOAT? right->floatVal : (double)right->intVal;
        return Value::makeBool(a<=b);
    }
    if (op.type==TokenType::GREATER_EQUAL) {
        if (left->kind==ValueKind::INT && right->kind==ValueKind::INT) return Value::makeBool(left->intVal >= right->intVal);
        double a = left->kind==ValueKind::FLOAT? left->floatVal : (double)left->intVal;
        double b = right->kind==ValueKind::FLOAT? right->floatVal : (double)right->intVal;
        return Value::makeBool(a>=b);
    }
    runtimeError(expr->line, "Unsupported binary operator " + lex);
    return Value::makeNil();
}

bool Interpreter::valuesEqual(ValuePtr a, ValuePtr b) {
    if (!a || !b) return a==b;
    if (a->kind!=b->kind) {
        // allow int/float comparison
        if ((a->kind==ValueKind::INT || a->kind==ValueKind::FLOAT) && (b->kind==ValueKind::INT || b->kind==ValueKind::FLOAT)) {
            double da = a->kind==ValueKind::FLOAT? a->floatVal : a->intVal;
            double db = b->kind==ValueKind::FLOAT? b->floatVal : b->intVal;
            return std::abs(da-db)<1e-9;
        }
        return false;
    }
    switch(a->kind) {
        case ValueKind::NIL: return true;
        case ValueKind::BOOL: return a->boolVal==b->boolVal;
        case ValueKind::INT: return a->intVal==b->intVal;
        case ValueKind::FLOAT: return std::abs(a->floatVal-b->floatVal)<1e-9;
        case ValueKind::STRING: return a->strVal==b->strVal;
        case ValueKind::ARRAY: case ValueKind::TUPLE: {
            if (a->elements.size()!=b->elements.size()) return false;
            for (size_t i=0;i<a->elements.size();++i) if (!valuesEqual(a->elements[i], b->elements[i])) return false;
            return true;
        }
        default: return a->toString()==b->toString();
    }
}

ValuePtr Interpreter::evalUnary(ExprPtr expr, EnvPtr env) {
    auto val = evalExpression(expr->unaryExpr, env);
    std::string lex = expr->op.lexeme;
    TokenType t = expr->op.type;
    if (t==TokenType::MINUS) {
        if (val->kind==ValueKind::INT) return Value::makeInt(-val->intVal);
        if (val->kind==ValueKind::FLOAT) return Value::makeFloat(-val->floatVal);
    }
    if (t==TokenType::BANG || t==TokenType::NOT) {
        return Value::makeBool(!val->isTruthy());
    }
    return val;
}

ValuePtr Interpreter::evalIf(ExprPtr expr, EnvPtr env) {
    auto cond = evalExpression(expr->cond, env);
    if (cond->isTruthy()) {
        return evalExpression(expr->thenBranch, env);
    } else {
        if (expr->elseBranch) return evalExpression(expr->elseBranch, env);
        return Value::makeNil();
    }
}

ValuePtr Interpreter::evalFor(ExprPtr expr, EnvPtr env) {
    // eval iterable
    auto iterVal = evalExpression(expr->forIter, env);
    std::vector<ValuePtr> iterElems;
    if (iterVal->kind==ValueKind::RANGE) {
        int64_t start=0,end=0;
        if (iterVal->rangeStart->kind==ValueKind::INT) start=iterVal->rangeStart->intVal;
        if (iterVal->rangeEnd->kind==ValueKind::INT) end=iterVal->rangeEnd->intVal;
        else if (iterVal->rangeEnd->kind==ValueKind::FLOAT) end=(int64_t)iterVal->rangeEnd->floatVal;
        if (iterVal->rangeInclusive) end+=1;
        for (int64_t i=start;i<end;++i) iterElems.push_back(Value::makeInt(i));
    } else if (iterVal->kind==ValueKind::ARRAY || iterVal->kind==ValueKind::TUPLE) {
        iterElems = iterVal->elements;
    } else if (iterVal->kind==ValueKind::DICT) {
        // iterate keys? For simplicity iterate values? We'll iterate entries as tuple?
        for (auto& kv : iterVal->dictStringMap) iterElems.push_back(Value::makeString(kv.first));
    } else {
        runtimeError(expr->line, "For loop requires iterable");
    }

    ValuePtr last = Value::makeNil();
    for (auto& elemVal : iterElems) {
        EnvPtr loopEnv = std::make_shared<Env>(env);
        // bind pattern
        if (expr->forPattern) {
            bindPattern(expr->forPattern, elemVal, loopEnv);
        }
        try {
            last = evalExpression(expr->forBody, loopEnv);
        } catch (BreakSignal& b) {
            if (!b.label || (expr->label && *b.label==*expr->label) || !expr->label) {
                break;
            } else throw;
        } catch (ContinueSignal& c) {
            if (!c.label || (expr->label && *c.label==*expr->label) || !expr->label) {
                continue;
            } else throw;
        }
    }
    return Value::makeNil();
}

ValuePtr Interpreter::evalWhile(ExprPtr expr, EnvPtr env) {
    while (true) {
        auto cond = evalExpression(expr->cond, env);
        if (!cond->isTruthy()) break;
        try {
            evalExpression(expr->forBody, env);
        } catch (BreakSignal& b) {
            if (!b.label || (expr->label && *b.label==*expr->label) || !expr->label) break;
            else throw;
        } catch (ContinueSignal& c) {
            if (!c.label || (expr->label && *c.label==*expr->label) || !expr->label) continue;
            else throw;
        }
    }
    return Value::makeNil();
}

ValuePtr Interpreter::evalLoop(ExprPtr expr, EnvPtr env) {
    while (true) {
        try {
            evalExpression(expr->forBody, env);
        } catch (BreakSignal& b) {
            if (!b.label || (expr->label && *b.label==*expr->label) || !expr->label) break;
            else throw;
        } catch (ContinueSignal& c) {
            if (!c.label || (expr->label && *c.label==*expr->label) || !expr->label) continue;
            else throw;
        }
    }
    return Value::makeNil();
}

ValuePtr Interpreter::evalMatch(ExprPtr expr, EnvPtr env) {
    auto target = evalExpression(expr->matchTarget, env);
    for (auto& arm : expr->matchArms) {
        EnvPtr armEnv = std::make_shared<Env>(env);
        if (matchPattern(arm.pattern, target, armEnv)) {
            if (arm.guard) {
                auto guardVal = evalExpression(arm.guard, armEnv);
                if (!guardVal->isTruthy()) continue;
            }
            return evalExpression(arm.body, armEnv);
        }
    }
    runtimeError(expr->line, "Match failed: no arm matched " + target->toString());
    return Value::makeNil();
}

ValuePtr Interpreter::evalClosure(ExprPtr expr, EnvPtr env) {
    auto uf = std::make_shared<UserFunction>();
    uf->name = "<closure>";
    for (auto& p : expr->params) {
        FnParam fp; fp.name = p; uf->params.push_back(fp);
    }
    uf->body = expr->closureBody;
    uf->closureEnv = env;
    auto v = std::make_shared<Value>(ValueKind::FUNCTION);
    v->userFn = uf;
    return v;
}

ValuePtr Interpreter::evalRange(ExprPtr expr, EnvPtr env) {
    auto s = evalExpression(expr->rangeStart, env);
    auto e = evalExpression(expr->rangeEnd, env);
    return Value::makeRange(s, e, expr->rangeInclusive);
}

ValuePtr Interpreter::evalTemplate(ExprPtr expr, EnvPtr env) {
    std::string result;
    for (auto& part : expr->templateParts) {
        if (!part.isExpr) result += part.text;
        else {
            auto v = evalExpression(part.expr, env);
            result += v->toString();
        }
    }
    return Value::makeString(result);
}

ValuePtr Interpreter::evalComprehension(ExprPtr expr, EnvPtr env) {
    // Handles both array and dict comprehension
    // Recursive evaluation of clauses
    std::function<std::vector<std::unordered_map<std::string, ValuePtr>>(size_t, EnvPtr)> evalClauses;
    // We will implement iterative expansion
    // For simplicity handle single clause first, then nested loops via recursion

    std::function<void(size_t, EnvPtr, std::vector<ValuePtr>&, std::vector<std::pair<ValuePtr,ValuePtr>>&)> recurseArray;
    std::vector<ValuePtr> arrayResults;
    std::vector<std::pair<ValuePtr,ValuePtr>> dictResultsCollect;

    // Convert clauses to stack
    std::function<void(size_t, EnvPtr)> dfs = [&](size_t idx, EnvPtr curEnv){
        if (idx >= expr->compClauses.size()) {
            // evaluate inner expr
            if (expr->isDictComp) {
                auto k = evalExpression(expr->compKey, curEnv);
                auto v = evalExpression(expr->compExpr, curEnv);
                dictResultsCollect.push_back({k,v});
            } else {
                auto v = evalExpression(expr->compExpr, curEnv);
                arrayResults.push_back(v);
            }
            return;
        }
        auto& clause = expr->compClauses[idx];
        auto iterVal = evalExpression(clause.iterable, curEnv);
        std::vector<ValuePtr> elems;
        if (iterVal->kind==ValueKind::RANGE) {
            int64_t s=0,e=0;
            if (iterVal->rangeStart->kind==ValueKind::INT) s=iterVal->rangeStart->intVal;
            if (iterVal->rangeEnd->kind==ValueKind::INT) e=iterVal->rangeEnd->intVal;
            if (iterVal->rangeInclusive) e++;
            for (int64_t i=s;i<e;++i) elems.push_back(Value::makeInt(i));
        } else if (iterVal->kind==ValueKind::ARRAY || iterVal->kind==ValueKind::TUPLE) elems = iterVal->elements;
        else {
            // iterate dict?
            runtimeError(expr->line, "Comprehension iterable must be array or range");
            return;
        }
        for (auto& elem : elems) {
            EnvPtr nextEnv = std::make_shared<Env>(curEnv);
            if (clause.pattern) bindPattern(clause.pattern, elem, nextEnv);
            else if (!clause.var.empty()) nextEnv->define(clause.var, elem, false);
            if (clause.condition) {
                auto condVal = evalExpression(clause.condition, nextEnv);
                if (!condVal->isTruthy()) continue;
            }
            dfs(idx+1, nextEnv);
        }
    };

    dfs(0, env);

    if (expr->isDictComp) {
        auto dict = std::make_shared<Value>(ValueKind::DICT);
        for (auto& kv : dictResultsCollect) {
            if (kv.first->kind==ValueKind::STRING) dict->dictStringMap[kv.first->strVal]=kv.second;
            else dict->dictEntries.push_back(kv);
        }
        return dict;
    } else {
        return Value::makeArray(arrayResults);
    }
}

ValuePtr Interpreter::evalStructLiteral(ExprPtr expr, EnvPtr env) {
    auto structVal = std::make_shared<Value>(ValueKind::STRUCT_INSTANCE);
    structVal->structName = expr->structName;
    // check if structDef exists
    for (auto& fieldPair : expr->structFields) {
        auto fval = evalExpression(fieldPair.second, env);
        structVal->fields[fieldPair.first] = fval;
    }
    // If anonymous struct (no name) treat as dict? But we keep as struct instance with empty name
    if (expr->isAnonymousStruct) {
        structVal->structName = "";
    }
    return structVal;
}

ValuePtr Interpreter::lookupFieldOrMethod(ValuePtr object, const std::string& fieldName) {
    if (!object) return nullptr;
    switch(object->kind) {
        case ValueKind::STRUCT_INSTANCE: {
            auto it = object->fields.find(fieldName);
            if (it!=object->fields.end()) return it->second;
            // also check impl methods elsewhere
            return nullptr;
        }
        case ValueKind::DICT: {
            auto it = object->dictStringMap.find(fieldName);
            if (it!=object->dictStringMap.end()) return it->second;
            return nullptr;
        }
        case ValueKind::BOX: {
            // deref?
            if (fieldName=="value" || fieldName=="inner") return object->inner;
            // delegate to inner?
            return lookupFieldOrMethod(object->inner, fieldName);
        }
        case ValueKind::REF:
        case ValueKind::MUT_REF: {
            return lookupFieldOrMethod(object->inner, fieldName);
        }
        case ValueKind::STRING: {
            if (fieldName=="len") return Value::makeInt(object->strVal.size());
            return nullptr;
        }
        default: return nullptr;
    }
}

} // namespace cotton
