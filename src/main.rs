mod ast;
mod err;
mod llvm;
mod mem;
mod tools;

fn main() -> Result<(), ()> {
    tools::cmd::cmd_parse()
}
