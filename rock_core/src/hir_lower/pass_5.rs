use super::hir_builder as hb;
use crate::ast;
use crate::error::ErrorComp;
use crate::hir;
use crate::intern::InternID;
use crate::text::{TextOffset, TextRange};

pub fn run(hb: &mut hb::HirBuilder) {
    for id in hb.proc_ids() {
        typecheck_proc(hb, id)
    }
}

fn typecheck_proc(hb: &mut hb::HirBuilder, id: hir::ProcID) {
    let decl = hb.proc_ast(id);
    let data = hb.proc_data(id);

    match decl.block {
        Some(block) => {
            let _ = typecheck_expr_2(
                hb,
                data.from_id,
                BlockFlags::from_root(),
                &mut ProcScope::new(id),
                data.return_ty,
                block,
            );
        }
        None => {
            //@for now having a tail directive assumes that it must be a #[c_call]
            // and this directory is only present with no block expression
            let directive_tail = decl
                .directive_tail
                .expect("directive expected with no proc block");

            let directive_name = hb.name_str(directive_tail.name.id);
            if directive_name != "c_call" {
                hb.error(
                    ErrorComp::error(format!(
                        "expected a `c_call` directive, got `{}`",
                        directive_name
                    ))
                    .context(hb.src(data.from_id, directive_tail.name.range)),
                )
            }
        }
    }
}

fn type_format(hb: &mut hb::HirBuilder, ty: hir::Type) -> String {
    match ty {
        hir::Type::Error => "error".into(),
        hir::Type::Basic(basic) => match basic {
            ast::BasicType::Unit => "()".into(),
            ast::BasicType::Bool => "bool".into(),
            ast::BasicType::S8 => "s8".into(),
            ast::BasicType::S16 => "s16".into(),
            ast::BasicType::S32 => "s32".into(),
            ast::BasicType::S64 => "s64".into(),
            ast::BasicType::Ssize => "ssize".into(),
            ast::BasicType::U8 => "u8".into(),
            ast::BasicType::U16 => "u16".into(),
            ast::BasicType::U32 => "u32".into(),
            ast::BasicType::U64 => "u64".into(),
            ast::BasicType::Usize => "usize".into(),
            ast::BasicType::F32 => "f32".into(),
            ast::BasicType::F64 => "f64".into(),
            ast::BasicType::Char => "char".into(),
            ast::BasicType::Rawptr => "rawptr".into(),
        },
        hir::Type::Enum(id) => hb.name_str(hb.enum_data(id).name.id).into(),
        hir::Type::Union(id) => hb.name_str(hb.union_data(id).name.id).into(),
        hir::Type::Struct(id) => hb.name_str(hb.struct_data(id).name.id).into(),
        hir::Type::Reference(ref_ty, mutt) => {
            let mut_str = match mutt {
                ast::Mut::Mutable => "mut ",
                ast::Mut::Immutable => "",
            };
            format!("&{}{}", mut_str, type_format(hb, *ref_ty))
        }
        hir::Type::ArraySlice(slice) => {
            let mut_str = match slice.mutt {
                ast::Mut::Mutable => "mut",
                ast::Mut::Immutable => "",
            };
            format!("[{}]{}", mut_str, type_format(hb, slice.ty))
        }
        hir::Type::ArrayStatic(array) => format!("[<SIZE>]{}", type_format(hb, array.ty)),
        hir::Type::ArrayStaticDecl(array) => format!("[<SIZE>]{}", type_format(hb, array.ty)),
    }
}

#[derive(Copy, Clone)]
struct BlockFlags {
    in_loop: bool,
    in_defer: bool,
}

impl BlockFlags {
    fn from_root() -> BlockFlags {
        BlockFlags {
            in_loop: false,
            in_defer: false,
        }
    }

    fn enter_defer(self) -> BlockFlags {
        BlockFlags {
            in_loop: self.in_loop,
            in_defer: true,
        }
    }

    fn enter_loop(self) -> BlockFlags {
        BlockFlags {
            in_loop: true,
            in_defer: self.in_defer,
        }
    }
}

#[derive(Copy, Clone)]
enum VariableID {
    Local(hir::LocalID),
    Param(hir::ProcParamID),
}

//@locals in scope needs to be popped
// on stack exit
struct ProcScope<'hir> {
    proc_id: hir::ProcID,
    locals: Vec<&'hir hir::VarDecl<'hir>>,
    locals_in_scope: Vec<hir::LocalID>,
}

impl<'hir> ProcScope<'hir> {
    fn new(proc_id: hir::ProcID) -> ProcScope<'hir> {
        ProcScope {
            proc_id,
            locals: Vec::new(),
            locals_in_scope: Vec::new(),
        }
    }

    fn get_local(&self, id: hir::LocalID) -> &'hir hir::VarDecl<'hir> {
        self.locals.get(id.index()).unwrap()
    }

    fn get_param(
        &self,
        hb: &hb::HirBuilder<'_, '_, 'hir>,
        id: hir::ProcParamID,
    ) -> &'hir hir::ProcParam<'hir> {
        let data = hb.proc_data(self.proc_id);
        data.params.get(id.index()).unwrap()
    }

    fn push_local(&mut self, var_decl: &'hir hir::VarDecl<'hir>) {
        let id = hir::LocalID::new(self.locals.len());
        self.locals.push(var_decl);
        self.locals_in_scope.push(id);
    }

    fn find_variable(&self, hb: &hb::HirBuilder, id: InternID) -> Option<VariableID> {
        let data = hb.proc_data(self.proc_id);
        for (idx, param) in data.params.iter().enumerate() {
            if param.name.id == id {
                let id = hir::ProcParamID::new(idx);
                return Some(VariableID::Param(id));
            }
        }
        for local_id in self.locals_in_scope.iter().cloned() {
            if self.get_local(local_id).name.id == id {
                return Some(VariableID::Local(local_id));
            }
        }
        None
    }
}

struct TypeResult<'hir> {
    ty: hir::Type<'hir>,
    expr: &'hir hir::Expr<'hir>,
}

impl<'hir> TypeResult<'hir> {
    fn new(ty: hir::Type<'hir>, expr: &'hir hir::Expr<'hir>) -> TypeResult<'hir> {
        TypeResult { ty, expr }
    }
}

pub fn type_matches<'hir>(ty: hir::Type<'hir>, ty2: hir::Type<'hir>) -> bool {
    match (ty, ty2) {
        (hir::Type::Error, ..) => true,
        (.., hir::Type::Error) => true,
        (hir::Type::Basic(basic), hir::Type::Basic(basic2)) => basic == basic2,
        (hir::Type::Enum(id), hir::Type::Enum(id2)) => id == id2,
        (hir::Type::Union(id), hir::Type::Union(id2)) => id == id2,
        (hir::Type::Struct(id), hir::Type::Struct(id2)) => id == id2,
        (hir::Type::Reference(ref_ty, mutt), hir::Type::Reference(ref_ty2, mutt2)) => {
            mutt == mutt2 && type_matches(*ref_ty, *ref_ty2)
        }
        (hir::Type::ArraySlice(slice), hir::Type::ArraySlice(slice2)) => {
            slice.mutt == slice2.mutt && type_matches(slice.ty, slice2.ty)
        }
        //@division of 2 kinds of static sized arrays
        // makes this eq check totally incorrect, for now
        // or theres needs to be a 4 cases to compare them all
        (hir::Type::ArrayStatic(array), hir::Type::ArrayStatic(array2)) => {
            //@size const_expr is ignored
            type_matches(array.ty, array2.ty)
        }
        (hir::Type::ArrayStaticDecl(array), hir::Type::ArrayStaticDecl(array2)) => {
            //@size const_expr is ignored
            type_matches(array.ty, array2.ty)
        }
        _ => false,
    }
}

//@need type_repr instead of allocating hir types
// and maybe type::unknown, to facilitate better inference
// to better represent partially typed arrays, etc
#[must_use]
fn typecheck_expr_2<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    block_flags: BlockFlags,
    proc_scope: &mut ProcScope<'hir>,
    expect_ty: hir::Type<'hir>,
    expr: &'ast ast::Expr<'ast>,
) -> TypeResult<'hir> {
    let type_result = match expr.kind {
        ast::ExprKind::Unit => typecheck_unit(hb),
        ast::ExprKind::LitNull => typecheck_lit_null(hb),
        ast::ExprKind::LitBool { val } => typecheck_lit_bool(hb, val),
        ast::ExprKind::LitInt { val } => typecheck_lit_int(hb, expect_ty, val),
        ast::ExprKind::LitFloat { val } => typecheck_lit_float(hb, expect_ty, val),
        ast::ExprKind::LitChar { val } => typecheck_lit_char(hb, val),
        ast::ExprKind::LitString { id } => typecheck_lit_string(hb, id),
        ast::ExprKind::If { if_ } => {
            typecheck_if(hb, origin_id, block_flags, proc_scope, expect_ty, if_)
        }
        ast::ExprKind::Block { stmts } => {
            typecheck_block(hb, origin_id, block_flags, proc_scope, expect_ty, stmts)
        }
        ast::ExprKind::Match { match_ } => {
            typecheck_match(hb, origin_id, block_flags, proc_scope, expect_ty, match_)
        }
        ast::ExprKind::Field { target, name } => {
            typecheck_field(hb, origin_id, block_flags, proc_scope, target, name)
        }
        ast::ExprKind::Index { target, index } => {
            typecheck_index(hb, origin_id, block_flags, proc_scope, target, index)
        }
        ast::ExprKind::Cast { target, ty } => typecheck_cast(
            hb,
            origin_id,
            block_flags,
            proc_scope,
            target,
            ty,
            expr.range,
        ),
        ast::ExprKind::Sizeof { ty } => typecheck_placeholder(hb),
        ast::ExprKind::Item { path } => typecheck_placeholder(hb),
        ast::ExprKind::ProcCall { proc_call } => typecheck_placeholder(hb),
        ast::ExprKind::StructInit { struct_init } => typecheck_placeholder(hb),
        ast::ExprKind::ArrayInit { input } => typecheck_placeholder(hb),
        ast::ExprKind::ArrayRepeat { expr, size } => typecheck_placeholder(hb),
        ast::ExprKind::UnaryExpr { op, rhs } => typecheck_placeholder(hb),
        ast::ExprKind::BinaryExpr { op, lhs, rhs } => typecheck_placeholder(hb),
    };

    if !type_matches(expect_ty, type_result.ty) {
        let msg: String = format!(
            "type mismatch: expected `{}`, found `{}`",
            type_format(hb, expect_ty),
            type_format(hb, type_result.ty)
        );
        hb.error(ErrorComp::error(msg).context(hb.src(origin_id, expr.range)));
    }

    type_result
}

fn typecheck_placeholder<'ast, 'hir>(hb: &mut hb::HirBuilder<'_, 'ast, 'hir>) -> TypeResult<'hir> {
    TypeResult::new(hir::Type::Error, hb.arena().alloc(hir::Expr::Error))
}

fn typecheck_unit<'ast, 'hir>(hb: &mut hb::HirBuilder<'_, 'ast, 'hir>) -> TypeResult<'hir> {
    TypeResult::new(
        hir::Type::Basic(ast::BasicType::Unit),
        hb.arena().alloc(hir::Expr::Unit),
    )
}

fn typecheck_lit_null<'ast, 'hir>(hb: &mut hb::HirBuilder<'_, 'ast, 'hir>) -> TypeResult<'hir> {
    TypeResult::new(
        hir::Type::Basic(ast::BasicType::Rawptr),
        hb.arena().alloc(hir::Expr::LitNull),
    )
}

fn typecheck_lit_bool<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    val: bool,
) -> TypeResult<'hir> {
    TypeResult::new(
        hir::Type::Basic(ast::BasicType::Bool),
        hb.arena().alloc(hir::Expr::LitBool { val }),
    )
}

fn typecheck_lit_int<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    expect_ty: hir::Type<'hir>,
    val: u64,
) -> TypeResult<'hir> {
    const DEFAULT_INT_TYPE: ast::BasicType = ast::BasicType::S32;

    let lit_type = match expect_ty {
        hir::Type::Basic(expect) => match expect {
            ast::BasicType::Unit => DEFAULT_INT_TYPE,
            ast::BasicType::Bool => DEFAULT_INT_TYPE,
            ast::BasicType::S8
            | ast::BasicType::S16
            | ast::BasicType::S32
            | ast::BasicType::S64
            | ast::BasicType::Ssize
            | ast::BasicType::U8
            | ast::BasicType::U16
            | ast::BasicType::U32
            | ast::BasicType::U64
            | ast::BasicType::Usize => expect,
            ast::BasicType::F32 => DEFAULT_INT_TYPE,
            ast::BasicType::F64 => DEFAULT_INT_TYPE,
            ast::BasicType::Char => DEFAULT_INT_TYPE,
            ast::BasicType::Rawptr => DEFAULT_INT_TYPE,
        },
        _ => DEFAULT_INT_TYPE,
    };

    TypeResult::new(
        hir::Type::Basic(lit_type),
        hb.arena().alloc(hir::Expr::LitInt { val, ty: lit_type }),
    )
}

fn typecheck_lit_float<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    expect_ty: hir::Type<'hir>,
    val: f64,
) -> TypeResult<'hir> {
    const DEFAULT_FLOAT_TYPE: ast::BasicType = ast::BasicType::F64;

    let lit_type = match expect_ty {
        hir::Type::Basic(expect) => match expect {
            ast::BasicType::Unit => DEFAULT_FLOAT_TYPE,
            ast::BasicType::Bool => DEFAULT_FLOAT_TYPE,
            ast::BasicType::S8 => DEFAULT_FLOAT_TYPE,
            ast::BasicType::S16 => DEFAULT_FLOAT_TYPE,
            ast::BasicType::S32 => DEFAULT_FLOAT_TYPE,
            ast::BasicType::S64 => DEFAULT_FLOAT_TYPE,
            ast::BasicType::Ssize => DEFAULT_FLOAT_TYPE,
            ast::BasicType::U8 => DEFAULT_FLOAT_TYPE,
            ast::BasicType::U16 => DEFAULT_FLOAT_TYPE,
            ast::BasicType::U32 => DEFAULT_FLOAT_TYPE,
            ast::BasicType::U64 => DEFAULT_FLOAT_TYPE,
            ast::BasicType::Usize => DEFAULT_FLOAT_TYPE,
            ast::BasicType::F32 | ast::BasicType::F64 => expect,
            ast::BasicType::Char => DEFAULT_FLOAT_TYPE,
            ast::BasicType::Rawptr => DEFAULT_FLOAT_TYPE,
        },
        _ => DEFAULT_FLOAT_TYPE,
    };

    TypeResult::new(
        hir::Type::Basic(lit_type),
        hb.arena().alloc(hir::Expr::LitFloat { val, ty: lit_type }),
    )
}

fn typecheck_lit_char<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    val: char,
) -> TypeResult<'hir> {
    TypeResult::new(
        hir::Type::Basic(ast::BasicType::Char),
        hb.arena().alloc(hir::Expr::LitChar { val }),
    )
}

fn typecheck_lit_string<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    id: InternID,
) -> TypeResult<'hir> {
    let slice = hb.arena().alloc(hir::ArraySlice {
        mutt: ast::Mut::Immutable,
        ty: hir::Type::Basic(ast::BasicType::U8),
    });

    TypeResult::new(
        hir::Type::ArraySlice(slice),
        hb.arena().alloc(hir::Expr::LitString { id }),
    )
}

fn typecheck_if<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    block_flags: BlockFlags,
    proc_scope: &mut ProcScope<'hir>,
    expect_ty: hir::Type<'hir>,
    if_: &'ast ast::If<'ast>,
) -> TypeResult<'hir> {
    //@linearize the ast::If and hir repr of the if else chain
    // need to know if its closed or open without recursion
    // and parsing with the loop would be better
    typecheck_placeholder(hb)
}

fn typecheck_match<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    block_flags: BlockFlags,
    proc_scope: &mut ProcScope<'hir>,
    expect_ty: hir::Type<'hir>,
    match_: &'ast ast::Match<'ast>,
) -> TypeResult<'hir> {
    let on_res = typecheck_expr_2(
        hb,
        origin_id,
        block_flags,
        proc_scope,
        hir::Type::Error, // no expectation
        match_.on_expr,
    );
    for arm in match_.arms {
        if let Some(pat) = arm.pat {
            let pat_res = typecheck_expr_2(hb, origin_id, block_flags, proc_scope, on_res.ty, pat);
        }
        //@check match arm expr
    }
    typecheck_placeholder(hb)
}

fn typecheck_field<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    block_flags: BlockFlags,
    proc_scope: &mut ProcScope<'hir>,
    target: &'ast ast::Expr,
    name: ast::Ident,
) -> TypeResult<'hir> {
    let target_res = typecheck_expr_2(
        hb,
        origin_id,
        block_flags,
        proc_scope,
        hir::Type::Error, // no expectation
        target,
    );

    let (field_ty, kind) = match target_res.ty {
        hir::Type::Reference(ref_ty, mutt) => verify_type_field(hb, origin_id, *ref_ty, name),
        _ => verify_type_field(hb, origin_id, target_res.ty, name),
    };

    match kind {
        FieldExprKind::None => {
            TypeResult::new(hir::Type::Error, hb.arena().alloc(hir::Expr::Error))
        }
        FieldExprKind::Member(id) => TypeResult::new(
            field_ty,
            hb.arena().alloc(hir::Expr::UnionMember {
                target: target_res.expr,
                id,
            }),
        ),
        FieldExprKind::Field(id) => TypeResult::new(
            field_ty,
            hb.arena().alloc(hir::Expr::StructField {
                target: target_res.expr,
                id,
            }),
        ),
    }
}

enum FieldExprKind {
    None,
    Member(hir::UnionMemberID),
    Field(hir::StructFieldID),
}

fn verify_type_field<'hir>(
    hb: &mut hb::HirBuilder<'_, '_, 'hir>,
    origin_id: hir::ScopeID,
    ty: hir::Type<'hir>,
    name: ast::Ident,
) -> (hir::Type<'hir>, FieldExprKind) {
    match ty {
        hir::Type::Error => (hir::Type::Error, FieldExprKind::None),
        hir::Type::Union(id) => {
            let data = hb.union_data(id);
            let find = data.members.iter().enumerate().find_map(|(id, member)| {
                (member.name.id == name.id).then(|| (hir::UnionMemberID::new(id), member))
            });
            match find {
                Some((id, member)) => (member.ty, FieldExprKind::Member(id)),
                _ => {
                    hb.error(
                        ErrorComp::error(format!(
                            "no field `{}` exists on union type `{}`",
                            hb.name_str(name.id),
                            hb.name_str(data.name.id),
                        ))
                        .context(hb.src(origin_id, name.range)),
                    );
                    (hir::Type::Error, FieldExprKind::None)
                }
            }
        }
        hir::Type::Struct(id) => {
            let data = hb.struct_data(id);
            let find = data.fields.iter().enumerate().find_map(|(id, field)| {
                (field.name.id == name.id).then(|| (hir::StructFieldID::new(id), field))
            });
            match find {
                Some((id, field)) => (field.ty, FieldExprKind::Field(id)),
                _ => {
                    hb.error(
                        ErrorComp::error(format!(
                            "no field `{}` exists on struct type `{}`",
                            hb.name_str(name.id),
                            hb.name_str(data.name.id),
                        ))
                        .context(hb.src(origin_id, name.range)),
                    );
                    (hir::Type::Error, FieldExprKind::None)
                }
            }
        }
        _ => {
            let ty_format = type_format(hb, ty);
            hb.error(
                ErrorComp::error(format!(
                    "no field `{}` exists on value of type {}",
                    hb.name_str(name.id),
                    ty_format,
                ))
                .context(hb.src(origin_id, name.range)),
            );
            (hir::Type::Error, FieldExprKind::None)
        }
    }
}

fn typecheck_index<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    block_flags: BlockFlags,
    proc_scope: &mut ProcScope<'hir>,
    target: &'ast ast::Expr<'ast>,
    index: &'ast ast::Expr<'ast>,
) -> TypeResult<'hir> {
    let target_res = typecheck_expr_2(
        hb,
        origin_id,
        block_flags,
        proc_scope,
        hir::Type::Error, // no expectation
        target,
    );
    let index_res = typecheck_expr_2(
        hb,
        origin_id,
        block_flags,
        proc_scope,
        hir::Type::Basic(ast::BasicType::Usize),
        index,
    );

    let elem_ty = match target_res.ty {
        hir::Type::Reference(ref_ty, mutt) => verify_elem_type(*ref_ty),
        _ => verify_elem_type(target_res.ty),
    };

    match elem_ty {
        Some(it) => {
            let hir_expr = hb.arena().alloc(hir::Expr::Index {
                target: target_res.expr,
                index: index_res.expr,
            });
            TypeResult::new(it, hir_expr)
        }
        None => {
            let ty_format = type_format(hb, target_res.ty);
            hb.error(
                ErrorComp::error(format!("cannot index value of type {}", ty_format))
                    .context(hb.src(origin_id, index.range)),
            );
            TypeResult::new(hir::Type::Error, hb.arena().alloc(hir::Expr::Error))
        }
    }
}

fn verify_elem_type(ty: hir::Type) -> Option<hir::Type> {
    match ty {
        hir::Type::Error => Some(hir::Type::Error),
        hir::Type::ArraySlice(slice) => Some(slice.ty),
        hir::Type::ArrayStatic(array) => Some(array.ty),
        hir::Type::ArrayStaticDecl(array) => Some(array.ty),
        _ => None,
    }
}

fn typecheck_cast<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    block_flags: BlockFlags,
    proc_scope: &mut ProcScope<'hir>,
    target: &'ast ast::Expr<'ast>,
    ty: &'ast ast::Type<'ast>,
    cast_range: TextRange,
) -> TypeResult<'hir> {
    let target_res = typecheck_expr_2(
        hb,
        origin_id,
        block_flags,
        proc_scope,
        hir::Type::Error, // no expectation
        target,
    );
    let cast_ty = super::pass_3::resolve_decl_type(hb, origin_id, *ty, true);

    match (target_res.ty, cast_ty) {
        (hir::Type::Error, ..) => {}
        (.., hir::Type::Error) => {}
        (hir::Type::Basic(from), hir::Type::Basic(into)) => {
            //@verify that from into pair is valid
            // determine type of the cast, according to llvm, e.g: fp_trunc, fp_to_int etc.
        }
        _ => {
            let from_format = type_format(hb, target_res.ty);
            let into_format = type_format(hb, cast_ty);
            hb.error(
                ErrorComp::error(format!(
                    "non privitive cast from `{from_format}` into `{into_format}`",
                ))
                .context(hb.src(origin_id, cast_range)),
            );
        }
    }

    let hir_ty = hb.arena().alloc(cast_ty);
    TypeResult {
        ty: cast_ty,
        expr: hb.arena().alloc(hir::Expr::Cast {
            target: target_res.expr,
            ty: hir_ty,
        }),
    }
}

fn typecheck_block<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    block_flags: BlockFlags,
    proc_scope: &mut ProcScope<'hir>,
    expect_ty: hir::Type<'hir>,
    stmts: &'ast [ast::Stmt<'ast>],
) -> TypeResult<'hir> {
    for stmt in stmts {
        match stmt.kind {
            ast::StmtKind::Break => typecheck_stmt_break(hb, origin_id, block_flags, stmt.range),
            ast::StmtKind::Continue => {
                typecheck_stmt_continue(hb, origin_id, block_flags, stmt.range);
            }
            ast::StmtKind::Return(ret_expr) => {}
            ast::StmtKind::Defer(block) => typecheck_stmt_defer(
                hb,
                origin_id,
                block_flags,
                proc_scope,
                stmt.range.start(),
                block,
            ),
            ast::StmtKind::ForLoop(for_) => {}
            ast::StmtKind::VarDecl(var_decl) => {}
            ast::StmtKind::VarAssign(var_assign) => {}
            ast::StmtKind::ExprSemi(expr) => {
                let _ = typecheck_expr_2(
                    hb,
                    origin_id,
                    block_flags,
                    proc_scope,
                    hir::Type::Error, // no expectation
                    expr,
                );
            }
            ast::StmtKind::ExprTail(expr) => {
                let _ = typecheck_expr_2(hb, origin_id, block_flags, proc_scope, expect_ty, expr);
            }
        }
    }

    TypeResult::new(
        hir::Type::Basic(ast::BasicType::Unit),
        hb.arena().alloc(hir::Expr::Unit),
    )
}

fn typecheck_stmt_break<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    block_flags: BlockFlags,
    stmt_range: TextRange,
) {
    if !block_flags.in_loop {
        hb.error(
            ErrorComp::error("cannot use `break` outside of a loop")
                .context(hb.src(origin_id, stmt_range)),
        );
    }
}

fn typecheck_stmt_continue<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    block_flags: BlockFlags,
    stmt_range: TextRange,
) {
    if !block_flags.in_loop {
        hb.error(
            ErrorComp::error("cannot use `continue` outside of a loop")
                .context(hb.src(origin_id, stmt_range)),
        );
    }
}

//@allow break and continue from loops that originated within defer itself
// this can probably be done via resetting the in_loop when entering defer block
fn typecheck_stmt_defer<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    block_flags: BlockFlags,
    proc_scope: &mut ProcScope<'hir>,
    stmt_start: TextOffset,
    block: &'ast ast::Expr<'ast>,
) {
    if block_flags.in_defer {
        hb.error(
            ErrorComp::error("`defer` statement cannot be nested")
                .context(hb.src(origin_id, TextRange::new(stmt_start, stmt_start + 5.into()))),
        );
    }
    let _ = typecheck_expr_2(
        hb,
        origin_id,
        block_flags.enter_defer(),
        proc_scope,
        hir::Type::Basic(ast::BasicType::Unit),
        block,
    );
}

//@better idea would be to return type repr that is not allocated via arena
// and will be fast to construct and compare
#[must_use]
fn typecheck_expr<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    block_flags: BlockFlags,
    checked_expr: &ast::Expr<'ast>,
    locals: &mut ProcScope<'hir>,
) -> hir::Type<'hir> {
    match checked_expr.kind {
        ast::ExprKind::Unit => hir::Type::Basic(ast::BasicType::Unit),
        ast::ExprKind::LitNull => hir::Type::Basic(ast::BasicType::Rawptr),
        ast::ExprKind::LitBool { val } => hir::Type::Basic(ast::BasicType::Bool),
        ast::ExprKind::LitInt { val } => hir::Type::Basic(ast::BasicType::S32),
        ast::ExprKind::LitFloat { val } => hir::Type::Basic(ast::BasicType::F64),
        ast::ExprKind::LitChar { val } => hir::Type::Basic(ast::BasicType::Char),
        ast::ExprKind::LitString { id } => {
            let slice = hb.arena().alloc(hir::ArraySlice {
                mutt: ast::Mut::Immutable,
                ty: hir::Type::Basic(ast::BasicType::U8),
            });
            hir::Type::ArraySlice(slice)
        }
        ast::ExprKind::If { if_ } => {
            //if if_.is_empty() {
            //    hir::Type::Basic(ast::BasicType::Unit)
            //} else {
            //    for arm in if_ {
            //        if let Some(cond) = arm.cond {
            //            let _ = typecheck_expr(hb, origin_id, block_flags, cond, locals);
            //            //@expect bool
            //        }
            //        let _ = typecheck_expr(hb, origin_id, block_flags, arm.expr, locals);
            //    }
            //    typecheck_todo(hb, origin_id, checked_expr)
            //}
            typecheck_todo(hb, origin_id, checked_expr)
        }
        ast::ExprKind::Block { stmts } => {
            for (idx, stmt) in stmts.iter().enumerate() {
                let last = idx + 1 == stmts.len();
                let stmt_ty = match stmt.kind {
                    ast::StmtKind::Break => {
                        if !block_flags.in_loop {
                            hb.error(
                                ErrorComp::error("cannot use `break` outside of a loop")
                                    .context(hb.src(origin_id, stmt.range)),
                            );
                        }
                        hir::Type::Basic(ast::BasicType::Unit)
                    }
                    ast::StmtKind::Continue => {
                        if !block_flags.in_loop {
                            hb.error(
                                ErrorComp::error("cannot use `continue` outside of a loop")
                                    .context(hb.src(origin_id, stmt.range)),
                            );
                        }
                        hir::Type::Basic(ast::BasicType::Unit)
                    }
                    ast::StmtKind::Return(expr) => {
                        if block_flags.in_defer {
                            let range =
                                TextRange::new(stmt.range.start(), stmt.range.start() + 6.into());
                            hb.error(
                                ErrorComp::error("cannot use `return` inside the defer block")
                                    .context(hb.src(origin_id, range)),
                            );
                        }
                        if let Some(expr) = expr {
                            let _ = typecheck_expr(
                                hb,
                                origin_id,
                                block_flags.enter_defer(),
                                expr,
                                locals,
                            );
                        }
                        hir::Type::Basic(ast::BasicType::Unit)
                    }
                    ast::StmtKind::Defer(defer) => {
                        if block_flags.in_defer {
                            let range =
                                TextRange::new(stmt.range.start(), stmt.range.start() + 5.into());
                            hb.error(
                                ErrorComp::error("`defer` statement cannot be nested")
                                    .context(hb.src(origin_id, range)),
                            );
                        }
                        typecheck_expr(hb, origin_id, block_flags.enter_defer(), defer, locals)
                    }
                    ast::StmtKind::ForLoop(for_) => {
                        //@check and add for loop variables to the corect scope
                        typecheck_expr(hb, origin_id, block_flags.enter_loop(), for_.block, locals)
                    }
                    ast::StmtKind::VarDecl(var_decl) => {
                        let duplicate = match hb.scope_name_defined(origin_id, var_decl.name.id) {
                            Some(existing) => {
                                super::pass_1::name_already_defined_error(
                                    hb,
                                    origin_id,
                                    var_decl.name,
                                    existing,
                                );
                                true
                            }
                            None => {
                                let var = locals.find_variable(hb, var_decl.name.id);

                                let existing = match var {
                                    Some(VariableID::Local(id)) => {
                                        Some(locals.get_local(id).name.range)
                                    }
                                    Some(VariableID::Param(id)) => {
                                        Some(locals.get_param(hb, id).name.range)
                                    }
                                    None => None,
                                };

                                match existing {
                                    Some(existing) => {
                                        hb.error(
                                            ErrorComp::error(format!(
                                                "local `{}` is already defined",
                                                hb.name_str(var_decl.name.id)
                                            ))
                                            .context(hb.src(origin_id, var_decl.name.range))
                                            .context_info(
                                                "defined here",
                                                hb.src(origin_id, existing),
                                            ),
                                        );
                                        true
                                    }
                                    None => false,
                                }
                            }
                        };

                        let ty = var_decl
                            .ty
                            .map(|ast_ty| {
                                super::pass_3::resolve_decl_type(hb, origin_id, ast_ty, true)
                            })
                            .unwrap_or(hir::Type::Error);
                        let expr = var_decl.expr.map(|ast_expr| {
                            let _ = typecheck_expr(hb, origin_id, block_flags, ast_expr, locals);
                            hb.arena().alloc(hir::Expr::Error)
                        });

                        if !duplicate {
                            let var_decl = hb.arena().alloc(hir::VarDecl {
                                mutt: var_decl.mutt,
                                name: var_decl.name,
                                ty,
                                expr,
                            });
                            locals.push_local(var_decl);
                        }

                        hir::Type::Basic(ast::BasicType::Unit)
                    }
                    ast::StmtKind::VarAssign(_) => hir::Type::Basic(ast::BasicType::Unit),
                    ast::StmtKind::ExprSemi(expr) => {
                        typecheck_expr(hb, origin_id, block_flags, expr, locals)
                    }
                    ast::StmtKind::ExprTail(expr) => {
                        typecheck_expr(hb, origin_id, block_flags, expr, locals)
                    }
                };
                if last {
                    return stmt_ty;
                }
            }
            hir::Type::Basic(ast::BasicType::Unit)
        }
        ast::ExprKind::Match { match_ } => {
            let _ = typecheck_expr(hb, origin_id, block_flags, match_.on_expr, locals); // @only enums and integers and bools are allowed (like switch expr)
            for arm in match_.arms {
                if let Some(pat) = arm.pat {
                    let _ = typecheck_expr(hb, origin_id, block_flags, pat, locals);
                    //@expect same type as being matched on (enum -> enum, int -> int)
                }
                let _ = typecheck_expr(hb, origin_id, block_flags, arm.expr, locals);
            }
            typecheck_todo(hb, origin_id, checked_expr)
        }
        ast::ExprKind::Field { target, name } => {
            //@allowing only single reference access of the field, no automatic derefencing is done automatically
            let ty = typecheck_expr(hb, origin_id, block_flags, target, locals);
            match ty {
                hir::Type::Reference(ref_ty, mutt) => check_field_ty(hb, origin_id, *ref_ty, name),
                _ => check_field_ty(hb, origin_id, ty, name),
            }
        }
        ast::ExprKind::Index { target, index } => {
            //@allowing only single reference access of the field, no automatic derefencing is done automatically
            let ty = typecheck_expr(hb, origin_id, block_flags, target, locals);
            let _ = typecheck_expr(hb, origin_id, block_flags, index, locals); //@expect usize
            match ty {
                hir::Type::Reference(ref_ty, mutt) => {
                    check_index_ty(hb, origin_id, *ref_ty, index.range)
                }
                _ => check_index_ty(hb, origin_id, ty, index.range),
            }
        }
        ast::ExprKind::Cast { target, ty } => typecheck_todo(hb, origin_id, checked_expr),
        ast::ExprKind::Sizeof { ty } => {
            //@check ast type
            //@this can be promoted to a constant expression
            // but no alignment or size resolution is done so far on checking phase
            hir::Type::Basic(ast::BasicType::Usize)
        }
        ast::ExprKind::Item { path } => {
            //@nameresolve item path
            let target_id = path_resolve_target_scope(hb, origin_id, path);
            typecheck_todo(hb, origin_id, checked_expr)
        }
        ast::ExprKind::ProcCall { proc_call } => {
            //@nameresolve proc_call.path
            let target_id = path_resolve_target_scope(hb, origin_id, proc_call.path);
            for &expr in proc_call.input {
                let _ = typecheck_expr(hb, origin_id, block_flags, expr, locals);
            }
            typecheck_todo(hb, origin_id, checked_expr)
        }
        ast::ExprKind::StructInit { struct_init } => {
            //@nameresolve struct_init.path
            let target_id = path_resolve_target_scope(hb, origin_id, struct_init.path);

            // @first find if field name exists, only then handle
            // name and expr or just a name
            for init in struct_init.input {
                match init.expr {
                    Some(expr) => {
                        //@field name and the expression
                        let _ = typecheck_expr(hb, origin_id, block_flags, expr, locals);
                    }
                    None => {
                        //@field name that must match some named value in scope
                    }
                }
            }
            typecheck_todo(hb, origin_id, checked_expr)
        }
        ast::ExprKind::ArrayInit { input } => {
            //@expected type should make empty array to be of that type
            // since type of empty array literal is anything otherwise
            // if we work with non hir types directly representing the Unknown type is possible
            // and mutation is fine

            // rust example:
            // let empty: [unknown; 0] = [];
            // let slice: &[u32] = &empty; //now array type is known

            //@try to design a better method that might check a procedure body
            // without starting to produce a new hir right-away
            // since the series of Hir::Expr are not usefull if this code
            // wont be compiled any further, we dont need to allocate anything in that case

            let mut elem_ty = hir::Type::Error;
            for (idx, &expr) in input.iter().enumerate() {
                let ty = typecheck_expr(hb, origin_id, block_flags, expr, locals);
                if idx == 0 {
                    elem_ty = ty;
                }
            }
            let size = hb.arena().alloc(hir::Expr::LitInt {
                val: input.len() as u64,
                ty: ast::BasicType::Usize,
            });
            let array = hb.arena().alloc(hir::ArrayStatic { size, ty: elem_ty });
            hir::Type::ArrayStatic(array)
        }
        ast::ExprKind::ArrayRepeat { expr, size } => typecheck_todo(hb, origin_id, checked_expr),
        ast::ExprKind::UnaryExpr { op, rhs } => {
            let rhs = typecheck_expr(hb, origin_id, block_flags, rhs, locals);
            typecheck_todo(hb, origin_id, checked_expr)
        }
        ast::ExprKind::BinaryExpr { op, lhs, rhs } => {
            let lhs = typecheck_expr(hb, origin_id, block_flags, lhs, locals);
            let rhs = typecheck_expr(hb, origin_id, block_flags, rhs, locals);
            typecheck_todo(hb, origin_id, checked_expr)
        }
    }
}

fn typecheck_todo<'hir>(
    hb: &mut hb::HirBuilder,
    from_id: hir::ScopeID,
    checked_expr: &ast::Expr,
) -> hir::Type<'hir> {
    hb.error(
        ErrorComp::warning("this expression is not yet typechecked")
            .context(hb.src(from_id, checked_expr.range)),
    );
    hir::Type::Error
}

fn check_field_ty<'hir>(
    hb: &mut hb::HirBuilder<'_, '_, 'hir>,
    from_id: hir::ScopeID,
    ty: hir::Type<'hir>,
    name: ast::Ident,
) -> hir::Type<'hir> {
    match ty {
        hir::Type::Error => hir::Type::Error,
        hir::Type::Union(id) => {
            let data = hb.union_data(id);
            let find = data
                .members
                .iter()
                .enumerate()
                .find_map(|(id, member)| (member.name.id == name.id).then(|| member));
            match find {
                Some(member) => member.ty,
                _ => {
                    hb.error(
                        ErrorComp::error(format!(
                            "no field `{}` exists on union type `{}`",
                            hb.name_str(name.id),
                            hb.name_str(data.name.id),
                        ))
                        .context(hb.src(from_id, name.range)),
                    );
                    hir::Type::Error
                }
            }
        }
        hir::Type::Struct(id) => {
            let data = hb.struct_data(id);
            let find = data
                .fields
                .iter()
                .enumerate()
                .find_map(|(id, field)| (field.name.id == name.id).then(|| field));
            match find {
                Some(field) => field.ty,
                _ => {
                    hb.error(
                        ErrorComp::error(format!(
                            "no field `{}` exists on struct type `{}`",
                            hb.name_str(name.id),
                            hb.name_str(data.name.id),
                        ))
                        .context(hb.src(from_id, name.range)),
                    );
                    hir::Type::Error
                }
            }
        }
        _ => {
            let ty_format = type_format(hb, ty);
            hb.error(
                ErrorComp::error(format!(
                    "no field `{}` exists on value of type {}",
                    hb.name_str(name.id),
                    ty_format,
                ))
                .context(hb.src(from_id, name.range)),
            );
            hir::Type::Error
        }
    }
}

fn check_index_ty<'hir>(
    hb: &mut hb::HirBuilder<'_, '_, 'hir>,
    from_id: hir::ScopeID,
    ty: hir::Type<'hir>,
    index_range: TextRange,
) -> hir::Type<'hir> {
    match ty {
        hir::Type::Error => hir::Type::Error,
        hir::Type::ArraySlice(slice) => slice.ty,
        hir::Type::ArrayStatic(array) => array.ty,
        hir::Type::ArrayStaticDecl(array) => array.ty,
        _ => {
            let ty_format = type_format(hb, ty);
            hb.error(
                ErrorComp::error(format!("cannot index value of type {}", ty_format,))
                    .context(hb.src(from_id, index_range)),
            );
            hir::Type::Error
        }
    }
}

/*
path syntax resolution:
prefix.name.name.name ...

prefix:
super.   -> start at scope_id
package. -> start at scope_id

items:
mod        -> <chained> will change target scope of an item
proc       -> [no follow]
enum       -> <follow?> by single enum variant name
union      -> [no follow]
struct     -> [no follow]
const      -> <follow?> by <chained> field access
global     -> <follow?> by <chained> field access
param_var  -> <follow?> by <chained> field access
local_var  -> <follow?> by <chained> field access

*/

fn path_resolve_target_scope<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    path: &'ast ast::Path<'ast>,
) -> Option<(hir::ScopeID, &'ast [ast::Ident])> {
    let mut target_id = match path.kind {
        ast::PathKind::None => origin_id,
        ast::PathKind::Super => match hb.scope_parent(origin_id) {
            Some(it) => it,
            None => {
                let range = TextRange::new(path.range_start, path.range_start + 5.into());
                hb.error(
                    ErrorComp::error("parent module `super` cannot be used from the root module")
                        .context(hb.src(origin_id, range)),
                );
                return None;
            }
        },
        ast::PathKind::Package => hb::ROOT_SCOPE_ID,
    };

    let mut mod_count: usize = 0;
    for name in path.names {
        match hb.symbol_from_scope(origin_id, target_id, path.kind, name.id) {
            Some((symbol, source)) => match symbol {
                hb::SymbolKind::Mod(id) => {
                    let data = hb.get_mod(id);
                    if let Some(new_target) = data.target {
                        mod_count += 1;
                        target_id = new_target;
                    } else {
                        hb.error(
                            ErrorComp::error(format!(
                                "module `{}` does not have its associated file",
                                hb.name_str(name.id)
                            ))
                            .context(hb.src(origin_id, name.range))
                            .context_info("defined here", source),
                        );
                        return None;
                    }
                }
                _ => break,
            },
            None => {
                hb.error(
                    ErrorComp::error(format!("name `{}` is not found", hb.name_str(name.id)))
                        .context(hb.src(origin_id, name.range)),
                );
                return None;
            }
        }
    }

    Some((target_id, &path.names[mod_count..]))
}

pub fn path_resolve_as_module_path<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    path: &'ast ast::Path<'ast>,
) -> Option<hir::ScopeID> {
    let (target_id, names) = path_resolve_target_scope(hb, origin_id, path)?;

    match names.first() {
        Some(name) => {
            hb.error(
                ErrorComp::error(format!("`{}` is not a module", hb.name_str(name.id)))
                    .context(hb.src(origin_id, name.range)),
            );
            None
        }
        _ => Some(target_id),
    }
}

pub fn path_resolve_as_type<'ast, 'hir>(
    hb: &mut hb::HirBuilder<'_, 'ast, 'hir>,
    origin_id: hir::ScopeID,
    path: &'ast ast::Path<'ast>,
) -> hir::Type<'hir> {
    let (target_id, names) = match path_resolve_target_scope(hb, origin_id, path) {
        Some(it) => it,
        None => return hir::Type::Error,
    };
    let mut names = names.iter();

    match names.next() {
        Some(name) => match hb.symbol_from_scope(origin_id, target_id, path.kind, name.id) {
            Some((kind, source)) => {
                let ty = match kind {
                    hb::SymbolKind::Enum(id) => hir::Type::Enum(id),
                    hb::SymbolKind::Union(id) => hir::Type::Union(id),
                    hb::SymbolKind::Struct(id) => hir::Type::Struct(id),
                    _ => {
                        hb.error(
                            ErrorComp::error(format!("expected type, got other item",))
                                .context(hb.src(origin_id, name.range))
                                .context_info("defined here", source),
                        );
                        return hir::Type::Error;
                    }
                };
                if let Some(next_name) = names.next() {
                    hb.error(
                        ErrorComp::error(format!("type cannot be accessed further",))
                            .context(hb.src(origin_id, next_name.range))
                            .context_info("defined here", source),
                    );
                    return hir::Type::Error;
                }
                ty
            }
            None => {
                //@is a duplicate check
                // maybe module resolver can return a Option<(SymbolKind, SourceRange)>
                // which was seen before breaking
                hb.error(
                    ErrorComp::error(format!("name `{}` is not found", hb.name_str(name.id)))
                        .context(hb.src(origin_id, name.range)),
                );
                hir::Type::Error
            }
        },
        None => {
            let range = TextRange::new(
                path.range_start,
                path.names.last().expect("non empty path").range.end(), //@just store path range in ast?
            );
            hb.error(
                ErrorComp::error(format!("expected type, got module path",))
                    .context(hb.src(origin_id, range)),
            );
            hir::Type::Error
        }
    }
}