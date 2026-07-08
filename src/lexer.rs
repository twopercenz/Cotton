//! Cotton 렉서 (명세 §A.4 ~ §A.14 구현)
//!
//! - ASI(자동 세미콜론 삽입) 규칙 (§A.4)
//! - 중첩 가능한 블록 주석 & 문서 주석 (§A.5)
//! - 식별자 / raw 식별자 (§A.6)
//! - 키워드 (§A.7)
//! - 정수 / 실수 / 문자 / 문자열(보간 포함) 리터럴 (§A.8)
//! - 연산자 / 구두점 / 구분자 (§A.9, §A.10)
//! - 속성 / 매크로 시길 (§A.11)
//! - 복구 가능한 에러 보고 (§A.12)

use crate::token::{int_suffix_from_str, FloatSuffix, Keyword, Op, Punct, Span, Token, TokenKind};
use unicode_xid::UnicodeXID;

/// 문자열 보간을 처리하기 위한 모드 스택의 프레임 (§A.8.5)
///
/// `interp_depth == 0` 이면 현재 이 문자열의 리터럴 텍스트를 소비 중(InString 모드),
/// `> 0` 이면 `{ }` 안의 표현식을 일반 토큰처럼 렉싱 중(InterpExpr 모드)이며
/// 값은 중첩된 `{` 깊이를 추적한다.
struct StringFrame {
    interp_depth: u32,
}

pub struct Lexer<'a> {
    src: &'a str,
    chars: Vec<char>,
    byte_offsets: Vec<usize>, // chars[i] 가 시작하는 바이트 오프셋
    pos: usize,               // chars 인덱스
    line: u32,
    col: u32,
    /// 문자열 보간 모드 스택. 비어 있으면 최상위(Normal) 모드.
    string_stack: Vec<StringFrame>,
    /// ASI 판단을 위해, 현재 줄에서 마지막으로 방출한 "의미있는" 토큰이
    /// 트리거 집합에 속하는지 기록.
    last_triggers_asi: bool,
    /// 이미 EOF 토큰을 방출했는지
    emitted_eof: bool,
}

impl<'a> Lexer<'a> {
    pub fn new(src: &'a str) -> Self {
        let mut chars = Vec::new();
        let mut byte_offsets = Vec::new();
        for (i, c) in src.char_indices() {
            byte_offsets.push(i);
            chars.push(c);
        }
        // BOM 제거 (§A.2: "no BOM required (stripped if present)")
        let mut start = 0;
        if chars.first() == Some(&'\u{FEFF}') {
            start = 1;
        }
        Lexer {
            src,
            chars,
            byte_offsets,
            pos: start,
            line: 1,
            col: 1,
            string_stack: Vec::new(),
            last_triggers_asi: false,
            emitted_eof: false,
        }
    }

    fn cur_span(&self) -> Span {
        let byte_offset = self.byte_offsets.get(self.pos).copied().unwrap_or(self.src.len());
        Span { line: self.line, col: self.col, byte_offset }
    }

    fn peek(&self) -> Option<char> {
        self.chars.get(self.pos).copied()
    }

    fn peek_at(&self, n: usize) -> Option<char> {
        self.chars.get(self.pos + n).copied()
    }

    fn bump(&mut self) -> Option<char> {
        let c = self.peek()?;
        self.pos += 1;
        if c == '\n' {
            self.line += 1;
            self.col = 1;
        } else {
            self.col += 1;
        }
        Some(c)
    }

    fn eat(&mut self, expected: char) -> bool {
        if self.peek() == Some(expected) {
            self.bump();
            true
        } else {
            false
        }
    }

    /// 다음 토큰 하나를 생성한다. 스트림이 끝나면 이후 계속 EOF를 반환.
    pub fn next_token(&mut self) -> Token {
        loop {
            if let Some(frame_active) = self.string_stack.last().map(|f| f.interp_depth == 0) {
                if frame_active {
                    return self.lex_string_text();
                }
                // interp_depth > 0: 일반 토큰 렉싱으로 진입하되 { } " 를 가로챈다.
                return self.lex_normal(true);
            }
            return self.lex_normal(false);
        }
    }

    /// 모든 토큰을 EOF까지 수집.
    pub fn tokenize(mut self) -> Vec<Token> {
        let mut out = Vec::new();
        loop {
            let tok = self.next_token();
            let is_eof = matches!(tok.kind, TokenKind::Eof);
            out.push(tok);
            if is_eof {
                break;
            }
        }
        out
    }

    // ------------------------------------------------------------------
    // Normal 모드 (§A.3, §A.9, §A.10) + ASI (§A.4)
    // ------------------------------------------------------------------

    fn lex_normal(&mut self, in_interp: bool) -> Token {
        loop {
            let Some(c) = self.peek() else {
                if self.emitted_eof {
                    return self.make(TokenKind::Eof, self.cur_span());
                }
                // EOF 도달 — 줄 끝 처리 없이 바로 EOF (마지막 줄에 대한 ASI는
                // 이미 개행 처리 시점에 끝났어야 함)
                self.emitted_eof = true;
                return self.make(TokenKind::Eof, self.cur_span());
            };

            // 공백/개행 처리
            if c == '\n' {
                let had_trigger = self.last_triggers_asi;
                self.bump();
                if !in_interp && had_trigger {
                    self.last_triggers_asi = false;
                    return self.make(TokenKind::Semi { inserted: true }, self.cur_span());
                }
                continue;
            }
            if c.is_whitespace() {
                self.bump();
                continue;
            }

            // 주석 (§A.5) — 공백처럼 취급되어 ASI 트리거 상태를 바꾸지 않음
            if c == '/' && self.peek_at(1) == Some('/') {
                if self.peek_at(2) == Some('/') && self.peek_at(3) != Some('/') {
                    return self.lex_doc_comment(false);
                }
                if self.peek_at(2) == Some('!') {
                    return self.lex_doc_comment(true);
                }
                self.skip_line_comment();
                continue;
            }
            if c == '/' && self.peek_at(1) == Some('*') {
                let span = self.cur_span();
                if let Err(msg) = self.skip_block_comment() {
                    self.last_triggers_asi = false;
                    return self.make(TokenKind::Error(msg), span);
                }
                continue;
            }

            let span = self.cur_span();

            // 문자열 진입
            if c == '"' {
                self.bump();
                self.string_stack.push(StringFrame { interp_depth: 0 });
                self.last_triggers_asi = false;
                return self.make(TokenKind::StringStart, span);
            }

            // interp 표현식 내부에서만 의미 있는 특수 문자
            if in_interp && c == '{' {
                self.bump();
                if let Some(f) = self.string_stack.last_mut() {
                    f.interp_depth += 1;
                }
                self.last_triggers_asi = false;
                return self.make(TokenKind::Punct(Punct::LBrace), span);
            }
            if in_interp && c == '}' {
                self.bump();
                let close_interp = if let Some(f) = self.string_stack.last_mut() {
                    f.interp_depth -= 1;
                    f.interp_depth == 0
                } else {
                    false
                };
                if close_interp {
                    self.last_triggers_asi = false;
                    return self.make(TokenKind::InterpEnd, span);
                }
                self.last_triggers_asi = true; // '}' 는 ASI 트리거 대상
                return self.make(TokenKind::Punct(Punct::RBrace), span);
            }

            // raw 문자열 / raw 식별자: r"...", r#"...  또는 r#ident
            if c == 'r' && (self.peek_at(1) == Some('"') || self.peek_at(1) == Some('#')) {
                if let Some(tok) = self.try_lex_raw(span) {
                    return tok;
                }
            }

            // 식별자 / 키워드
            if is_ident_start(c) {
                return self.lex_ident_or_keyword(span);
            }
            if c == '_' {
                return self.lex_ident_or_keyword(span);
            }

            // 숫자
            if c.is_ascii_digit() {
                return self.lex_number(span);
            }

            // 문자 리터럴
            if c == '\'' {
                return self.lex_char(span);
            }

            // 매크로 시길
            if c == '$' {
                return self.lex_macro_sigil(span);
            }

            // 속성 시작 #[
            if c == '#' && self.peek_at(1) == Some('[') {
                self.bump();
                self.bump();
                self.last_triggers_asi = false;
                return self.make(TokenKind::Punct(Punct::AttrStart), span);
            }

            // 연산자 / 구두점 / 구분자
            return self.lex_operator_or_punct(span);
        }
    }

    fn make(&self, kind: TokenKind, span: Span) -> Token {
        Token { kind, span }
    }

    // ------------------------------------------------------------------
    // 주석 (§A.5)
    // ------------------------------------------------------------------

    fn skip_line_comment(&mut self) {
        while let Some(c) = self.peek() {
            if c == '\n' {
                break;
            }
            self.bump();
        }
    }

    /// 중첩 가능한 블록 주석. `/*` 는 이미 확인만 했고 아직 소비 전.
    fn skip_block_comment(&mut self) -> Result<(), String> {
        self.bump(); // '/'
        self.bump(); // '*'
        let mut depth = 1u32;
        loop {
            match self.peek() {
                None => return Err("unterminated block comment".to_string()),
                Some('/') if self.peek_at(1) == Some('*') => {
                    self.bump();
                    self.bump();
                    depth += 1;
                }
                Some('*') if self.peek_at(1) == Some('/') => {
                    self.bump();
                    self.bump();
                    depth -= 1;
                    if depth == 0 {
                        return Ok(());
                    }
                }
                Some(_) => {
                    self.bump();
                }
            }
        }
    }

    fn lex_doc_comment(&mut self, is_module_level: bool) -> Token {
        let span = self.cur_span();
        // "///" 또는 "//!" 소비
        self.bump();
        self.bump();
        self.bump();
        let mut text = String::new();
        while let Some(c) = self.peek() {
            if c == '\n' {
                break;
            }
            text.push(c);
            self.bump();
        }
        self.last_triggers_asi = false; // 주석은 ASI 판단에서 공백처럼 취급
        self.make(TokenKind::DocComment { text: text.trim().to_string(), is_module_level }, span)
    }

    // ------------------------------------------------------------------
    // 식별자 / 키워드 (§A.6, §A.7)
    // ------------------------------------------------------------------

    fn lex_ident_or_keyword(&mut self, span: Span) -> Token {
        // raw 식별자 r#keyword 는 try_lex_raw 에서 먼저 처리되므로 여기 도달하지 않음
        let mut s = String::new();
        while let Some(c) = self.peek() {
            if is_ident_continue(c) {
                s.push(c);
                self.bump();
            } else {
                break;
            }
        }

        if s == "true" || s == "false" {
            self.last_triggers_asi = true; // BOOL_LITERAL, 리터럴이므로 트리거 대상
            return self.make(TokenKind::BoolLiteral(s == "true"), span);
        }

        if let Some(kw) = Keyword::from_str(&s) {
            self.last_triggers_asi = kw.triggers_asi();
            return self.make(TokenKind::Keyword(kw), span);
        }

        self.last_triggers_asi = true; // IDENT, 트리거 대상
        self.make(TokenKind::Ident(s), span)
    }

    // ------------------------------------------------------------------
    // raw 문자열 / raw 식별자 (§A.6, §A.8.5)
    // ------------------------------------------------------------------

    /// `r` 로 시작하는 위치에서 raw string(`r"..."`, `r#"...  "#`) 또는
    /// raw 식별자(`r#ident`)를 시도한다. 해당하지 않으면 None 반환(호출부가
    /// 평범한 식별자 `r`로 폴백하도록).
    fn try_lex_raw(&mut self, span: Span) -> Option<Token> {
        // 얼마나 많은 '#' 이 오는지, 그 뒤가 '"'인지 ident-start 인지 확인.
        let mut n = 1; // 'r' 다음부터 세기 시작
        while self.peek_at(n) == Some('#') {
            n += 1;
        }
        let hash_count = n - 1;
        match self.peek_at(n) {
            Some('"') => {
                // raw string: r + '#'*hash_count + '"'
                self.bump(); // r
                for _ in 0..hash_count {
                    self.bump();
                }
                self.bump(); // "
                let mut text = String::new();
                loop {
                    match self.peek() {
                        None => {
                            self.last_triggers_asi = false;
                            return Some(self.make(
                                TokenKind::Error("unterminated raw string".to_string()),
                                span,
                            ));
                        }
                        Some('"') => {
                            // closing 은 '"' 뒤에 hash_count개의 '#' 이 필요
                            let closes = (0..hash_count).all(|i| self.peek_at(1 + i) == Some('#'));
                            if closes {
                                self.bump(); // "
                                for _ in 0..hash_count {
                                    self.bump();
                                }
                                self.last_triggers_asi = true; // STRING_*, 리터럴
                                return Some(self.make(TokenKind::RawString(text), span));
                            } else {
                                text.push('"');
                                self.bump();
                            }
                        }
                        Some(c) => {
                            text.push(c);
                            self.bump();
                        }
                    }
                }
            }
            Some(c) if hash_count > 0 && (is_ident_start(c) || c == '_') => {
                // raw 식별자 r#keyword
                self.bump(); // r
                self.bump(); // #
                let mut s = String::new();
                while let Some(c) = self.peek() {
                    if is_ident_continue(c) {
                        s.push(c);
                        self.bump();
                    } else {
                        break;
                    }
                }
                self.last_triggers_asi = true;
                Some(self.make(TokenKind::Ident(s), span))
            }
            _ => None,
        }
    }

    // ------------------------------------------------------------------
    // 숫자 리터럴 (§A.8.1, §A.8.2)
    // ------------------------------------------------------------------

    fn lex_number(&mut self, span: Span) -> Token {
        let mut text = String::new();

        // 16진수 / 8진수 / 2진수
        if self.peek() == Some('0') {
            let radix_char = self.peek_at(1);
            let (radix, digit_ok): (u32, fn(char) -> bool) = match radix_char {
                Some('x') | Some('X') => (16, |c: char| c.is_ascii_hexdigit()),
                Some('o') | Some('O') => (8, |c: char| ('0'..='7').contains(&c)),
                Some('b') | Some('B') => (2, |c: char| c == '0' || c == '1'),
                _ => (10, |c: char| c.is_ascii_digit()),
            };
            if radix != 10 {
                text.push(self.bump().unwrap()); // '0'
                text.push(self.bump().unwrap()); // 'x'/'o'/'b'
                let mut any_digit = false;
                while let Some(c) = self.peek() {
                    if digit_ok(c) {
                        text.push(c);
                        self.bump();
                        any_digit = true;
                    } else if c == '_' {
                        text.push(c);
                        self.bump();
                    } else {
                        break;
                    }
                }
                if !any_digit {
                    self.last_triggers_asi = false;
                    return self.make(
                        TokenKind::Error(format!("invalid numeric literal: '{text}' has no digits after radix {radix} prefix")),
                        span,
                    );
                }
                let suffix = self.try_lex_int_suffix();
                self.last_triggers_asi = true;
                return self.make(TokenKind::IntLiteral { text, suffix }, span);
            }
        }

        // 10진 정수부
        while let Some(c) = self.peek() {
            if c.is_ascii_digit() || c == '_' {
                text.push(c);
                self.bump();
            } else {
                break;
            }
        }

        // 실수부: '.' 다음에 반드시 숫자가 와야 함 (§A.8.2 — 5. 나 .5 금지, range .. 와의 모호성 회피)
        let mut is_float = false;
        if self.peek() == Some('.')
            && self.peek_at(1).map(|c| c.is_ascii_digit()).unwrap_or(false)
        {
            is_float = true;
            text.push(self.bump().unwrap()); // '.'
            while let Some(c) = self.peek() {
                if c.is_ascii_digit() || c == '_' {
                    text.push(c);
                    self.bump();
                } else {
                    break;
                }
            }
        }

        // 지수부
        if matches!(self.peek(), Some('e') | Some('E')) {
            let mut lookahead = 1;
            if matches!(self.peek_at(1), Some('+') | Some('-')) {
                lookahead = 2;
            }
            if self.peek_at(lookahead).map(|c| c.is_ascii_digit()).unwrap_or(false) {
                is_float = true;
                text.push(self.bump().unwrap()); // e/E
                if matches!(self.peek(), Some('+') | Some('-')) {
                    text.push(self.bump().unwrap());
                }
                while let Some(c) = self.peek() {
                    if c.is_ascii_digit() || c == '_' {
                        text.push(c);
                        self.bump();
                    } else {
                        break;
                    }
                }
            }
        }

        if is_float {
            let suffix = if self.peek() == Some('f') {
                let save = self.pos;
                let (save_line, save_col) = (self.line, self.col);
                self.bump();
                if self.eat('3') && self.eat('2') {
                    Some(FloatSuffix::F32)
                } else if self.eat('6') && self.eat('4') {
                    Some(FloatSuffix::F64)
                } else {
                    self.pos = save;
                    self.line = save_line;
                    self.col = save_col;
                    None
                }
            } else {
                None
            };
            self.last_triggers_asi = true;
            return self.make(TokenKind::FloatLiteral { text, suffix }, span);
        }

        // '.' 뒤에 숫자가 없으면 그냥 정수 리터럴로 끝 (다음 '.' 은 별도 토큰: 메서드
        // 호출 `5.to_string()` 또는 range `5..10` 은 파서가 처리)
        let suffix = self.try_lex_int_suffix();
        self.last_triggers_asi = true;
        self.make(TokenKind::IntLiteral { text, suffix }, span)
    }

    fn try_lex_int_suffix(&mut self) -> Option<crate::token::IntSuffix> {
        // 접미사 후보: i8,i16,i32,i64,i128,isize,u8,u16,u32,u64,u128,usize
        let start = self.pos;
        let (save_line, save_col) = (self.line, self.col);
        if !matches!(self.peek(), Some('i') | Some('u')) {
            return None;
        }
        let mut s = String::new();
        while let Some(c) = self.peek() {
            if c.is_ascii_alphanumeric() {
                s.push(c);
                self.bump();
            } else {
                break;
            }
        }
        if let Some(sfx) = int_suffix_from_str(&s) {
            Some(sfx)
        } else {
            self.pos = start;
            self.line = save_line;
            self.col = save_col;
            None
        }
    }

    // ------------------------------------------------------------------
    // 문자 리터럴 (§A.8.4)
    // ------------------------------------------------------------------

    fn lex_char(&mut self, span: Span) -> Token {
        self.bump(); // opening '
        let c = match self.peek() {
            Some('\\') => match self.read_escape() {
                Ok(ch) => ch,
                Err(msg) => {
                    self.last_triggers_asi = false;
                    return self.make(TokenKind::Error(msg), span);
                }
            },
            Some(c) => {
                self.bump();
                c
            }
            None => {
                self.last_triggers_asi = false;
                return self.make(TokenKind::Error("unterminated char literal".to_string()), span);
            }
        };
        if !self.eat('\'') {
            self.last_triggers_asi = false;
            return self.make(
                TokenKind::Error("unterminated char literal: expected closing '".to_string()),
                span,
            );
        }
        self.last_triggers_asi = true;
        self.make(TokenKind::CharLiteral(c), span)
    }

    /// `\` 로 시작하는 이스케이프 시퀀스를 읽어 문자로 변환 (§A.8.4, §A.8.5)
    fn read_escape(&mut self) -> Result<char, String> {
        self.bump(); // '\'
        let c = self.peek().ok_or_else(|| "unterminated escape sequence".to_string())?;
        match c {
            'n' => { self.bump(); Ok('\n') }
            't' => { self.bump(); Ok('\t') }
            'r' => { self.bump(); Ok('\r') }
            '\\' => { self.bump(); Ok('\\') }
            '\'' => { self.bump(); Ok('\'') }
            '"' => { self.bump(); Ok('"') }
            '0' => { self.bump(); Ok('\0') }
            '{' => { self.bump(); Ok('{') }
            '}' => { self.bump(); Ok('}') }
            'x' => {
                self.bump();
                let h1 = self.peek().filter(|c| c.is_ascii_hexdigit())
                    .ok_or_else(|| "invalid \\x escape: expected hex digit".to_string())?;
                self.bump();
                let h2 = self.peek().filter(|c| c.is_ascii_hexdigit())
                    .ok_or_else(|| "invalid \\x escape: expected two hex digits".to_string())?;
                self.bump();
                let val = u32::from_str_radix(&format!("{h1}{h2}"), 16).unwrap();
                char::from_u32(val).ok_or_else(|| format!("invalid \\x escape: 0x{val:X}"))
            }
            'u' => {
                self.bump();
                if !self.eat('{') {
                    return Err("invalid \\u escape: expected '{'".to_string());
                }
                let mut hex = String::new();
                while let Some(c) = self.peek() {
                    if c == '}' {
                        break;
                    }
                    if !c.is_ascii_hexdigit() {
                        return Err(format!("invalid \\u escape: unexpected '{c}'"));
                    }
                    hex.push(c);
                    self.bump();
                }
                if !self.eat('}') {
                    return Err("invalid \\u escape: expected closing '}'".to_string());
                }
                let val = u32::from_str_radix(&hex, 16)
                    .map_err(|_| "invalid \\u escape: bad hex digits".to_string())?;
                char::from_u32(val).ok_or_else(|| format!("bad unicode escape: \\u{{{hex}}} (0x{val:X} out of valid codepoint range)"))
            }
            other => Err(format!("invalid escape sequence: \\{other}")),
        }
    }

    // ------------------------------------------------------------------
    // 문자열 텍스트 모드 (§A.8.5) — InString
    // ------------------------------------------------------------------

    fn lex_string_text(&mut self) -> Token {
        let span = self.cur_span();
        let mut text = String::new();
        loop {
            match self.peek() {
                None => {
                    // 모드 스택에서 빠져나와야 다음 next_token() 호출이 EOF로
                    // 수렴한다 (그렇지 않으면 같은 위치에서 계속 에러만 재생산).
                    self.string_stack.pop();
                    self.last_triggers_asi = false;
                    return self.make(TokenKind::Error("unterminated string".to_string()), span);
                }
                Some('\n') => {
                    // 명세상 문자열 리터럴은 줄바꿈 전에 닫혀야 함.
                    // 개행은 소비하지 않고 모드만 Normal로 되돌려 이후
                    // 토큰화가 정상적으로 이어지도록 한다 (에러는 복구 가능해야 함, §A.12).
                    self.string_stack.pop();
                    self.last_triggers_asi = false;
                    return self.make(TokenKind::Error("unterminated string: newline before closing \"".to_string()), span);
                }
                Some('"') => {
                    self.bump();
                    self.string_stack.pop();
                    if !text.is_empty() {
                        // STRING_TEXT를 먼저 방출하고 싶지만 한 토큰만 반환 가능하므로,
                        // text가 있으면 STRING_TEXT를 반환하고 다음 호출에서 STRING_END를
                        // 내보내도록 되돌린다.
                        self.pos -= 1;
                        self.col -= 1;
                        self.string_stack.push(StringFrame { interp_depth: 0 });
                        self.last_triggers_asi = false;
                        return self.make(TokenKind::StringText(text), span);
                    }
                    self.last_triggers_asi = true; // STRING_END는 리터럴 종료 토큰, 트리거 대상
                    return self.make(TokenKind::StringEnd, span);
                }
                Some('{') => {
                    if self.peek_at(1) == Some('{') {
                        text.push('{');
                        self.bump();
                        self.bump();
                        continue;
                    }
                    if !text.is_empty() {
                        return self.make(TokenKind::StringText(text), span);
                    }
                    self.bump();
                    if let Some(f) = self.string_stack.last_mut() {
                        f.interp_depth = 1;
                    }
                    self.last_triggers_asi = false;
                    return self.make(TokenKind::InterpStart, span);
                }
                Some('}') if self.peek_at(1) == Some('}') => {
                    text.push('}');
                    self.bump();
                    self.bump();
                }
                Some('\\') => {
                    if !text.is_empty() {
                        // 이스케이프 처리 전에 지금까지의 텍스트를 우선 방출
                        return self.make(TokenKind::StringText(text), span);
                    }
                    match self.read_escape() {
                        Ok(ch) => text.push(ch),
                        Err(msg) => {
                            self.last_triggers_asi = false;
                            return self.make(TokenKind::Error(msg), span);
                        }
                    }
                }
                Some(c) => {
                    text.push(c);
                    self.bump();
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // 매크로 시길 (§A.11)
    // ------------------------------------------------------------------

    fn lex_macro_sigil(&mut self, span: Span) -> Token {
        self.bump(); // '$'
        if !self.peek().map(is_ident_start).unwrap_or(false) {
            self.last_triggers_asi = false;
            return self.make(
                TokenKind::Error("stray '$' outside macro body (or not immediately followed by an identifier)".to_string()),
                span,
            );
        }
        let mut s = String::new();
        while let Some(c) = self.peek() {
            if is_ident_continue(c) {
                s.push(c);
                self.bump();
            } else {
                break;
            }
        }
        self.last_triggers_asi = true;
        self.make(TokenKind::MacroSigil(s), span)
    }

    // ------------------------------------------------------------------
    // 연산자 / 구두점 / 구분자 (§A.9, §A.10)
    // ------------------------------------------------------------------

    fn lex_operator_or_punct(&mut self, span: Span) -> Token {
        let c = self.bump().unwrap();
        macro_rules! tok {
            ($kind:expr, $trigger:expr) => {{
                self.last_triggers_asi = $trigger;
                return self.make($kind, span);
            }};
        }
        match c {
            '+' => {
                if self.eat('=') { tok!(TokenKind::Op(Op::PlusEq), false) }
                tok!(TokenKind::Op(Op::Plus), false)
            }
            '-' => {
                if self.eat('>') { tok!(TokenKind::Op(Op::Arrow), false) }
                if self.eat('=') { tok!(TokenKind::Op(Op::MinusEq), false) }
                tok!(TokenKind::Op(Op::Minus), false)
            }
            '*' => {
                if self.eat('=') { tok!(TokenKind::Op(Op::StarEq), false) }
                tok!(TokenKind::Op(Op::Star), false)
            }
            '/' => {
                if self.eat('=') { tok!(TokenKind::Op(Op::SlashEq), false) }
                tok!(TokenKind::Op(Op::Slash), false)
            }
            '%' => {
                if self.eat('=') { tok!(TokenKind::Op(Op::PercentEq), false) }
                tok!(TokenKind::Op(Op::Percent), false)
            }
            '=' => {
                if self.eat('=') { tok!(TokenKind::Op(Op::EqEq), false) }
                if self.eat('>') { tok!(TokenKind::Op(Op::FatArrow), false) }
                tok!(TokenKind::Op(Op::Eq), false)
            }
            '!' => {
                if self.eat('=') { tok!(TokenKind::Op(Op::Ne), false) }
                tok!(TokenKind::Op(Op::Not), false)
            }
            '<' => {
                if self.eat('<') {
                    if self.eat('=') { tok!(TokenKind::Op(Op::ShlEq), false) }
                    tok!(TokenKind::Op(Op::Shl), false)
                }
                if self.eat('=') { tok!(TokenKind::Op(Op::Le), false) }
                tok!(TokenKind::Op(Op::Lt), false)
            }
            '>' => {
                if self.eat('>') {
                    if self.eat('=') { tok!(TokenKind::Op(Op::ShrEq), false) }
                    tok!(TokenKind::Op(Op::Shr), false)
                }
                if self.eat('=') { tok!(TokenKind::Op(Op::Ge), false) }
                tok!(TokenKind::Op(Op::Gt), false)
            }
            '&' => {
                if self.eat('&') { tok!(TokenKind::Op(Op::AndAnd), false) }
                if self.eat('=') { tok!(TokenKind::Op(Op::AmpEq), false) }
                tok!(TokenKind::Op(Op::Amp), false)
            }
            '|' => {
                if self.eat('|') { tok!(TokenKind::Op(Op::OrOr), false) }
                if self.eat('=') { tok!(TokenKind::Op(Op::PipeEq), false) }
                tok!(TokenKind::Op(Op::Pipe), false)
            }
            '^' => {
                if self.eat('=') { tok!(TokenKind::Op(Op::CaretEq), false) }
                tok!(TokenKind::Op(Op::Caret), false)
            }
            '~' => tok!(TokenKind::Op(Op::Tilde), false),
            '?' => tok!(TokenKind::Op(Op::Question), false),
            '.' => {
                if self.eat('.') {
                    if self.eat('=') { tok!(TokenKind::Op(Op::DotDotEq), false) }
                    tok!(TokenKind::Op(Op::DotDot), false)
                }
                tok!(TokenKind::Punct(Punct::Dot), false)
            }
            ',' => tok!(TokenKind::Punct(Punct::Comma), false),
            ';' => tok!(TokenKind::Semi { inserted: false }, false),
            ':' => {
                if self.eat(':') { tok!(TokenKind::Punct(Punct::PathSep), false) }
                tok!(TokenKind::Punct(Punct::Colon), false)
            }
            '(' => tok!(TokenKind::Punct(Punct::LParen), false),
            ')' => tok!(TokenKind::Punct(Punct::RParen), true), // 닫는 구분자, ASI 트리거
            '{' => tok!(TokenKind::Punct(Punct::LBrace), false),
            '}' => tok!(TokenKind::Punct(Punct::RBrace), true), // 닫는 구분자, ASI 트리거
            '[' => tok!(TokenKind::Punct(Punct::LBracket), false),
            ']' => tok!(TokenKind::Punct(Punct::RBracket), true), // 닫는 구분자, ASI 트리거
            '$' => unreachable!("handled earlier"),
            other => {
                self.last_triggers_asi = false;
                self.make(TokenKind::Error(format!("unrecognized character: '{other}'")), span)
            }
        }
    }
}

fn is_ident_start(c: char) -> bool {
    c == '_' || UnicodeXID::is_xid_start(c)
}

fn is_ident_continue(c: char) -> bool {
    c == '_' || UnicodeXID::is_xid_continue(c)
}
