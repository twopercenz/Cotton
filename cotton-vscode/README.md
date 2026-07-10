# Cotton Language – VS Code Extension

Syntax highlighting for **Cotton**, the language where *the compiler absorbs the difficulty* – ownership tracking, type inference, bounds checking – and the programmer writes short readable code. No GC, no hidden exceptions, no dynamic dispatch unless asked for.

Inspired by:
- **Rust**: ownership/borrowing, `Result/Option`, `match`, traits, zero-cost generics
- **JavaScript**: `` `Hello ${name}` `` template strings, `[first, ...rest]` destructuring, `|x| x*x` closures
- **Python**: comprehensions `[n*n for n in nums]`, `for x in y`

## Features

- `.cot` / `.cotton` file association
- Full TextMate grammar:
  - keywords: `let mut const fn struct enum impl trait if else for in while loop match break continue return import module pub async await spawn unsafe Box and or not`
  - types: `i32 f64 str bool Option Result` + any `CamelCase` type
  - strings: `"..."`, `'...'`, `"""multiline"""`, interpolated `` `Hello ${name}` `` with nested `${expr}` highlighting
  - comments `//` and `/* */` with TODO highlighting
  - numbers, bools, `None Some Ok Err nil`
  - operators: `-> => ?? ?. ? .. ..= | == != <= >= += -= ** & &mut`
- Bracket matching and auto-closing for `{} [] () "" '' ```
- Snippets: `fn`, `struct`, `impl`, `let`, `letmut`, `for`, `if`, `match`, `closure`, `tstr`, `closestpair`

## Installation

### Option 1: Manual
1. Copy this folder `cotton-vscode` to `~/.vscode/extensions/cotton-lang-0.1.0/`
2. Reload VS Code

### Option 2: VSIX package
```bash
npm install -g @vscode/vsce
cd cotton-vscode
vsce package
code --install-extension cotton-lang-0.1.0.vsix
```

## Example

```cotton
struct Point { x: f64 y: f64 }

impl Point {
    fn distance(self, other: Point) -> f64 {
        ((self.x - other.x) ** 2 + (self.y - other.y) ** 2).sqrt()
    }
}

fn main() {
    let points = [Point{x: 0, y: 0}, Point{x: 3, y: 4}]
    let squares = [n * n for n in [1,2,3]]
    let msg = `Count: ${points.len()}`
    print(msg)
}
```

Colors adapt to your VS Code theme.

## File icon (optional)

If you want a file icon, add to your icon theme or install `vscode-icons` – `.cot` will be recognized as Cotton.

License: MIT
