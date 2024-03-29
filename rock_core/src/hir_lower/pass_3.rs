use super::hir_build::{HirData, HirEmit};
use crate::ast;
use crate::error::ErrorComp;
use crate::hir;

pub fn run<'hir, 'ast>(hir: &mut HirData<'hir, 'ast>, emit: &mut HirEmit<'hir>) {
    for id in hir.proc_ids() {
        process_proc_data(hir, emit, id)
    }
    for id in hir.enum_ids() {
        process_enum_data(hir, emit, id)
    }
    for id in hir.union_ids() {
        process_union_data(hir, emit, id)
    }
    for id in hir.struct_ids() {
        process_struct_data(hir, emit, id)
    }
    for id in hir.const_ids() {
        process_const_data(hir, emit, id)
    }
    for id in hir.global_ids() {
        process_global_data(hir, emit, id)
    }
}

pub fn resolve_type_instant<'hir, 'ast>(
    hir: &HirData<'hir, 'ast>,
    emit: &mut HirEmit<'hir>,
    origin_id: hir::ScopeID,
    ast_ty: ast::Type<'ast>,
) -> hir::Type<'hir> {
    match ast_ty {
        ast::Type::Basic(basic) => hir::Type::Basic(basic),
        ast::Type::Custom(path) => super::pass_5::path_resolve_as_type(hir, emit, origin_id, path),
        ast::Type::Reference(ref_ty, mutt) => {
            let ref_ty = resolve_type_instant(hir, emit, origin_id, *ref_ty);
            let ty = emit.arena.alloc(ref_ty);
            hir::Type::Reference(ty, mutt)
        }
        ast::Type::ArraySlice(slice) => {
            let elem_ty = resolve_type_instant(hir, emit, origin_id, slice.ty);
            let hir_slice = emit.arena.alloc(hir::ArraySlice {
                mutt: slice.mutt,
                ty: elem_ty,
            });
            hir::Type::ArraySlice(hir_slice)
        }
        ast::Type::ArrayStatic(array) => {
            let size =
                super::pass_4::const_resolve_const_expr_instant(hir, emit, origin_id, array.size.0);
            let elem_ty = resolve_type_instant(hir, emit, origin_id, array.ty);
            let hir_array = emit.arena.alloc(hir::ArrayStatic { size, ty: elem_ty });
            hir::Type::ArrayStatic(hir_array)
        }
    }
}

pub fn resolve_type_delayed<'hir, 'ast>(
    hir: &mut HirData<'hir, 'ast>,
    emit: &mut HirEmit<'hir>,
    origin_id: hir::ScopeID,
    ast_ty: ast::Type<'ast>,
) -> hir::Type<'hir> {
    match ast_ty {
        ast::Type::Basic(basic) => hir::Type::Basic(basic),
        ast::Type::Custom(path) => super::pass_5::path_resolve_as_type(hir, emit, origin_id, path),
        ast::Type::Reference(ref_ty, mutt) => {
            let ref_ty = resolve_type_delayed(hir, emit, origin_id, *ref_ty);
            let ty = emit.arena.alloc(ref_ty);
            hir::Type::Reference(ty, mutt)
        }
        ast::Type::ArraySlice(slice) => {
            let elem_ty = resolve_type_delayed(hir, emit, origin_id, slice.ty);
            let hir_slice = emit.arena.alloc(hir::ArraySlice {
                mutt: slice.mutt,
                ty: elem_ty,
            });
            hir::Type::ArraySlice(hir_slice)
        }
        ast::Type::ArrayStatic(array) => {
            let const_id = hir.add_const_expr(origin_id, array.size);
            let elem_ty = resolve_type_delayed(hir, emit, origin_id, array.ty);
            let hir_array = emit.arena.alloc(hir::ArrayStaticDecl {
                size: const_id,
                ty: elem_ty,
            });
            hir::Type::ArrayStaticDecl(hir_array)
        }
    }
}

fn process_proc_data<'hir>(hir: &mut HirData<'hir, '_>, emit: &mut HirEmit<'hir>, id: hir::ProcID) {
    let item = hir.proc_ast(id);
    let origin_id = hir.proc_data(id).origin_id;
    let mut unique = Vec::<hir::ProcParam>::new();

    for param in item.params.iter() {
        if let Some(existing) = unique.iter().find(|&it| it.name.id == param.name.id) {
            emit.error(
                ErrorComp::error(format!(
                    "parameter `{}` is defined multiple times",
                    hir.name_str(param.name.id)
                ))
                .context(hir.src(origin_id, param.name.range))
                .context_info(
                    "existing parameter",
                    hir.src(origin_id, existing.name.range),
                ),
            );
        } else {
            unique.push(hir::ProcParam {
                mutt: param.mutt,
                name: param.name,
                ty: resolve_type_delayed(hir, emit, origin_id, param.ty),
            });
        }
    }

    hir.proc_data_mut(id).params = emit.arena.alloc_slice(&unique);
    hir.proc_data_mut(id).return_ty = if let Some(ret_ty) = item.return_ty {
        resolve_type_delayed(hir, emit, origin_id, ret_ty)
    } else {
        hir::Type::Basic(ast::BasicType::Unit)
    }
}

fn process_enum_data<'hir>(hir: &mut HirData<'hir, '_>, emit: &mut HirEmit<'hir>, id: hir::EnumID) {
    let item = hir.enum_ast(id);
    let origin_id = hir.enum_data(id).origin_id;
    let mut unique = Vec::<hir::EnumVariant>::new();

    for variant in item.variants.iter() {
        if let Some(existing) = unique.iter().find(|&it| it.name.id == variant.name.id) {
            emit.error(
                ErrorComp::error(format!(
                    "variant `{}` is defined multiple times",
                    hir.name_str(variant.name.id)
                ))
                .context(hir.src(origin_id, variant.name.range))
                .context_info("existing variant", hir.src(origin_id, existing.name.range)),
            );
        } else {
            unique.push(hir::EnumVariant {
                name: variant.name,
                value: variant
                    .value
                    .map(|value| hir.add_const_expr(origin_id, value)),
            });
        }
    }
    hir.enum_data_mut(id).variants = emit.arena.alloc_slice(&unique);
}

fn process_union_data<'hir>(
    hir: &mut HirData<'hir, '_>,
    emit: &mut HirEmit<'hir>,
    id: hir::UnionID,
) {
    let item = hir.union_ast(id);
    let origin_id = hir.union_data(id).origin_id;
    let mut unique = Vec::<hir::UnionMember>::new();

    for member in item.members.iter() {
        if let Some(existing) = unique.iter().find(|&it| it.name.id == member.name.id) {
            emit.error(
                ErrorComp::error(format!(
                    "member `{}` is defined multiple times",
                    hir.name_str(member.name.id)
                ))
                .context(hir.src(origin_id, member.name.range))
                .context_info("existing member", hir.src(origin_id, existing.name.range)),
            );
        } else {
            unique.push(hir::UnionMember {
                name: member.name,
                ty: resolve_type_delayed(hir, emit, origin_id, member.ty),
            });
        }
    }
    hir.union_data_mut(id).members = emit.arena.alloc_slice(&unique);
}

fn process_struct_data<'hir>(
    hir: &mut HirData<'hir, '_>,
    emit: &mut HirEmit<'hir>,
    id: hir::StructID,
) {
    let item = hir.struct_ast(id);
    let origin_id = hir.struct_data(id).origin_id;
    let mut unique = Vec::<hir::StructField>::new();

    for field in item.fields.iter() {
        if let Some(existing) = unique.iter().find(|&it| it.name.id == field.name.id) {
            emit.error(
                ErrorComp::error(format!(
                    "field `{}` is defined multiple times",
                    hir.name_str(field.name.id)
                ))
                .context(hir.src(origin_id, field.name.range))
                .context_info("existing field", hir.src(origin_id, existing.name.range)),
            );
        } else {
            unique.push(hir::StructField {
                vis: field.vis,
                name: field.name,
                ty: resolve_type_delayed(hir, emit, origin_id, field.ty),
            });
        }
    }
    hir.struct_data_mut(id).fields = emit.arena.alloc_slice(&unique);
}

fn process_const_data<'hir>(
    hir: &mut HirData<'hir, '_>,
    emit: &mut HirEmit<'hir>,
    id: hir::ConstID,
) {
    let item = hir.const_ast(id);
    let origin_id = hir.const_data(id).origin_id;

    let ty = resolve_type_delayed(hir, emit, origin_id, item.ty);
    let const_id = hir.add_const_expr(origin_id, item.value);

    let data = hir.const_data_mut(id);
    data.ty = ty;
    data.value = const_id;
}

fn process_global_data<'hir>(
    hir: &mut HirData<'hir, '_>,
    emit: &mut HirEmit<'hir>,
    id: hir::GlobalID,
) {
    let item = hir.global_ast(id);
    let origin_id = hir.global_data(id).origin_id;

    let ty = resolve_type_delayed(hir, emit, origin_id, item.ty);
    let const_id = hir.add_const_expr(origin_id, item.value);

    let data = hir.global_data_mut(id);
    data.ty = ty;
    data.value = const_id;
}
