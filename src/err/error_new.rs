use crate::{
    ast::{span::Span, CompCtx, FileID},
    err::{ansi, span_fmt},
};

#[derive(Clone)]
pub struct CompError {
    pub src: SourceLoc,
    pub msg: Message,
    pub context: Vec<ErrorContext>,
}

#[derive(Clone)]
pub enum ErrorContext {
    Message { msg: Message },
    MessageSource { ctx_src: SourceLoc, msg: Message },
}

#[derive(Clone)]
pub enum Message {
    Str(&'static str),
    String(String),
}

impl CompError {
    pub fn new(src: SourceLoc, msg: Message) -> Self {
        Self {
            src,
            msg,
            context: Vec::new(),
        }
    }

    pub fn context(mut self, ctx: ErrorContext) -> Self {
        self.context.push(ctx);
        self
    }

    pub fn add_context(&mut self, ctx: ErrorContext) {
        self.context.push(ctx);
    }
}

impl Message {
    pub fn as_str(&self) -> &str {
        match self {
            Message::Str(str) => str,
            Message::String(string) => string.as_str(),
        }
    }
}

#[derive(Copy, Clone)]
pub struct SourceLoc {
    pub span: Span,
    pub file_id: FileID,
}

impl SourceLoc {
    pub fn new(span: Span, file_id: FileID) -> Self {
        Self { span, file_id }
    }
}

pub fn report_check_errors_cli(ctx: &CompCtx, errors: &[CompError]) {
    for error in errors {
        let ansi_red = ansi::Color::as_ansi_str(ansi::Color::BoldRed);
        let ansi_clear = "\x1B[0m";
        eprintln!("\n{}error:{} {}", ansi_red, ansi_clear, error.msg.as_str());
        span_fmt::print_simple(ctx.file(error.src.file_id), error.src.span, None, false);

        for context in error.context.iter() {
            match context {
                ErrorContext::Message { msg } => {
                    eprintln!("{}", msg.as_str());
                }
                ErrorContext::MessageSource { ctx_src, msg } => {
                    span_fmt::print_simple(
                        ctx.file(ctx_src.file_id),
                        ctx_src.span,
                        Some(msg.as_str()),
                        true,
                    );
                }
            }
        }
    }
}
