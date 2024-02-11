use super::span::*;
use super::token::*;

pub struct TokenList {
    tokens: Vec<Token>,
    spans: Vec<Span>,
    chars: Vec<char>,
    strings: Vec<String>,
}

#[derive(Clone, Copy)]
pub struct TokenIndex(u32);

impl TokenList {
    pub fn new(cap: usize) -> Self {
        Self {
            tokens: Vec::with_capacity(cap),
            spans: Vec::with_capacity(cap),
            chars: Vec::new(),
            strings: Vec::new(),
        }
    }

    pub fn add(&mut self, token: Token, span: Span) {
        self.tokens.push(token);
        self.spans.push(span);
    }

    pub fn add_char(&mut self, c: char, span: Span) {
        self.tokens.push(Token::CharLit);
        self.spans.push(span);
        self.chars.push(c);
    }

    pub fn add_string(&mut self, s: String, span: Span) {
        self.tokens.push(Token::StringLit);
        self.spans.push(span);
        self.strings.push(s);
    }

    pub fn token(&self, index: usize) -> Token {
        unsafe { *self.tokens.get_unchecked(index) }
    }

    pub fn span(&self, index: usize) -> Span {
        unsafe { *self.spans.get_unchecked(index) }
    }

    pub fn char(&self, index: usize) -> char {
        unsafe { *self.chars.get_unchecked(index) }
    }

    pub fn string(&self, index: usize) -> &str {
        unsafe { self.strings.get_unchecked(index) }
    }

    pub fn len(&self) -> usize {
        self.tokens.len()
    }

    pub fn cap(&self) -> usize {
        self.tokens.capacity()
    }
}

impl TokenIndex {
    pub fn new(index: usize) -> Self {
        Self(index as u32)
    }

    pub fn index(&self) -> usize {
        self.0 as usize
    }
}
