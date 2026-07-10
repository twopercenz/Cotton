#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <variant>
#include <optional>
#include "ast.h"

namespace cotton {

struct Env;
struct Value;
using ValuePtr = std::shared_ptr<Value>;
using EnvPtr = std::shared_ptr<Env>;

enum class ValueKind {
    NIL,
    BOOL,
    INT,
    FLOAT,
    STRING,
    ARRAY,
    TUPLE,
    DICT,
    STRUCT_INSTANCE,
    ENUM_VARIANT,
    RANGE,
    RESULT_OK,
    RESULT_ERR,
    OPTION_SOME,
    OPTION_NONE,
    FUNCTION,
    BUILTIN,
    BOX,
    REF,
    MUT_REF
};

struct StructDef;
struct EnumDef;

struct UserFunction {
    std::string name;
    std::vector<FnParam> params;
    std::vector<std::string> genericParams;
    ExprPtr body; // BLOCK
    EnvPtr closureEnv;
    bool isMethod = false;
};

struct BuiltinFunction {
    std::string name;
    std::function<ValuePtr(std::vector<ValuePtr>, EnvPtr)> fn;
};

struct Value {
    ValueKind kind = ValueKind::NIL;
    // primitives
    bool boolVal = false;
    int64_t intVal = 0;
    double floatVal = 0.0;
    std::string strVal;

    // array/tuple: elements
    std::vector<ValuePtr> elements;

    // dict: string keys for simplicity + generic entries as vector
    std::unordered_map<std::string, ValuePtr> dictStringMap;
    std::vector<std::pair<ValuePtr, ValuePtr>> dictEntries;

    // struct
    std::string structName;
    std::unordered_map<std::string, ValuePtr> fields;

    // enum variant
    std::string enumName;
    std::string variantName;
    std::vector<ValuePtr> variantFields;

    // range
    ValuePtr rangeStart;
    ValuePtr rangeEnd;
    bool rangeInclusive = false;

    // result/option/box/ref
    ValuePtr inner; // for Ok, Err, Some, Box, Ref
    std::string errMsg; // for Err

    // function
    std::shared_ptr<UserFunction> userFn;
    std::shared_ptr<BuiltinFunction> builtinFn;

    Value() : kind(ValueKind::NIL) {}
    explicit Value(ValueKind k): kind(k) {}

    static ValuePtr makeNil() { return std::make_shared<Value>(ValueKind::NIL); }
    static ValuePtr makeBool(bool b) { auto v=std::make_shared<Value>(ValueKind::BOOL); v->boolVal=b; return v; }
    static ValuePtr makeInt(int64_t i) { auto v=std::make_shared<Value>(ValueKind::INT); v->intVal=i; return v; }
    static ValuePtr makeFloat(double d) { auto v=std::make_shared<Value>(ValueKind::FLOAT); v->floatVal=d; return v; }
    static ValuePtr makeString(const std::string& s) { auto v=std::make_shared<Value>(ValueKind::STRING); v->strVal=s; return v; }
    static ValuePtr makeArray(const std::vector<ValuePtr>& elems) { auto v=std::make_shared<Value>(ValueKind::ARRAY); v->elements=elems; return v; }
    static ValuePtr makeTuple(const std::vector<ValuePtr>& elems) { auto v=std::make_shared<Value>(ValueKind::TUPLE); v->elements=elems; return v; }
    static ValuePtr makeRange(ValuePtr start, ValuePtr end, bool incl) { auto v=std::make_shared<Value>(ValueKind::RANGE); v->rangeStart=start; v->rangeEnd=end; v->rangeInclusive=incl; return v; }
    static ValuePtr makeOk(ValuePtr inner) { auto v=std::make_shared<Value>(ValueKind::RESULT_OK); v->inner=inner; return v; }
    static ValuePtr makeErr(const std::string& msg) { auto v=std::make_shared<Value>(ValueKind::RESULT_ERR); v->errMsg=msg; if (!msg.empty()){auto inner = makeString(msg); v->inner=inner;} return v; }
    static ValuePtr makeErr(ValuePtr inner, const std::string& msg="") { auto v=std::make_shared<Value>(ValueKind::RESULT_ERR); v->inner=inner; v->errMsg=msg; return v; }
    static ValuePtr makeSome(ValuePtr inner) { auto v=std::make_shared<Value>(ValueKind::OPTION_SOME); v->inner=inner; return v; }
    static ValuePtr makeNone() { return std::make_shared<Value>(ValueKind::OPTION_NONE); }
    static ValuePtr makeBox(ValuePtr inner) { auto v=std::make_shared<Value>(ValueKind::BOX); v->inner=inner; return v; }
    static ValuePtr makeRef(ValuePtr inner, bool mut) { auto v=std::make_shared<Value>(mut?ValueKind::MUT_REF:ValueKind::REF); v->inner=inner; return v; }

    bool isTruthy() const {
        switch(kind) {
            case ValueKind::NIL: return false;
            case ValueKind::BOOL: return boolVal;
            case ValueKind::OPTION_NONE: return false;
            case ValueKind::RESULT_ERR: return false;
            case ValueKind::INT: return intVal!=0;
            case ValueKind::FLOAT: return floatVal!=0.0;
            case ValueKind::STRING: return !strVal.empty();
            case ValueKind::ARRAY: case ValueKind::TUPLE: return !elements.empty();
            default: return true;
        }
    }
    bool isCopyType() const {
        return kind==ValueKind::NIL || kind==ValueKind::BOOL || kind==ValueKind::INT || kind==ValueKind::FLOAT || kind==ValueKind::REF || kind==ValueKind::MUT_REF;
    }

    std::string toString() const;
};

struct VarInfo {
    ValuePtr value;
    bool isMut = false;
    bool isMoved = false;
    bool isConst = false;
};

struct Env : std::enable_shared_from_this<Env> {
    std::unordered_map<std::string, VarInfo> vars;
    EnvPtr parent;

    Env(EnvPtr p=nullptr): parent(p) {}

    void define(const std::string& name, ValuePtr val, bool isMut=false, bool isConst=false) {
        vars[name] = {val, isMut, false, isConst};
    }
    bool exists(const std::string& name) const {
        auto it = vars.find(name);
        if (it!=vars.end()) return true;
        if (parent) return parent->exists(name);
        return false;
    }
    VarInfo* getVarInfo(const std::string& name) {
        auto it = vars.find(name);
        if (it!=vars.end()) return &it->second;
        if (parent) return parent->getVarInfo(name);
        return nullptr;
    }
    ValuePtr get(const std::string& name) {
        auto info = getVarInfo(name);
        if (!info) return nullptr;
        if (info->isMoved) {
            // moved error will be handled by interpreter
            return nullptr;
        }
        return info->value;
    }
    bool set(const std::string& name, ValuePtr val) {
        auto info = getVarInfo(name);
        if (!info) return false;
        if (info->isConst) return false;
        if (!info->isMut) {
            // allow reassignment? In Cotton immutable by default, let without mut cannot be reassigned. So check isMut.
            // But for simplicity, we allow if not const and inside same scope? We'll enforce: if not mut, fail.
            // However loop variables etc need mut? We'll enforce only if explicitly mut required.
            // For now allow if isMut or if not defined as immutable let? We track isMut.
            if (!info->isMut) return false;
        }
        info->value = val;
        info->isMoved = false;
        return true;
    }
    void markMoved(const std::string& name) {
        auto info = getVarInfo(name);
        if (info) info->isMoved = true;
    }
};

} // namespace cotton
