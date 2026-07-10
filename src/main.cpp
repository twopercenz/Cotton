#include "lexer.h"
#include "parser.h"
#include "interpreter.h"
#include <iostream>
#include <fstream>
#include <sstream>

using namespace cotton;

int main(int argc, char** argv) {
    std::cout << "Cotton language v0.1 — compiler absorbs difficulty, programmer writes short readable code\n";
    std::cout << "No GC, ownership tracking, expression-oriented, zero-cost abstractions\n\n";

    std::string source;
    std::string filename;

    if (argc >= 2) {
        filename = argv[1];
        std::ifstream f(filename);
        if (!f) {
            std::cerr << "Cannot open file: " << filename << "\n";
            return 1;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        source = ss.str();
    } else {
        // run built-in demo if no file given
        source = R"(
import math

struct Point {
    x: f64
    y: f64
}

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

fn divide(a: f64, b: f64) -> Result<f64, str> {
    if b == 0 {
        return Err("division by zero")
    }
    Ok(a / b)
}

fn main() {
    let points = [Point{x: 0, y: 0}, Point{x: 3, y: 4}, Point{x: 1, y: 1}]
    let (a, b) = closest_pair(points)
    print(`Closest pair: ${a.x},${a.y} and ${b.x},${b.y}`)

    // Comprehensions
    let nums = [1, 2, 3, 4, 5]
    let squares = [n * n for n in nums]
    print(`Squares: ${squares}`)

    let evens = [n for n in nums if n % 2 == 0]
    print(`Evens: ${evens}`)

    // Pattern matching
    let shape = "Circle"
    match shape {
        "Circle" => print("It's a circle"),
        "Rect" | "Square" => print("rect"),
        _ => print("other")
    }

    // Result handling
    let value = divide(10, 2)?
    print(`10/2 = ${value}`)
    let safe = divide(10, 0) ?? 0.0
    print(`safe divide = ${safe}`)

    // Optional chaining demo
    let maybe: Option<i32> = None
    // let len = maybe?.len() // would be None

    // Destructuring
    let [first, ...rest] = nums
    print(`First: ${first}, Rest: ${rest}`)

    // Ownership demo: Box
    let boxed = Box(Point{x: 10, y: 20})
    print(`Boxed point: ${boxed.value.x}, ${boxed.value.y}`)

    // Closures
    let square = |x: i32| x * x
    print(`square(5) = ${square(5)}`)

    // Template strings and multiline
    let block = """
    multi-line
    text
    """
    print(block)
}
)";
        filename = "<builtin demo>";
    }

    Lexer lexer(source);
    auto tokens = lexer.tokenize();

    // Debug token count
    // for (auto& t: tokens) std::cout << tokenTypeToString(t.type) << " '" << t.lexeme << "'\n";

    Parser parser(tokens);
    Program prog = parser.parseProgram();

    if (parser.hadError) {
        std::cerr << "Parsing failed.\n";
        return 1;
    }

    Interpreter interp;
    try {
        auto result = interp.interpret(prog);
        std::cout << "\nProgram finished with: " << result->toString() << "\n";
    } catch (std::exception& e) {
        std::cerr << "Interpreter aborted: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
