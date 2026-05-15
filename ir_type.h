#ifndef C_COMPILER_IR_TYPE_H
#define C_COMPILER_IR_TYPE_H

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <cstddef>

enum class IRPrimitive {
    VOID, I1, I8, I16, I32, I64,
    U8, U16, U32, U64, F32, F64
};

struct IRType {
    struct Pointer  { std::unique_ptr<IRType> pointee; };
    struct Array    { std::unique_ptr<IRType> element; size_t size = 0; };
    struct Function { std::unique_ptr<IRType> return_type; std::vector<IRType> params; bool variadic = false; };
    struct Struct   { std::string name; std::vector<IRType> args; };
    struct Param    { size_t index; std::string name; };

    std::variant<IRPrimitive,
        std::unique_ptr<Pointer>, std::unique_ptr<Array>, std::unique_ptr<Function>,
        Struct, Param> kind;

    IRType() : kind(IRPrimitive::VOID) {}
    IRType(IRPrimitive p) : kind(p) {}
    IRType(Struct s) : kind(std::move(s)) {}
    IRType(Param p) : kind(std::move(p)) {}
    IRType(const IRType& other) { *this = other; }
    IRType& operator=(const IRType& other);
    IRType(IRType&&) = default;
    IRType& operator=(IRType&&) = default;

    bool is_void()    const { return is_prim() && as_prim() == IRPrimitive::VOID; }
    bool is_prim()    const { return std::holds_alternative<IRPrimitive>(kind); }
    bool is_ptr()     const { return std::holds_alternative<std::unique_ptr<Pointer>>(kind); }
    bool is_arr()     const { return std::holds_alternative<std::unique_ptr<Array>>(kind); }
    bool is_fn()      const { return std::holds_alternative<std::unique_ptr<Function>>(kind); }
    bool is_struct()  const { return std::holds_alternative<Struct>(kind); }
    bool is_param()   const { return std::holds_alternative<Param>(kind); }

    IRPrimitive as_prim()     const { return std::get<IRPrimitive>(kind); }
    const Pointer& as_ptr()   const { return *std::get<std::unique_ptr<Pointer>>(kind); }
    const Array& as_arr()     const { return *std::get<std::unique_ptr<Array>>(kind); }
    const Function& as_fn()   const { return *std::get<std::unique_ptr<Function>>(kind); }
    const Struct& as_struct() const { return std::get<Struct>(kind); }
    const Param& as_param()   const { return std::get<Param>(kind); }

    bool has_params() const;
    IRType copy() const;

    // Substitute a single type parameter with a concrete type
    // Returns a copy of this type with all occurrences of `from` replaced by `to`
    IRType substitute(const Param& from, const IRType& to) const;

    bool operator==(const IRType& o) const;
    bool operator!=(const IRType& o) const { return !(*this == o); }
};

struct IRTypeFactory {
    static IRType prim(IRPrimitive p) { return p; }
    static IRType ptr(IRType pointee);
    static IRType arr(IRType elem, size_t sz = 0);
    static IRType fn(IRType ret, std::vector<IRType> params, bool var = false);
    static IRType strukt(std::string name, std::vector<IRType> args = {});
    static IRType param(size_t idx, std::string name);
};

#endif
