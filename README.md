# C-Compiler

a C/C++ compiler i'm hacking on. nowhere near done.

## what works

- preprocessor: `#define`, `#if`/`#ifdef`, `__VA_ARGS__`, string concat, `#embed`, `__has_include`
- lexer spits out tokens
- parser builds an AST (mostly C, some C++ constructs)
- semantic analysis: basic type checking, expression validation
- IR pipeline: builds Generic IR with `TypeParam` for templates, can instantiate + verify it
- codegen doesn't exist yet. the compiler just dumps IR and stops.

## build

```sh
cmake -B build && cmake --build build
./build/C_Compiler test.cpp
```

## structure

| file | what |
|---|---|
| preprocessor.cpp | macro expansion, directives |
| lexer.cpp | tokenizer |
| parser.cpp | recursive descent parser |
| semantic.cpp | name resolution, type checking |
| ast.h | ast node types |
| ir.h / ir_type.h | two-layer ir (gir + cir) |
| ir_builder.cpp | ast -> gir lowering |
| ir_instantiator.cpp | template instantiation (gir -> cir) |
| ir_verifier.cpp | checks for leftover type params |

there's also [ir.md](ir.md) with the full ir design doc.

## status

this is a learning project / hobby thing. lots of gaps, probably plenty of bugs, no backend yet. but the pieces are starting to fit together.
