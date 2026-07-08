//! Cotton 토큰 정의 (명세 §A.3, §A.7, §A.9~A.10)

use std::fmt;

/// 소스 파일 내 위치: 줄, 열, 바이트 오프셋 (모두 명세 요구사항)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Span {
    pub line: u32,
    pub col: u32,
    pub byte_offset: usize,
}

impl fmt::Display for Span {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}:{}", self.line, self.col)
    }
}

/// 정수 리터럴 접미사 (§A.8.1)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IntSuffix {
    I8, I16, I32, I64, I128, Isize,
    U8, U16, U32, U64, U128, Usize,
}

impl IntSuffix {
    fn from_str(s: &str) -> Option<Self> {
        use IntSuffix::*;
        Some(match s {
            "i8" => I8, "i16" => I16, "i32" => I32, "i64" => I64, "i128" => I128, "isize" => Isize,
            "u8" => U8, "u16" => U16, "u32" => U32, "u64" => U64, "u128" => U128, "usize" => Usize,
            _ => return None,
        })
    }
}

/// 부동소수점 리터럴 접미사 (§A.8.2)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FloatSuffix {
    F32,
    F64,
}

/// 예약 키워드 (§A.7) — 총 35개
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Keyword {
    // 바인딩
    Let, Var, Const, Static,
    // 함수 & 타입
    Fn, Struct, Enum, Trait, Impl, Type, SelfType, SelfValue,
    // 제어 흐름
    If, Else, Match, For, While, Loop, Break, Continue, Return, In,
    // 에러 처리
    Try, Catch,
    // 모듈
    Module, Use, Pub, As,
    // 제네릭/트레이트
    Where, Dyn,
    // 동시성
    Async, Await, Spawn, Puff,
    // 메모리/안전성
    Unsafe, Move, Extern,
    // 매크로
    Macro,
    // 리터럴-키워드
    True, False,
    // 향후 예약
    Yield, Abstract,
}

impl Keyword {
    /// 식별자 문자열이 키워드인지 확인. 아니면 None (IDENT로 처리).
    pub fn from_str(s: &str) -> Option<Self> {
        use Keyword::*;
        Some(match s {
            "let" => Let, "var" => Var, "const" => Const, "static" => Static,
            "fn" => Fn, "struct" => Struct, "enum" => Enum, "trait" => Trait,
            "impl" => Impl, "type" => Type, "Self" => SelfType, "self" => SelfValue,
            "if" => If, "else" => Else, "match" => Match, "for" => For,
            "while" => While, "loop" => Loop, "break" => Break, "continue" => Continue,
            "return" => Return, "in" => In,
            "try" => Try, "catch" => Catch,
            "module" => Module, "use" => Use, "pub" => Pub, "as" => As,
            "where" => Where, "dyn" => Dyn,
            "async" => Async, "await" => Await, "spawn" => Spawn, "puff" => Puff,
            "unsafe" => Unsafe, "move" => Move, "extern" => Extern,
            "macro" => Macro,
            "true" => True, "false" => False,
            "yield" => Yield, "abstract" => Abstract,
            _ => return None,
        })
    }

    /// ASI 트리거 집합에 포함되는 키워드인지 (§A.4): self, Self, return, break, continue
    pub fn triggers_asi(&self) -> bool {
        matches!(
            self,
            Keyword::SelfValue | Keyword::SelfType | Keyword::Return | Keyword::Break | Keyword::Continue
        )
    }

    pub fn as_str(&self) -> &'static str {
        use Keyword::*;
        match self {
            Let => "let", Var => "var", Const => "const", Static => "static",
            Fn => "fn", Struct => "struct", Enum => "enum", Trait => "trait",
            Impl => "impl", Type => "type", SelfType => "Self", SelfValue => "self",
            If => "if", Else => "else", Match => "match", For => "for",
            While => "while", Loop => "loop", Break => "break", Continue => "continue",
            Return => "return", In => "in",
            Try => "try", Catch => "catch",
            Module => "module", Use => "use", Pub => "pub", As => "as",
            Where => "where", Dyn => "dyn",
            Async => "async", Await => "await", Spawn => "spawn", Puff => "puff",
            Unsafe => "unsafe", Move => "move", Extern => "extern",
            Macro => "macro",
            True => "true", False => "false",
            Yield => "yield", Abstract => "abstract",
        }
    }
}

/// 연산자 (§A.9)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Op {
    // 산술
    Plus, Minus, Star, Slash, Percent,
    // 비교
    EqEq, Ne, Lt, Gt, Le, Ge,
    // 논리
    AndAnd, OrOr, Not,
    // 비트
    Amp, Pipe, Caret, Tilde, Shl, Shr,
    // 대입
    Eq, PlusEq, MinusEq, StarEq, SlashEq, PercentEq,
    AmpEq, PipeEq, CaretEq, ShlEq, ShrEq,
    // 범위
    DotDot, DotDotEq,
    // 화살표
    Arrow,   // ->
    FatArrow, // =>
    // 에러 전파
    Question,
}

/// 구두점/구분자 (§A.9, §A.10)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Punct {
    Comma, Semi, Colon, PathSep, Dot, Underscore,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    AttrStart,  // #[
}

/// 토큰 종류 (§A.3)
#[derive(Debug, Clone, PartialEq)]
pub enum TokenKind {
    Ident(String),
    Keyword(Keyword),
    IntLiteral { text: String, suffix: Option<IntSuffix> },
    FloatLiteral { text: String, suffix: Option<FloatSuffix> },
    CharLiteral(char),
    // 문자열 보간 관련 토큰들 (§A.8.5)
    StringStart,
    StringText(String),
    InterpStart,
    InterpEnd,
    StringEnd,
    /// raw string은 보간이 없으므로 하나의 완결된 토큰으로 방출
    RawString(String),
    BoolLiteral(bool),
    Op(Op),
    Punct(Punct),
    DocComment { text: String, is_module_level: bool }, // /// vs //!
    MacroSigil(String), // $ident
    /// 자동 삽입된 세미콜론 (실제 `;`와 구분해 디버깅에 유용하도록 별도 마킹)
    Semi { inserted: bool },
    Eof,
    /// 복구 가능한 렉서 에러 (§A.12) — 에러를 방출하고 계속 진행
    Error(String),
}

#[derive(Debug, Clone, PartialEq)]
pub struct Token {
    pub kind: TokenKind,
    pub span: Span,
}

impl fmt::Display for Token {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match &self.kind {
            TokenKind::Ident(s) => write!(f, "IDENT({s})"),
            TokenKind::Keyword(k) => write!(f, "KEYWORD({})", k.as_str()),
            TokenKind::IntLiteral { text, suffix } => {
                write!(f, "INT_LITERAL({text}")?;
                if let Some(sfx) = suffix {
                    write!(f, "{:?}", sfx)?;
                }
                write!(f, ")")
            }
            TokenKind::FloatLiteral { text, suffix } => {
                write!(f, "FLOAT_LITERAL({text}")?;
                if let Some(sfx) = suffix {
                    write!(f, "{:?}", sfx)?;
                }
                write!(f, ")")
            }
            TokenKind::CharLiteral(c) => write!(f, "CHAR_LITERAL({c:?})"),
            TokenKind::StringStart => write!(f, "STRING_START(\")"),
            TokenKind::StringText(s) => write!(f, "STRING_TEXT({s})"),
            TokenKind::InterpStart => write!(f, "INTERP_START({{)"),
            TokenKind::InterpEnd => write!(f, "INTERP_END(}})"),
            TokenKind::StringEnd => write!(f, "STRING_END(\")"),
            TokenKind::RawString(s) => write!(f, "RAW_STRING({s:?})"),
            TokenKind::BoolLiteral(b) => write!(f, "BOOL_LITERAL({b})"),
            TokenKind::Op(op) => write!(f, "OP({})", op_str(*op)),
            TokenKind::Punct(p) => write!(f, "{}", punct_display(*p)),
            TokenKind::DocComment { text, is_module_level } => {
                write!(f, "DOC_COMMENT({}{})", if *is_module_level { "//!" } else { "///" }, text)
            }
            TokenKind::MacroSigil(name) => write!(f, "MACRO_SIGIL(${name})"),
            TokenKind::Semi { inserted } => {
                if *inserted { write!(f, "SEMI*") } else { write!(f, "SEMI") }
            }
            TokenKind::Eof => write!(f, "EOF"),
            TokenKind::Error(msg) => write!(f, "ERROR({msg})"),
        }
    }
}

fn punct_display(p: Punct) -> &'static str {
    match p {
        Punct::Comma => "PUNCT(,)",
        Punct::Semi => "PUNCT(;)",
        Punct::Colon => "PUNCT(:)",
        Punct::PathSep => "PUNCT(::)",
        Punct::Dot => "PUNCT(.)",
        Punct::Underscore => "PUNCT(_)",
        Punct::LParen => "DELIM(()",
        Punct::RParen => "DELIM())",
        Punct::LBrace => "DELIM({)",
        Punct::RBrace => "DELIM(})",
        Punct::LBracket => "DELIM([)",
        Punct::RBracket => "DELIM(])",
        Punct::AttrStart => "ATTRIBUTE_START(#[)",
    }
}

pub fn op_str(op: Op) -> &'static str {
    use Op::*;
    match op {
        Plus => "+", Minus => "-", Star => "*", Slash => "/", Percent => "%",
        EqEq => "==", Ne => "!=", Lt => "<", Gt => ">", Le => "<=", Ge => ">=",
        AndAnd => "&&", OrOr => "||", Not => "!",
        Amp => "&", Pipe => "|", Caret => "^", Tilde => "~", Shl => "<<", Shr => ">>",
        Eq => "=", PlusEq => "+=", MinusEq => "-=", StarEq => "*=", SlashEq => "/=", PercentEq => "%=",
        AmpEq => "&=", PipeEq => "|=", CaretEq => "^=", ShlEq => "<<=", ShrEq => ">>=",
        DotDot => "..", DotDotEq => "..=",
        Arrow => "->", FatArrow => "=>",
        Question => "?",
    }
}

pub(crate) fn int_suffix_from_str(s: &str) -> Option<IntSuffix> {
    IntSuffix::from_str(s)
}
