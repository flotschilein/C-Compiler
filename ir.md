# LightIR — A Two-Layer IR for Templates

```
                     ┌──────────────────────┐
                     │    C/C++ Source      │
                     └──────────┬───────────┘
                                │
                     ┌──────────▼───────────┐
                     │   Lexer / Parser     │
                     └──────────┬───────────┘
                                │
                     ┌──────────▼───────────┐
                     │    Semantic Analysis │
                     └──────────┬───────────┘
                                │
                     ┌──────────▼───────────┐      ┌──────────────────┐
                     │   Generic IR (GIR)   │─────▶│  Template Body   │
                     │   (symbolic types)   │      │  (deferred)      │
                     └──────────┬───────────┘      └──────────────────┘
                                │
                     ┌──────────▼───────────┐
                     │   Instantiation      │
                     │   (type substitution)│
                     └──────────┬───────────┘
                                │
                     ┌──────────▼───────────┐
                     │   Concrete IR (CIR)  │
                     │   (SSA, typed)       │
                     └──────────┬───────────┘
                                │
                     ┌──────────▼───────────┐
                     │   Code Generation    │
                     └──────────────────────┘
```

---

## 1. Why a Two-Layer IR?

Templates require **deferred compilation** — you cannot lower a template function to concrete machine code until you know the type arguments. But you also cannot throw away the AST; you need a representation that stays **generic**.

| Layer | Role | When created |
|-------|------|--------------|
| **GIR** (Generic IR) | Lowered from AST, but retains symbolic type parameters | At definition site |
| **CIR** (Concrete IR) | Fully typed SSA with concrete types | At instantiation site |

A function template like:

```cpp
template<typename T>
T add(T a, T b) {
    return a + b;
}
```

…becomes GIR with `T` as a symbolic type param. Only when `add<int>(1, 2)` is seen do we produce CIR with `T → i32`.

---

## 2. GIR — Generic IR

### 2.1 Type System

Types carry **symbolic parameters** that are resolved at instantiation.

```
Type ::= PrimitiveType
       | PointerType(Type)
       | ArrayType(Type, Size)
       | FunctionType(ReturnType, [ParamType*], Variadic)
       | StructType(name, [Type*])    -- generic structs
       | TypeParam(index, name)       -- the key addition
```

`TypeParam` is what makes this different from a standard three-address-code IR:

```
%T0 = TypeParam(0, "T")        -- first template parameter
%T1 = TypeParam(1, "U")        -- second template parameter
```

All GIR value operands carry a `Type` (which may contain `TypeParam` nodes).

### 2.2 Instructions

GIR uses a **three-address code** with explicit type annotations.

```
┌─────────────────────────────────────────────────────────────────┐
│  Instruction            │  Operands            │  Semantics     │
├─────────────────────────────────────────────────────────────────┤
│  %r = add %a, %b        │  value, value        │  a + b         │
│  %r = sub %a, %b        │  value, value        │  a - b         │
│  %r = mul %a, %b        │  value, value        │  a * b         │
│  %r = div %a, %b        │  value, value        │  a / b         │
│  %r = rem %a, %b        │  value, value        │  a % b         │
│  %r = eq %a, %b         │  value, value        │  a == b        │
│  %r = ne %a, %b         │  value, value        │  a != b        │
│  %r = lt %a, %b         │  value, value        │  a < b         │
│  %r = le %a, %b         │  value, value        │  a <= b        │
│  %r = gt %a, %b         │  value, value        │  a > b         │
│  %r = ge %a, %b         │  value, value        │  a >= b        │
│  %r = and %a, %b        │  value, value        │  a && b        │
│  %r = or %a, %b         │  value, value        │  a \|\| b      │
│  %r = bit_and %a, %b    │  value, value        │  a & b         │
│  %r = bit_or %a, %b     │  value, value        │  a \| b        │
│  %r = bit_xor %a, %b    │  value, value        │  a ^ b         │
│  %r = shl %a, %b        │  value, value        │  a << b        │
│  %r = shr %a, %b        │  value, value        │  a >> b        │
│  %r = neg %a            │  value               │  -a            │
│  %r = not %a            │  value               │  !a            │
│  %r = bit_not %a        │  value               │  ~a            │
│  %r = load %p           │  pointer             │  *p            │
│  store %v, %p           │  value, pointer      │  *p = v        │
│  %r = ptr_add %p, %i    │  ptr, index          │  &p[i]         │
│  %r = gep %p, %i, %t    │  ptr, index, type    │  structured    │
│  %r = alloca %t         │  type                │  stack alloc   │
│  %r = cast %v, %t       │  value, target_type  │  (t)v          │
│  %r = call %f, [%args*] │  fn, args            │  f(args)       │
│  br %cond, %l1, %l2     │  cond, label, label  │  branch        │
│  br %l                  │  label               │  jump          │
│  ret %v                 │  value (optional)    │  return        │
│  phi [%v, %l]*          │  (value, label) pairs│  SSA phi       │
└─────────────────────────────────────────────────────────────────┘
```

Each instruction's result value carries a `Type` — the type of the computed result. When that type contains `TypeParam`, the instruction is **generic** and will be resolved later.

### 2.3 Example: Template Function in GIR

```cpp
template<typename T>
T max(T a, T b) {
    return a > b ? a : b;
}
```

```
function @max:
  params: (%a : TypeParam(0, "T"), %b : TypeParam(0, "T"))
  result: TypeParam(0, "T")

entry:
  %cmp = gt %a, %b                          ; Type: bool
  %result = select %cmp, %a, %b             ; Type: TypeParam(0, "T")
  ret %result
```

The `select` instruction acts like `cond ? a : b`. Its type is the **unified type** of both branches — here `TypeParam(0, "T")`.

### 2.4 GIR Data Structures

```cpp
// ir_type.h

enum class IRPrimitive {
    VOID, I1, I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64,
    PTR
};

struct IRType {
    std::variant<
        IRPrimitive,
        PointerType,
        ArrayType,
        FunctionType,
        StructType,
        TypeParam     // <-- the generic key
    > kind;
};

struct TypeParam {
    size_t index;       // positional index (0, 1, 2, …)
    std::string name;   // human-readable ("T", "U", …)
};
```

```cpp
// ir_instruction.h

struct IRValue {
    size_t id;
    IRType type;
};

struct Instruction {
    enum Opcode { ADD, SUB, MUL, …, CALL, BR, RET, PHI, SELECT, … };
    Opcode op;
    std::vector<size_t> operands;       // value ids
    IRType result_type;
    // metadata for instantiation
    std::optional<IRType> target_type;  // for cast/alloc
    std::string callee_name;            // for call
};
```

```cpp
struct IRBlock {
    std::string label;
    std::vector<Instruction> insts;
    // terminators: br/cond_br/ret
};

struct IRFunction {
    std::string name;
    std::vector<TypeParam> type_params;  // template params
    std::vector<IRValue> params;
    IRType return_type;
    std::vector<IRBlock> blocks;
    bool is_generic;                     // has unresolved TypeParams?
};
```

---

## 3. Instantiation: GIR → CIR

When the compiler encounters a template instantiation:

```
max<int>(1, 2)
```

**Step 1 — Match** the call-site types to the template parameters:

```
TypeParam(0, "T")  →  i32
```

**Step 2 — Substitute** every occurrence in the GIR body:

```
%cmp = gt %a, %b    →  %cmp = gt %a, %b      (type: i32 → bool)
%result = select …   →  %result = select …    (type: i32)
```

**Step 3 — Specialize** any instructions that need concrete type lowering:

```
%cmp = gt %a, %b    →  icmp sgt i32 %a, %b   (LLVM-style)
```

**Step 4 — Cache** the resulting CIR function so repeated instantiations with the same type args hit the cache.

### Instantiation Cache

```
Instantiation Key:  (function_name, [concrete types])
                    e.g., ("max", [i32])
                         ("max", [f64])
                         ("max", [MyStruct])

CIR cache:          map<InstantiationKey, unique_ptr<IRFunction>>
```

---

## 4. CIR — Concrete IR

After instantiation, CIR is a **fully concrete SSA IR** with no symbolic types left. All types are lowered to concrete `IRPrimitive`, `StructType`, `PointerType`, etc.

CIR is what gets fed to the **code generator** (register allocation, instruction selection, etc.).

```
function @max<int>:
  params: (%a : i32, %b : i32)
  result: i32

entry:
  %cmp = icmp sgt i32 %a, %b      ; concrete comparison
  %result = select i1 %cmp, i32 %a, i32 %b
  ret i32 %result
```

### CIR optimizations (optional, phase 2):

| Pass                             | Effect                                    |
|----------------------------------|-------------------------------------------|
| Constant folding                 | `add 2, 3 → 5`                            |
| Dead code elimination            | Remove unused values                      |
| Common subexpression elimination | Reuse identical computations              |
| Inlining                         | Replace calls with function body          |
| Specialization                   | Create monomorphized copies for hot paths |

---

## 5. Pipeline Walkthrough

```
┌────────────┐
│ Source     │  template<typename T> T max(T a, T b) { return a > b ? a : b; }
└─────┬──────┘
      │  Parse & Semantic Analysis
      ▼
┌────────────┐
│ AST        │  FunctionDecl "max" with TypeParam in its type
└─────┬──────┘
      │  Lower to GIR
      ▼
┌────────────┐
│ GIR        │  function @max<%T0>  (saved to module)
└─────┬──────┘
      │  Encounter: max(1, 2)
      ▼
┌─────────────────┐
│ Instantiate     │  T → i32
└─────┬───────────┘
      │  Substitute & cache
      ▼
┌────────────┐
│ CIR        │  function @max<int>  (concrete SSA)
└─────┬──────┘
      │  Code generation
      ▼
┌────────────┐
│ Assembly / │  max_int:
│ Machine    │    push rbp
│ Code       │    mov rbp, rsp
│            │    cmp edi, esi
│            │    cmovg eax, edi
│            │    cmovle eax, esi
│            │    pop rbp
│            │    ret
└────────────┘
```

---

## 6. Handling Template Template Parameters & Variadics

```cpp
template<typename T, template<typename> typename Container>
Container<T> wrap(T value);
```

```
function @wrap:
  type_params: (%T0 : Type, %C0 : Template)   ← Template kind
  params: (%value : %T0)
  result: %C0<%T0>                              ← applied template
```

For **variadic templates**, GIR supports:

```
function @printf_wrapper:
  type_params: [%T0, %T1...]                   ← pack
  params: (%fmt : ptr<char>, %args : %T1...)
  result: i32

  ; expand with foreach over pack
  %va_list = alloca va_list
  va_start %va_list
  ; ... iterate over %T1...
```

The GIR `expand` pseudo-instruction marks a pack expansion site:

```
; before substitution:
  %a = expand %args                            ; yields N values
  %r = call @process, %a1, %a2, ..., %aN

; after substitution (T1... = [i32, f64, ptr<char>]):
  %a1 = extract %args, 0
  %a2 = extract %args, 1
  %a3 = extract %args, 2
  %r = call @process, %a1, %a2, %a3
```

---

## 7. Design Decisions Summary

```
┌──────────────────────────────────────────────────────────┐
│  Decision                      │  Choice                 │
├──────────────────────────────────────────────────────────┤
│  Representation                │  SSA with explicit types│
│  Generic mechanism             │  TypeParam in type IR   │
│  Instantiation                 │  Eager, substitution    │
│  Cache                         │  Per (fn, [types]) key  │
│  Template params               │  Types + Templates +    │
│                                │  Packs (variadic)       │
│  Control flow                  │  Basic blocks + Br/Phi  │
│  Memory                        │  alloca/load/store      │
│  Struct access                 │  gep (field index)      │
│  Optimization                  │  Delayed to CIR phase   │
└──────────────────────────────────────────────────────────┘
```

---

## 8. Next Steps for Implementation

```
1. IR Type system         →  ir_type.h / ir_type.cpp
2. GIR instructions       →  ir_instruction.h
3. GIR builder            →  ir_builder.h (lower AST → GIR)
4. Instantiation engine   →  ir_instantiator.h (substitute params)
5. CIR verification       →  ir_verifier.h (no dangling TypeParams)
6. Cache                  →  ir_cache.h (memoize instantiations)
7. Dump/print             →  ir_printer.h (debug output)
8. Codegen backend        →  (separate phase, x86_64 or whatever)
```

Each step is independent and testable. Step 3 (AST → GIR lowering) is the biggest lift — it walks every AST node and emits GIR instructions. Steps 1–2 are pure data structures you can design and verify in isolation.
