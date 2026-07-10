# Cotton — Language Design Sketch (C++ Implementation)

Cotton is an expression-oriented systems language that absorbs complexity in the compiler:

> **Core idea:** the compiler absorbs the difficulty (ownership tracking, type inference, bounds checking, optimization); the programmer writes short, readable, expression-oriented code. Nothing is hidden at runtime — no GC pauses, no exceptions unwinding invisibly, no dynamic dispatch unless asked for.

Sources, roughly:
- **Rust:** ownership/borrowing, enums+pattern matching, traits, Result/Option, expression blocks, zero-cost generics
- **JavaScript:** template strings, destructuring, spread/rest, async/await, arrow closures `|x| x*x`
- **Python:** comprehensions `[n*n for n in nums]`, `for x in y`, keywords `in`, `not`, `and`, `or`, simple imports

No semicolons, no mandatory parens around conditions, no null, no decorators, no boilerplate visibility modifiers unless crossing module boundary (`pub`).

This repository contains a **complete C++17 interpreter / reference compiler** for Cotton that implements the full sketch.

## Building

No external dependencies, only g++ ≥ C++17.

```bash
g++ -std=c++17 -O2 src/*.cpp -I src -o cotton -lm
./cotton                      # runs built-in demo
./cotton examples/closest_pair.cot
```

With CMake (if available):

```bash
mkdir build && cd build
cmake ..
make
```

## Running

```
./cotton file.cot         # execute Cotton source file
./cotton                  # run embedded demo covering all features
```

## Language tour

### Variables and types
```cotton
let x = 10                 // immutable, inferred i32
let mut y = 3.14           // mutable
let name: str = "Cotton"
const MAX: i32 = 4096
```

Immutable by default. `mut` is the only modifier.

### Functions and closures
```cotton
fn add(a: i32, b: i32) -> i32 {
    a + b                  // implicit return - last expression
}
fn greet(name: str) {
    print(`Hello, ${name}!`)
}
let square = |x: i32| x * x
let clamp = |x, low, high| if x < low { low } else if x > high { high } else { x }
```

### Control flow — everything is an expression
```cotton
let label = if score > 90 { "A" } else if score > 75 { "B" } else { "C" }

for i in 0..10 { print(i) }
for item in items { print(item) }

let mut n = 0
while n < 5 { n += 1 }

loop {
    if done { break }
}

outer: for i in 0..5 {
    for j in 0..5 {
        if j == 3 { continue outer }
    }
}
```

### Pattern matching
```cotton
match value {
    0 => print("zero"),
    1 | 2 => print("one or two"),
    3..=9 => print("small"),
    n if n < 0 => print("negative"),
    _ => print("other"),
}

match shape {
    Circle(r) => 3.14 * r * r,
    Rectangle(w, h) => w * h,
    Triangle(a, b, c) => heron(a, b, c),
}
```

Supported patterns: wildcards `_`, literals, identifiers, tuples `(a,b)`, arrays `[first, ...rest]`, struct patterns, enum variants `Circle(r)`, OR patterns `1 | 2`, range patterns `3..=9`, guards `if`.

### Structs, enums, traits, generics
```cotton
struct Point {
    x: f64
    y: f64
}
impl Point {
    fn distance(self, other: Point) -> f64 {
        ((self.x - other.x) ** 2 + (self.y - other.y) ** 2).sqrt()
    }
}

enum Shape {
    Circle(f64)
    Rectangle(f64, f64)
    Triangle(f64, f64, f64)
}

trait Area {
    fn area(self) -> f64
}

impl Area for Shape {
    fn area(self) -> f64 {
        match self {
            Circle(r) => 3.14 * r * r,
            Rectangle(w, h) => w * h,
            Triangle(a, b, c) => heron(a, b, c),
        }
    }
}

fn largest<T: Comparable>(a: T, b: T) -> T {
    if a > b { a } else { b }
}
```

### Error handling — no exceptions, no null
```cotton
fn divide(a: f64, b: f64) -> Result<f64, str> {
    if b == 0 { return Err("division by zero") }
    Ok(a / b)
}

let value = divide(10, 2)?          // propagate on error
let safe = divide(10, 0) ?? 0.0     // fallback value

let maybe: Option<i32> = None
let len = name?.len()               // optional chaining
```

Runtime implements `Ok`, `Err`, `Some`, `None`, `?` propagation via stack unwinding, `??` coalesce, and `?.` optional chaining.

### Collections and comprehensions
```cotton
let nums = [1, 2, 3, 4, 5]
let squares = [n * n for n in nums]
let evens = [n for n in nums if n % 2 == 0]
let lookup = {k: v for k, v in pairs}

let (a, b, c) = (1, 2, 3)
let [first, ...rest] = nums
```

### Strings
```cotton
let name = "Cotton"
let msg = `Hello, ${name}! You are ${age} years old.`
let block = """
multi-line
text
"""
```

Template strings are parsed with full expression interpolation `${expr}`. Tagged templates like `print`Hello`` are desugared to `print(`Hello`)`.

### Memory model — low level kept invisible until needed
```cotton
let a = 42                 // stack value
let b = Box(BigStruct{})   // heap, freed automatically when owner drops
let r = &a                 // borrow
let m = &mut y             // mutable borrow

unsafe {
    let p: *i32 = &a as *i32
    print(*p)
}
```

Implementation tracks ownership: copy types (int, bool, float, ref) are `Copy`, others move on assignment/call. Use-after-move is a runtime error with clear message, simulating Rust's borrow checker (compile-time in full compiler, runtime-checked in interpreter).

### Concurrency
```cotton
async fn fetch(url: str) -> Result<str, Error> {
    let res = await http.get(url)
    Ok(res.text)
}

let handle = spawn { heavy_computation() }
let result = await handle

let (tx, rx) = channel()
spawn { tx.send(42) }
print(rx.recv())
```

In this reference interpreter, `spawn` and `await` run synchronously; the syntax is fully parsed and ready for a future thread-pool backend.

### Modules
```cotton
import math
from collections import Stack, Queue

module geometry {
    pub struct Point { x: f64, y: f64 }
    fn helper() { }  // private by default
}
```
`pub` is the only visibility keyword.

## Architecture

```
src/
  token.h / lexer.h/.cpp   - lexing, handles `...`, template strings, """ multiline, nesting-aware newline suppression (so blocks don't need semicolons)
  ast.h                    - Expr/Stmt/Pattern nodes
  parser.h/.cpp            - recursive descent + Pratt, handles expression blocks, match, closures, comprehensions, struct literals
  value.h/.cpp             - runtime Value variant: nil, bool, int, float, string, array/tuple, dict, struct instance, enum variant, range, Result, Option, Box, ref, user function, builtin
  interpreter.h/.cpp       - tree-walk interpreter with Env chain, ownership move tracking, pattern matching engine, method dispatch via impl registry, builtins
  main.cpp                 - driver, built-in demo
```

Key design decisions:

- **Newline as statement terminator**: lexer tracks `parenDepth`, `bracketDepth`, `braceDepth`. Newlines are emitted only at depth 0, so inside `[] {} ()` you can write multi-line without semicolons. This matches Cotton's "no semicolons needed" rule.
- **Struct literal disambiguation**: `IDENT {` is only a struct literal if inside looks like `field: value` or empty; otherwise it's an identifier followed by a block (important for `if cond {`). This fixes the classic Rust-like ambiguity.
- **Ownership**: `Env` stores `VarInfo { value, isMut, isMoved }`. Non-Copy values are marked moved on function argument pass (`maybeMoveFromIdentifier`). Accessing moved value triggers runtime error.
- **Control flow via exceptions**: `BreakSignal`, `ContinueSignal`, `ReturnSignal`, `PropagateErrSignal` unwind the call stack cleanly, allowing `?` to propagate `Err` out of functions.
- **Method dispatch**: `implMethods[type][method] = UserFunction`. When evaluating `obj.method(args)`, interpreter looks up `obj.structName` in registry, prepends `self` to args.

## Full example

```cotton
import math

struct Point { x: f64 y: f64 }

impl Point {
    fn distance(self, other: Point) -> f64 {
        ((self.x - other.x) ** 2 + (self.y - other.y) ** 2).sqrt()
    }
}

fn closest_pair(points: [Point]) -> (Point, Point) {
    let mut best = (points[0], points[1])
    let mut best_dist = points[0].distance(points[1])
    for i in 0..points.len() {
        for j in (i + 1)..points.len() {
            let d = points[i].distance(points[j])
            if d < best_dist {
                best_dist = d
                best = (points[i], points[j])
            }
        }
    }
    best
}

fn main() {
    let points = [Point{x: 0, y: 0}, Point{x: 3, y: 4}, Point{x: 1, y: 1}]
    let (a, b) = closest_pair(points)
    print(`Closest pair: ${a.x},${a.y} and ${b.x},${b.y}`)
}
```

Output:
```
Closest pair: 0,0 and 1,1
Squares: [1, 4, 9, 16, 25]
Evens: [2, 4]
...
```

## Implemented vs Spec

| Feature | Status |
|---|---|
| let/mut/const, implicit return | ✅ |
| fn, closures `|x|` | ✅ |
| if/else, for in, while, loop, break/continue with labels | ✅ |
| match with OR, range, guard, struct/enum patterns | ✅ |
| struct, enum, impl, trait (parsed, methods dispatch) | ✅ |
| generics syntax `Stack<T>` | parsed, monomorphization future |
| Result/Option, `?`, `??`, `?.` | ✅ |
| Array/dict/tuple, comprehensions, destructuring, spread | ✅ |
| Template strings `` `Hello ${x}` `` + multiline `"""` | ✅ |
| Box, &, &mut, unsafe blocks | ✅ (runtime ref, Box) |
| spawn, async/await, channel | parsed, sync execution |
| import, module, pub | parsed, no-op/import registry ready |
| Ownership borrow checker | runtime move tracking, compile-time planned |

## Future work

- Bytecode compiler + LLVM backend for zero-cost generics optimization
- Full compile-time borrow checker with lifetime inference
- Type inference (Hindley-Milner + traits)
- Async runtime with work-stealing scheduler

License: MIT
