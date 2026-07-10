#include "value.h"
#include <sstream>
#include <cmath>

namespace cotton {

std::string Value::toString() const {
    std::ostringstream oss;
    switch(kind) {
        case ValueKind::NIL: return "nil";
        case ValueKind::BOOL: return boolVal ? "true" : "false";
        case ValueKind::INT: return std::to_string(intVal);
        case ValueKind::FLOAT: {
            oss << floatVal;
            return oss.str();
        }
        case ValueKind::STRING: return strVal;
        case ValueKind::ARRAY: {
            oss << "[";
            for (size_t i=0;i<elements.size();++i){
                if (i) oss << ", ";
                oss << elements[i]->toString();
            }
            oss << "]";
            return oss.str();
        }
        case ValueKind::TUPLE: {
            oss << "(";
            for (size_t i=0;i<elements.size();++i){
                if (i) oss << ", ";
                oss << elements[i]->toString();
            }
            oss << ")";
            return oss.str();
        }
        case ValueKind::DICT: {
            oss << "{";
            bool first=true;
            for (auto &kv: dictStringMap) {
                if (!first) oss << ", ";
                first=false;
                oss << kv.first << ": " << kv.second->toString();
            }
            for (auto &p: dictEntries) {
                if (!first) oss << ", ";
                first=false;
                oss << p.first->toString() << ": " << p.second->toString();
            }
            oss << "}";
            return oss.str();
        }
        case ValueKind::STRUCT_INSTANCE: {
            oss << structName << "{";
            bool first=true;
            for (auto &kv: fields) {
                if (!first) oss << ", ";
                first=false;
                oss << kv.first << ": " << kv.second->toString();
            }
            oss << "}";
            return oss.str();
        }
        case ValueKind::ENUM_VARIANT: {
            oss << enumName << "::" << variantName;
            if (!variantFields.empty()) {
                oss << "(";
                for (size_t i=0;i<variantFields.size();++i){ if(i) oss<<", "; oss<<variantFields[i]->toString();}
                oss << ")";
            }
            return oss.str();
        }
        case ValueKind::RANGE: {
            std::string op = rangeInclusive ? "..=" : "..";
            return rangeStart->toString() + op + rangeEnd->toString();
        }
        case ValueKind::RESULT_OK: return "Ok(" + (inner?inner->toString():"nil") + ")";
        case ValueKind::RESULT_ERR: {
            if (inner) return "Err(" + inner->toString() + ")";
            return "Err(" + errMsg + ")";
        }
        case ValueKind::OPTION_SOME: return "Some(" + (inner?inner->toString():"nil") + ")";
        case ValueKind::OPTION_NONE: return "None";
        case ValueKind::FUNCTION: return "<fn " + (userFn?userFn->name:"closure") + ">";
        case ValueKind::BUILTIN: return "<builtin " + (builtinFn?builtinFn->name:"fn") + ">";
        case ValueKind::BOX: return "Box(" + (inner?inner->toString():"nil") + ")";
        case ValueKind::REF: return "&" + (inner?inner->toString():"nil");
        case ValueKind::MUT_REF: return "&mut " + (inner?inner->toString():"nil");
    }
    return "<unknown>";
}

} // namespace cotton
