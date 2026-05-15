#include "ir_type.h"
#include <cassert>

IRType& IRType::operator=(const IRType& other) {
    if (this == &other) return *this;
    if (other.is_prim()) {
        kind = other.as_prim();
    } else if (other.is_ptr()) {
        kind = std::unique_ptr<Pointer>(new Pointer{std::make_unique<IRType>(*other.as_ptr().pointee)});
    } else if (other.is_arr()) {
        kind = std::unique_ptr<Array>(new Array{std::make_unique<IRType>(*other.as_arr().element), other.as_arr().size});
    } else if (other.is_fn()) {
        auto fp = std::make_unique<Function>();
        fp->return_type = std::make_unique<IRType>(*other.as_fn().return_type);
        for (auto& p : other.as_fn().params) fp->params.push_back(p);
        fp->variadic = other.as_fn().variadic;
        kind = std::move(fp);
    } else if (other.is_struct()) {
        kind = other.as_struct();
    } else if (other.is_param()) {
        kind = other.as_param();
    }
    return *this;
}

bool IRType::has_params() const {
    if (is_param()) return true;
    if (is_ptr())  return as_ptr().pointee->has_params();
    if (is_arr())  return as_arr().element->has_params();
    if (is_fn()) {
        if (as_fn().return_type->has_params()) return true;
        for (auto& p : as_fn().params)
            if (p.has_params()) return true;
        return false;
    }
    if (is_struct()) {
        for (auto& a : as_struct().args)
            if (a.has_params()) return true;
        return false;
    }
    return false;
}

IRType IRType::copy() const {
    if (is_prim()) return as_prim();
    if (is_ptr())  return IRTypeFactory::ptr(as_ptr().pointee->copy());
    if (is_arr())  return IRTypeFactory::arr(as_arr().element->copy(), as_arr().size);
    if (is_fn()) {
        std::vector<IRType> params;
        for (auto& p : as_fn().params) params.push_back(p.copy());
        return IRTypeFactory::fn(as_fn().return_type->copy(), params, as_fn().variadic);
    }
    if (is_struct()) return IRTypeFactory::strukt(as_struct().name, [&]() {
        std::vector<IRType> args;
        for (auto& a : as_struct().args) args.push_back(a.copy());
        return args;
    }());
    if (is_param()) return IRTypeFactory::param(as_param().index, as_param().name);
    return IRType();
}

IRType IRType::substitute(const Param& from, const IRType& to) const {
    if (is_param()) {
        auto& p = as_param();
        if (p.index == from.index && p.name == from.name)
            return to.copy();
        return *this;
    }
    if (is_ptr())  return IRTypeFactory::ptr(as_ptr().pointee->substitute(from, to));
    if (is_arr())  return IRTypeFactory::arr(as_arr().element->substitute(from, to), as_arr().size);
    if (is_fn()) {
        std::vector<IRType> new_params;
        for (auto& p : as_fn().params)
            new_params.push_back(p.substitute(from, to));
        return IRTypeFactory::fn(as_fn().return_type->substitute(from, to), new_params, as_fn().variadic);
    }
    if (is_struct()) {
        std::vector<IRType> new_args;
        for (auto& a : as_struct().args)
            new_args.push_back(a.substitute(from, to));
        return IRTypeFactory::strukt(as_struct().name, new_args);
    }
    return *this;
}

bool IRType::operator==(const IRType& o) const {
    if (kind.index() != o.kind.index()) return false;
    if (is_prim()) return as_prim() == o.as_prim();
    if (is_ptr())  return *as_ptr().pointee == *o.as_ptr().pointee;
    if (is_arr()) {
        auto& a = as_arr();
        auto& b = o.as_arr();
        return *a.element == *b.element && a.size == b.size;
    }
    if (is_fn()) {
        auto& a = as_fn();
        auto& b = o.as_fn();
        if (*a.return_type != *b.return_type) return false;
        if (a.params.size() != b.params.size()) return false;
        for (size_t i = 0; i < a.params.size(); i++)
            if (a.params[i] != b.params[i]) return false;
        return a.variadic == b.variadic;
    }
    if (is_struct()) {
        auto& a = as_struct();
        auto& b = o.as_struct();
        if (a.name != b.name) return false;
        if (a.args.size() != b.args.size()) return false;
        for (size_t i = 0; i < a.args.size(); i++)
            if (a.args[i] != b.args[i]) return false;
        return true;
    }
    if (is_param()) {
        auto& a = as_param();
        auto& b = o.as_param();
        return a.index == b.index && a.name == b.name;
    }
    return false;
}

// Factory implementations

IRType IRTypeFactory::ptr(IRType pointee) {
    IRType t;
    t.kind = std::unique_ptr<IRType::Pointer>(new IRType::Pointer{std::make_unique<IRType>(std::move(pointee))});
    return t;
}

IRType IRTypeFactory::arr(IRType elem, size_t sz) {
    IRType t;
    t.kind = std::unique_ptr<IRType::Array>(new IRType::Array{std::make_unique<IRType>(std::move(elem)), sz});
    return t;
}

IRType IRTypeFactory::fn(IRType ret, std::vector<IRType> params, bool var) {
    IRType t;
    auto fp = std::make_unique<IRType::Function>();
    fp->return_type = std::make_unique<IRType>(std::move(ret));
    fp->params = std::move(params);
    fp->variadic = var;
    t.kind = std::move(fp);
    return t;
}

IRType IRTypeFactory::strukt(std::string name, std::vector<IRType> args) {
    IRType t;
    t.kind = IRType::Struct{std::move(name), std::move(args)};
    return t;
}

IRType IRTypeFactory::param(size_t idx, std::string name) {
    IRType t;
    t.kind = IRType::Param{idx, std::move(name)};
    return t;
}
