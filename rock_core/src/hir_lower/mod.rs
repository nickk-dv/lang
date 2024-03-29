mod hir_build;
mod pass_1;
mod pass_2;
mod pass_3;
mod pass_4;
mod pass_5;

use crate::ast;
use crate::error::ErrorComp;
use crate::hir;
use crate::session::Session;
use hir_build::{HirData, HirEmit};

pub fn check<'hir, 'ast>(
    ast: ast::Ast<'ast>,
    session: &Session,
) -> Result<hir::Hir<'hir>, Vec<ErrorComp>> {
    let mut hir = HirData::new(ast);
    let mut emit = HirEmit::new();
    pass_1::run(&mut hir, &mut emit, session);
    pass_2::run(&mut hir, &mut emit);
    pass_3::run(&mut hir, &mut emit);
    pass_4::run(&mut hir, &mut emit);
    pass_5::run(&mut hir, &mut emit);
    emit.emit(hir)
}
