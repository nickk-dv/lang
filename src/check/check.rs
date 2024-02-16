use super::*;
use crate::ast::parser::CompCtx;
use crate::err::ansi;
use crate::err::span_fmt;

pub fn check(ctx: &CompCtx, ast: &Ast) {
    let mut context = Context::new();
    pass_0_populate_scopes(&mut context, &ast, ctx);
    pass_1_check_namesets(&context, ctx);
}

fn report_no_src(message: &'static str) {
    let ansi_red = ansi::Color::as_ansi_str(ansi::Color::BoldRed);
    let ansi_clear = "\x1B[0m";
    eprintln!("{}error:{} {}", ansi_red, ansi_clear, message);
}

fn report(message: &'static str, ctx: &CompCtx, src: SourceLoc) {
    let ansi_red = ansi::Color::as_ansi_str(ansi::Color::BoldRed);
    let ansi_clear = "\x1B[0m";
    eprintln!("{}error:{} {}", ansi_red, ansi_clear, message);
    span_fmt::print_simple(ctx.file(src.file_id), src.span, None, false);
}

fn report_info(marker: &'static str, ctx: &CompCtx, src: SourceLoc) {
    span_fmt::print_simple(ctx.file(src.file_id), src.span, Some(marker), true);
}

fn pass_0_populate_scopes(context: &mut Context, ast: &Ast, ctx: &CompCtx) {
    use std::path::PathBuf;

    struct ScopeTreeTask {
        module: P<Module>,
        parent: Option<(ScopeID, ModuleID)>,
    }

    let mut module_map = HashMap::<PathBuf, Result<P<Module>, SourceLoc>>::new();
    for module in ast.modules.iter() {
        module_map.insert(ctx.file(module.file_id).path.clone(), Ok(*module));
    }

    let mut tasks = Vec::<ScopeTreeTask>::new();

    let root_path = PathBuf::new().join("test").join("main.lang");
    match module_map.get(&root_path).cloned() {
        Some(p) => match p {
            Ok(module) => {
                tasks.push(ScopeTreeTask {
                    module,
                    parent: None,
                });
            }
            Err(..) => {}
        },
        None => {
            report_no_src("missing root `main` or `lib` module");
            return;
        }
    };

    while let Some(task) = tasks.pop() {
        let parent_id = match task.parent {
            Some((scope_id, ..)) => Some(scope_id),
            None => None,
        };
        let scope_id = context.add_scope(Scope::new(task.module, parent_id));
        if let Some((.., module_id)) = task.parent {
            let parent_module = context.get_module_mut(module_id);
            parent_module.target_id = Some(scope_id);
        }

        for decl in task.module.decls {
            match decl {
                Decl::Module(sym_decl) => {
                    let scope = context.get_scope(scope_id);
                    if let Some(existing) = scope.get_symbol(sym_decl.name.id) {
                        let src = SourceLoc::new(sym_decl.name.span, scope.module.file_id);
                        let info_src = context.get_symbol_src(existing);
                        report("symbol redefinition", ctx, src);
                        report_info("already declared here", ctx, info_src)
                    } else {
                        let id = context.add_module(ModuleData {
                            from_id: scope_id,
                            decl: sym_decl,
                            target_id: None,
                        });
                        let scope = context.get_scope_mut(scope_id);
                        let _ = scope.add_symbol(sym_decl.name.id, Symbol::Module(id));

                        let name = ctx.intern().get_str(sym_decl.name.id);
                        let name_ext = name.to_string().clone() + ".lang";
                        let mut origin = ctx.file(scope.module.file_id).path.clone();
                        origin.pop();
                        let path1 = origin.clone().join(name_ext);
                        let path2 = origin.join(name).join("mod.lang");

                        let src = SourceLoc::new(sym_decl.name.span, scope.module.file_id);
                        let target = match (
                            module_map.get(&path1).cloned(),
                            module_map.get(&path2).cloned(),
                        ) {
                            (None, None) => {
                                report("both module paths are missing", ctx, src);
                                eprintln!("path: {:?}", path1);
                                eprintln!("path2: {:?}", path2);
                                continue;
                            }
                            (Some(..), Some(..)) => {
                                report("both module paths are prevent", ctx, src);
                                eprintln!("path: {:?}", path1);
                                eprintln!("path2: {:?}", path2);
                                continue;
                            }
                            (Some(p), None) => match p {
                                Ok(module) => {
                                    if let Some(node) = module_map.get_mut(&path1) {
                                        *node = Err(src);
                                    }
                                    module
                                }
                                Err(taken) => {
                                    report("module has been taken", ctx, src);
                                    report_info("by this module declaration", ctx, taken);
                                    eprintln!("path: {:?}", path1);
                                    continue;
                                }
                            },
                            (None, Some(p)) => match p {
                                Ok(module) => {
                                    if let Some(node) = module_map.get_mut(&path2) {
                                        *node = Err(src);
                                    }
                                    module
                                }
                                Err(taken) => {
                                    report("module has been taken", ctx, src);
                                    report_info("by this module declaration", ctx, taken);
                                    eprintln!("path: {:?}", path2);
                                    continue;
                                }
                            },
                        };

                        tasks.push(ScopeTreeTask {
                            module: target,
                            parent: Some((scope_id, id)),
                        });
                    }
                }
                Decl::Global(sym_decl) => {
                    let scope = context.get_scope(scope_id);
                    if let Some(existing) = scope.get_symbol(sym_decl.name.id) {
                        let src = SourceLoc::new(sym_decl.name.span, scope.module.file_id);
                        let info_src = context.get_symbol_src(existing);
                        report("symbol redefinition", ctx, src);
                        report_info("already declared here", ctx, info_src)
                    } else {
                        let id = context.add_global(GlobalData {
                            from_id: scope_id,
                            decl: sym_decl,
                        });
                        let scope = context.get_scope_mut(scope_id);
                        let _ = scope.add_symbol(sym_decl.name.id, Symbol::Global(id));
                    }
                }
                Decl::Proc(sym_decl) => {
                    let scope = context.get_scope(scope_id);
                    if let Some(existing) = scope.get_symbol(sym_decl.name.id) {
                        let src = SourceLoc::new(sym_decl.name.span, scope.module.file_id);
                        let info_src = context.get_symbol_src(existing);
                        report("symbol redefinition", ctx, src);
                        report_info("already declared here", ctx, info_src)
                    } else {
                        let id = context.add_proc(ProcData {
                            from_id: scope_id,
                            decl: sym_decl,
                        });
                        let scope = context.get_scope_mut(scope_id);
                        let _ = scope.add_symbol(sym_decl.name.id, Symbol::Proc(id));
                    }
                }
                Decl::Enum(sym_decl) => {
                    let scope = context.get_scope(scope_id);
                    if let Some(existing) = scope.get_symbol(sym_decl.name.id) {
                        let src = SourceLoc::new(sym_decl.name.span, scope.module.file_id);
                        let info_src = context.get_symbol_src(existing);
                        report("symbol redefinition", ctx, src);
                        report_info("already declared here", ctx, info_src)
                    } else {
                        let id = context.add_enum(EnumData {
                            from_id: scope_id,
                            decl: sym_decl,
                        });
                        let scope = context.get_scope_mut(scope_id);
                        let _ = scope.add_symbol(sym_decl.name.id, Symbol::Enum(id));
                    }
                }
                Decl::Union(sym_decl) => {
                    let scope = context.get_scope(scope_id);
                    if let Some(existing) = scope.get_symbol(sym_decl.name.id) {
                        let src = SourceLoc::new(sym_decl.name.span, scope.module.file_id);
                        let info_src = context.get_symbol_src(existing);
                        report("symbol redefinition", ctx, src);
                        report_info("already declared here", ctx, info_src)
                    } else {
                        let id = context.add_union(UnionData {
                            from_id: scope_id,
                            decl: sym_decl,
                            size: 0,
                            align: 0,
                        });
                        let scope = context.get_scope_mut(scope_id);
                        let _ = scope.add_symbol(sym_decl.name.id, Symbol::Union(id));
                    }
                }
                Decl::Struct(sym_decl) => {
                    let scope = context.get_scope(scope_id);
                    if let Some(existing) = scope.get_symbol(sym_decl.name.id) {
                        let src = SourceLoc::new(sym_decl.name.span, scope.module.file_id);
                        let info_src = context.get_symbol_src(existing);
                        report("symbol redefinition", ctx, src);
                        report_info("already declared here", ctx, info_src)
                    } else {
                        let id = context.add_struct(StructData {
                            from_id: scope_id,
                            decl: sym_decl,
                            size: 0,
                            align: 0,
                        });
                        let scope = context.get_scope_mut(scope_id);
                        let _ = scope.add_symbol(sym_decl.name.id, Symbol::Struct(id));
                    }
                }
                Decl::Import(..) => {}
            }
        }
    }
}

fn pass_1_check_namesets(context: &Context, ctx: &CompCtx) {
    for scope_id in context.scope_iter() {
        let mut name_set = HashMap::<InternID, Span>::new();

        let scope = context.get_scope(scope_id);
        let file_id = scope.module.file_id;

        for decl in scope.module.decls {
            match decl {
                Decl::Proc(proc_decl) => {
                    if proc_decl.params.is_empty() {
                        continue;
                    }
                    name_set.clear();
                    for param in proc_decl.params.iter() {
                        if let Some(existing) = name_set.get(&param.name.id).cloned() {
                            report(
                                "proc param redefinition",
                                ctx,
                                SourceLoc::new(param.name.span, file_id),
                            );
                            report_info(
                                "already defined here",
                                ctx,
                                SourceLoc::new(existing, file_id),
                            )
                        } else {
                            name_set.insert(param.name.id, param.name.span);
                        }
                    }
                }
                Decl::Enum(enum_decl) => {
                    if enum_decl.variants.is_empty() {
                        continue;
                    }
                    name_set.clear();
                    for variant in enum_decl.variants.iter() {
                        if let Some(existing) = name_set.get(&variant.name.id).cloned() {
                            report(
                                "enum variant redefinition",
                                ctx,
                                SourceLoc::new(variant.name.span, file_id),
                            );
                            report_info(
                                "already defined here",
                                ctx,
                                SourceLoc::new(existing, file_id),
                            )
                        } else {
                            name_set.insert(variant.name.id, variant.name.span);
                        }
                    }
                }
                Decl::Union(union_decl) => {
                    if union_decl.members.is_empty() {
                        continue;
                    }
                    name_set.clear();
                    for member in union_decl.members.iter() {
                        if let Some(existing) = name_set.get(&member.name.id).cloned() {
                            report(
                                "union member redefinition",
                                ctx,
                                SourceLoc::new(member.name.span, file_id),
                            );
                            report_info(
                                "already defined here",
                                ctx,
                                SourceLoc::new(existing, file_id),
                            )
                        } else {
                            name_set.insert(member.name.id, member.name.span);
                        }
                    }
                }
                Decl::Struct(struct_decl) => {
                    if struct_decl.fields.is_empty() {
                        continue;
                    }
                    name_set.clear();
                    for field in struct_decl.fields.iter() {
                        if let Some(existing) = name_set.get(&field.name.id).cloned() {
                            report(
                                "struct field redefinition",
                                ctx,
                                SourceLoc::new(field.name.span, file_id),
                            );
                            report_info(
                                "already defined here",
                                ctx,
                                SourceLoc::new(existing, file_id),
                            )
                        } else {
                            name_set.insert(field.name.id, field.name.span);
                        }
                    }
                }
                _ => {}
            }
        }
    }
}