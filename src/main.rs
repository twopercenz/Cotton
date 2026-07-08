use cotton_lexer::Lexer;

/// 명세 §A.14 "예제" 스니펫을 그대로 토큰화해 결과를 보여준다.
const WORKED_EXAMPLE: &str = "fn greet(name: str) -> String {\n    \"Hello, {name}!\"\n}\n";

fn dump(label: &str, src: &str) {
    println!("=== {label} ===");
    println!("--- source ---\n{src}");
    println!("--- tokens ---");
    let tokens = Lexer::new(src).tokenize();
    for t in &tokens {
        println!("{:>3}:{:<3} {}", t.span.line, t.span.col, t);
    }
    println!();
}

fn main() {
    dump("worked example (spec SS A.14)", WORKED_EXAMPLE);

    // ASI 규칙 예제 (§A.4)
    dump(
        "ASI: multi-line expression (no semicolon inserted mid-expression)",
        "let total = a +\n    b +\n    c\n",
    );
    dump(
        "ASI: multi-line call (semicolon inserted after closing paren)",
        "let result = compute(\n    x, y, z\n)\n",
    );

    // 숫자 리터럴
    dump(
        "numeric literals",
        "42 1_000_000 0xFF_FF 0b1010_1010 10u8 3.14 2.5e10 1_000.5f32 6.02e23\n",
    );

    // 중첩 보간 문자열
    dump(
        "nested string interpolation",
        "let s = \"Total: {items.len() + 1}\"\n",
    );

    // raw 문자열 / raw 식별자
    dump(
        "raw strings & raw identifiers",
        "r\"raw\\path\" r#\"contains \"quotes\" safely\"# let r#type = 1\n",
    );

    // 중첩 블록 주석 & 문서 주석
    dump(
        "nested block comments & doc comments",
        "/* outer /* inner */ still outer */\n/// doc comment\nfn f() {}\n//! module doc\n",
    );

    // 렉서 에러 예시 (복구 가능)
    dump(
        "recoverable lexer errors",
        "let x = \"unterminated\nlet y = 0x\nlet z = $stray\n",
    );
}
