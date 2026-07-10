# Cotton Design Notes

## Philosophy

Cotton tries to merge Rust's safety with Python/JS ergonomics:

- No GC, no runtime type info, no hidden allocations.
- Everything is an expression, braces return last value.
- Ownership tracked (in reference impl, runtime move check; full compiler does compile-time borrow checking).
- Minimal keywords: `pub` is only visibility, `mut` is only local mutability modifier.

## Lexical

- Line comment `//`, block `/* */`
- `;` only needed to pack multiple statements on one line; newline ends statements unless inside `() [] {}`.
- No indentation sensitivity.
- Identifiers: `[a-zA-Z_][a-zA-Z0-9_]*`, `_` is wildcard pattern.
- Numbers: `10`, `3.14`, underscores allowed `1_000`.
- Strings: `"..."`, `'...'`, supports escapes, multiline `"""..."""`, template `` `Hello ${name}` ``.
- Operators: `+ - * / % **`, `== != < > <= >=`, `.. ..=`, `=> ->`, `? ?. ??`, `& &mut`, `=`, `+= -=` etc.

## Grammar Highlights

```
program       := decl*
decl         := pub? async? fnDecl | structDecl | enumDecl | implDecl | traitDecl | moduleDecl | importDecl | letDecl | constDecl | stmt
stmt         := letDecl | returnStmt | breakStmt | continueStmt | exprStmt
expr         := assignment
assignment   := coalesce ( "=" | "+=" ... assignment )?
coalesce     := or ( "??" or )*
or           := and ( "or" and )*
and          := equality ( "and" equality )*
equality     := comparison ( "==" | "!=" comparison )*
comparison   := range ( "<" | ">" | "<=" | ">=" range )*
range        := term ( ".." | "..=" term )?
term         := factor ( "+" | "-" factor )*
factor       := power ( "*" | "/" | "%" power )*
power        := unary ( "**" power )?
unary        := ("-" | "!" | "not" | "&" mut? | "await") unary | postfix
postfix      := primary ( "(" args ")" | "[" expr "]" | "." IDENT | "?." IDENT | "`template`" )* "?"*
primary      := NUMBER | STRING | TEMPLATE | "true" | "false" | "None"
              | "{" blockOrDict "}"
              | "[" arrayOrComprehension "]"
              | "(" tupleOrGrouping ")"
              | "if" expr block ("else" block | "else" ifExpr)?
              | "for" pattern "in" expr block
              | "while" expr block
              | "loop" block
              | "match" expr "{" matchArm* "}"
              | "|" params "|" expr
              | "Box" "(" expr ")" | "unsafe" block | "spawn" block
              | IDENT "{" structFields "}"   // struct literal
              | IDENT
```

Ambiguity handling:
- `IDENT {` is struct literal only if inside first token after `{` is `IDENT COLON` or `}`. Otherwise it's IDENT followed by block (e.g., `if cond { ... }`).
- `{` alone is block unless `IDENT:` or `STRING:` pattern inside → dict.

## Ownership Model (simplified)

In full compiler, would be compile-time borrow checker similar to Rust. In this reference interpreter:

- Values are `Copy` if `Nil, Bool, Int, Float, Ref`.
- Others (`Array, Struct, Enum, Box`) move on assignment and call: identifier source marked moved.
- Accessing moved variable → runtime error.
- `&` creates `Ref`, `&mut` creates `MutRef`, both Copy, so borrowing doesn't move.

This simulates linear ownership without lifetime parameters; future work adds lifetimes.

## Type System Sketch

Not enforced at runtime (dynamic), but syntax fully parsed:

- `let x: Type = ...`
- `fn foo<T: Trait>(a: T) -> T`
- Struct generic params `<T>`
- Enums with payloads.

For full compiler, would implement Hindley-Milner + trait bounds + zero-cost monomorphization (like Rust).

## Error Handling

- `Result<T,E>` is `Ok(T)` | `Err(E)` at runtime.
- `?` postfix: if `Err`, throws `PropagateErrSignal` which unwinds to function return as `Err`. If `Ok`, unwraps.
- `??` coalesce: left if not `None`/`Err`/`Nil`, else right.
- `?.` optional chain: if `None`/`Nil`, returns `None`, else field.

## Comprehensions

Parsed as `[ expr for pattern in iterable if cond ...]`. Interpreter expands via DFS recursion over clauses, supporting multiple `for` and optional `if`.

Dict comprehension `{k: v for ...}` same.

## Template Strings

Lexer captures raw inner content with `${` ... `}` balanced braces. Parser splits into text/expr parts, recursively lexing/parsing expr substrings. Evaluator concatenates `toString`.

## Concurrency

Syntax fully parsed, execution currently synchronous. Future backend would use Rust-like async transform to state machines and work-stealing executor.

## Modules

`import math`, `from collections import Stack, Queue`, `module {}`. `pub` marks export. In interpreter, imports are no-op but registry ready for file loader.

## Future Compiler Pipeline

1. Lex → Parse → AST
2. Type inference + trait solving
3. Borrow checking + move analysis
4. MIR (mid-level IR) with owned/borrowed distinction
5. LLVM IR / C backend
6. Optimizations (inlining, bounds check elision)
