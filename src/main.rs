use logos::Logos;

// 1. 매크로(#[derive(Logos)]) 하나만 붙이면 끝납니다.
#[derive(Logos, Debug, PartialEq)]
enum Token {
    // 단순 키워드나 기호는 #[token]으로 바로 지정!
    #[token("let")]
    Let,

    #[token("mut")]
    Mut,

    #[token("=")]
    Assign,

    // 변수명이나 숫자는 정규표현식(Regex)으로 한 방에 처리!
    #[regex("[a-zA-Z_][a-zA-Z0-9_]*", |lex| lex.slice().to_string())]
    Identifier(String),

    // 숫자를 파싱해서 바로 i64 타입으로 넣어버리는 마법
    #[regex("[0-9]+", |lex| lex.slice().parse::<i64>().unwrap())]
    Number(i64),

    // 띄어쓰기나 엔터는 알아서 무시(skip)하라고 명령
    #[regex(r"[ \t\n\f]+", logos::skip)]
    Error,
}

fn main() {
    let code = "let mut zenith = 2026";
    
    // 2. 단 한 줄로 렉서 엔진 가동!
    let mut lexer = Token::lexer(code);

    // 3. 토큰을 쭉쭉 뽑아냅니다.
    while let Some(token) = lexer.next() {
        println!("{:?}", token);
    }
}