use super::ansi::{self, Color};
use super::check_err::CheckError;
use super::parse_err::*;
use super::span_fmt;
use crate::ast::ast::{Ast, SourceID};
use crate::ast::span::*;
use crate::ast::token::Token;

pub fn err(ast: &Ast, error: CheckError, no_context: bool, id: SourceID, span: Span) {
    let error_data = error.get_data();

    ansi::set_color(Color::BoldRed);
    print!("\nerror: ");
    ansi::reset();
    println!("{}", error_data.message);

    if let Some(help) = error_data.help {
        ansi::set_color(Color::Cyan);
        print!("help:  ");
        ansi::reset();
        println!("{}", help);
    }

    if !no_context {
        let source = ast.files.get(id as usize).unwrap(); //@err internal?
        span_fmt::print(source, span, None);
    }
}

pub fn parse_err(ast: &Ast, id: SourceID, err: ParseError) {
    let source = ast.files.get(id as usize).unwrap(); //@err internal?

    ansi::set_color(Color::BoldRed);
    print!("\nparse error: ");
    ansi::reset();
    println!("in {}", err.context.as_str());

    print!("expected: ");
    for (index, token) in err.expected.iter().enumerate() {
        if index < err.expected.len() - 1 {
            print!("`{}`, ", Token::as_str(*token));
        } else {
            println!("`{}`", Token::as_str(*token));
        }
    }
    span_fmt::print(source, err.got_token.span, Some("unexpected token"));
}
