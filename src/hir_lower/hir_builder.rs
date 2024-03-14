use crate::ast::ast;
use crate::ast::intern::InternID;
use crate::ast::CompCtx;
use crate::error::{ErrorComp, SourceRange};
use crate::hir;
use crate::mem::Arena;
use crate::text::TextRange;
use crate::vfs;
use std::collections::HashMap;

pub struct HirBuilder<'ctx, 'ast, 'hir> {
    ctx: &'ctx CompCtx,
    ast: ast::Ast<'ast>,
    mods: Vec<ModData>,
    scopes: Vec<Scope<'ast>>,
    errors: Vec<ErrorComp>,
    hir: hir::Hir<'hir>,
    ast_procs: Vec<&'ast ast::ProcDecl<'ast>>,
    ast_enums: Vec<&'ast ast::EnumDecl<'ast>>,
    ast_unions: Vec<&'ast ast::UnionDecl<'ast>>,
    ast_structs: Vec<&'ast ast::StructDecl<'ast>>,
    ast_consts: Vec<&'ast ast::ConstDecl<'ast>>,
    ast_globals: Vec<&'ast ast::GlobalDecl<'ast>>,
    ast_const_exprs: Vec<ast::ConstExpr<'ast>>,
}

#[derive(Copy, Clone)]
pub struct ModID(u32);
pub struct ModData {
    pub from_id: hir::ScopeID,
    pub vis: ast::Vis,
    pub name: ast::Ident,
    pub target: Option<hir::ScopeID>,
}

pub const ROOT_SCOPE_ID: hir::ScopeID = hir::ScopeID::new(0);
pub const DUMMY_CONST_EXPR_ID: hir::ConstExprID = hir::ConstExprID::new(u32::MAX as usize);

pub struct Scope<'ast> {
    parent: Option<hir::ScopeID>,
    module: ast::Module<'ast>,
    symbols: HashMap<InternID, Symbol>,
}

#[rustfmt::skip]
#[derive(Copy, Clone)]
pub enum Symbol {
    Defined  { kind: SymbolKind, },
    Imported { kind: SymbolKind, use_range: TextRange },
}

#[derive(Copy, Clone)]
pub enum SymbolKind {
    Mod(ModID),
    Proc(hir::ProcID),
    Enum(hir::EnumID),
    Union(hir::UnionID),
    Struct(hir::StructID),
    Const(hir::ConstID),
    Global(hir::GlobalID),
}

impl<'ctx, 'ast, 'hir> HirBuilder<'ctx, 'ast, 'hir> {
    pub fn new(ctx: &'ctx CompCtx, ast: ast::Ast<'ast>) -> HirBuilder<'ctx, 'ast, 'hir> {
        HirBuilder {
            ctx,
            ast,
            mods: Vec::new(),
            scopes: Vec::new(),
            errors: Vec::new(),
            hir: hir::Hir {
                arena: Arena::new(),
                scopes: Vec::new(),
                procs: Vec::new(),
                enums: Vec::new(),
                unions: Vec::new(),
                structs: Vec::new(),
                consts: Vec::new(),
                globals: Vec::new(),
                const_exprs: Vec::new(),
            },
            ast_procs: Vec::new(),
            ast_enums: Vec::new(),
            ast_unions: Vec::new(),
            ast_structs: Vec::new(),
            ast_consts: Vec::new(),
            ast_globals: Vec::new(),
            ast_const_exprs: Vec::new(),
        }
    }

    pub fn finish(self) -> Result<hir::Hir<'hir>, Vec<ErrorComp>> {
        if self.errors.is_empty() {
            Ok(self.hir)
        } else {
            Err(self.errors)
        }
    }

    pub fn ctx(&self) -> &'ctx CompCtx {
        self.ctx
    }
    pub fn name_str(&self, id: InternID) -> &str {
        self.ctx.intern().get_str(id)
    }
    pub fn ast_modules(&self) -> impl Iterator<Item = &ast::Module<'ast>> {
        self.ast.modules.iter()
    }
    pub fn error(&mut self, error: ErrorComp) {
        self.errors.push(error);
    }
    pub fn arena(&mut self) -> &mut Arena<'hir> {
        &mut self.hir.arena
    }

    pub fn proc_ids(&self) -> impl Iterator<Item = hir::ProcID> {
        (0..self.hir.procs.len()).map(hir::ProcID::new)
    }
    pub fn enum_ids(&self) -> impl Iterator<Item = hir::EnumID> {
        (0..self.hir.enums.len()).map(hir::EnumID::new)
    }
    pub fn union_ids(&self) -> impl Iterator<Item = hir::UnionID> {
        (0..self.hir.unions.len()).map(hir::UnionID::new)
    }
    pub fn struct_ids(&self) -> impl Iterator<Item = hir::StructID> {
        (0..self.hir.structs.len()).map(hir::StructID::new)
    }
    pub fn const_ids(&self) -> impl Iterator<Item = hir::ConstID> {
        (0..self.hir.consts.len()).map(hir::ConstID::new)
    }
    pub fn global_ids(&self) -> impl Iterator<Item = hir::GlobalID> {
        (0..self.hir.globals.len()).map(hir::GlobalID::new)
    }

    pub fn proc_ast(&self, id: hir::ProcID) -> &'ast ast::ProcDecl<'ast> {
        self.ast_procs.get(id.index()).unwrap()
    }
    pub fn enum_ast(&self, id: hir::EnumID) -> &'ast ast::EnumDecl<'ast> {
        self.ast_enums.get(id.index()).unwrap()
    }
    pub fn union_ast(&self, id: hir::UnionID) -> &'ast ast::UnionDecl<'ast> {
        self.ast_unions.get(id.index()).unwrap()
    }
    pub fn struct_ast(&self, id: hir::StructID) -> &'ast ast::StructDecl<'ast> {
        self.ast_structs.get(id.index()).unwrap()
    }
    pub fn const_ast(&self, id: hir::ConstID) -> &'ast ast::ConstDecl<'ast> {
        self.ast_consts.get(id.index()).unwrap()
    }
    pub fn global_ast(&self, id: hir::GlobalID) -> &'ast ast::GlobalDecl<'ast> {
        self.ast_globals.get(id.index()).unwrap()
    }
    pub fn const_expr_ast(&self, id: hir::ConstExprID) -> &'ast ast::Expr<'ast> {
        self.ast_const_exprs.get(id.index()).unwrap().0
    }

    pub fn proc_data(&self, id: hir::ProcID) -> &hir::ProcData<'hir> {
        self.hir.procs.get(id.index()).unwrap()
    }
    pub fn enum_data(&self, id: hir::EnumID) -> &hir::EnumData<'hir> {
        self.hir.enums.get(id.index()).unwrap()
    }
    pub fn union_data(&self, id: hir::UnionID) -> &hir::UnionData<'hir> {
        self.hir.unions.get(id.index()).unwrap()
    }
    pub fn struct_data(&self, id: hir::StructID) -> &hir::StructData<'hir> {
        self.hir.structs.get(id.index()).unwrap()
    }
    pub fn const_data(&self, id: hir::ConstID) -> &hir::ConstData<'hir> {
        self.hir.consts.get(id.index()).unwrap()
    }
    pub fn global_data(&self, id: hir::GlobalID) -> &hir::GlobalData<'hir> {
        self.hir.globals.get(id.index()).unwrap()
    }
    pub fn const_expr_data(&self, id: hir::ConstExprID) -> &hir::ConstExprData<'hir> {
        self.hir.const_exprs.get(id.index()).unwrap()
    }

    pub fn proc_data_mut(&mut self, id: hir::ProcID) -> &mut hir::ProcData<'hir> {
        self.hir.procs.get_mut(id.index()).unwrap()
    }
    pub fn enum_data_mut(&mut self, id: hir::EnumID) -> &mut hir::EnumData<'hir> {
        self.hir.enums.get_mut(id.index()).unwrap()
    }
    pub fn union_data_mut(&mut self, id: hir::UnionID) -> &mut hir::UnionData<'hir> {
        self.hir.unions.get_mut(id.index()).unwrap()
    }
    pub fn struct_data_mut(&mut self, id: hir::StructID) -> &mut hir::StructData<'hir> {
        self.hir.structs.get_mut(id.index()).unwrap()
    }
    pub fn const_data_mut(&mut self, id: hir::ConstID) -> &mut hir::ConstData<'hir> {
        self.hir.consts.get_mut(id.index()).unwrap()
    }
    pub fn global_data_mut(&mut self, id: hir::GlobalID) -> &mut hir::GlobalData<'hir> {
        self.hir.globals.get_mut(id.index()).unwrap()
    }
    pub fn const_expr_data_mut(&mut self, id: hir::ConstExprID) -> &mut hir::ConstExprData<'hir> {
        self.hir.const_exprs.get_mut(id.index()).unwrap()
    }

    pub fn add_proc(
        &mut self,
        decl: &'ast ast::ProcDecl<'ast>,
        data: hir::ProcData<'hir>,
    ) -> Symbol {
        let id = hir::ProcID::new(self.ast_procs.len());
        self.ast_procs.push(decl);
        self.hir.procs.push(data);
        Symbol::Defined {
            kind: SymbolKind::Proc(id),
        }
    }
    pub fn add_enum(
        &mut self,
        decl: &'ast ast::EnumDecl<'ast>,
        data: hir::EnumData<'hir>,
    ) -> Symbol {
        let id = hir::EnumID::new(self.ast_enums.len());
        self.ast_enums.push(decl);
        self.hir.enums.push(data);
        Symbol::Defined {
            kind: SymbolKind::Enum(id),
        }
    }
    pub fn add_union(
        &mut self,
        decl: &'ast ast::UnionDecl<'ast>,
        data: hir::UnionData<'hir>,
    ) -> Symbol {
        let id = hir::UnionID::new(self.ast_unions.len());
        self.ast_unions.push(decl);
        self.hir.unions.push(data);
        Symbol::Defined {
            kind: SymbolKind::Union(id),
        }
    }
    pub fn add_struct(
        &mut self,
        decl: &'ast ast::StructDecl<'ast>,
        data: hir::StructData<'hir>,
    ) -> Symbol {
        let id = hir::StructID::new(self.ast_structs.len());
        self.ast_structs.push(decl);
        self.hir.structs.push(data);
        Symbol::Defined {
            kind: SymbolKind::Struct(id),
        }
    }
    pub fn add_const(
        &mut self,
        decl: &'ast ast::ConstDecl<'ast>,
        data: hir::ConstData<'hir>,
    ) -> Symbol {
        let id = hir::ConstID::new(self.ast_consts.len());
        self.ast_consts.push(decl);
        self.hir.consts.push(data);
        Symbol::Defined {
            kind: SymbolKind::Const(id),
        }
    }
    pub fn add_global(
        &mut self,
        decl: &'ast ast::GlobalDecl<'ast>,
        data: hir::GlobalData<'hir>,
    ) -> Symbol {
        let id = hir::GlobalID::new(self.ast_globals.len());
        self.ast_globals.push(decl);
        self.hir.globals.push(data);
        Symbol::Defined {
            kind: SymbolKind::Global(id),
        }
    }

    pub fn add_const_expr(
        &mut self,
        from_id: hir::ScopeID,
        const_expr: ast::ConstExpr<'ast>,
    ) -> hir::ConstExprID {
        let id = hir::ConstExprID::new(self.ast_const_exprs.len());
        self.ast_const_exprs.push(const_expr);
        self.hir.const_exprs.push(hir::ConstExprData {
            from_id,
            value: None,
        });
        id
    }

    pub fn add_mod(&mut self, data: ModData) -> (Symbol, ModID) {
        let id = ModID(self.mods.len() as u32);
        self.mods.push(data);
        let symbol = Symbol::Defined {
            kind: SymbolKind::Mod(id),
        };
        (symbol, id)
    }

    pub fn scope_ids(&self) -> impl Iterator<Item = hir::ScopeID> {
        (0..self.scopes.len()).map(hir::ScopeID::new)
    }

    pub fn get_mod(&self, id: ModID) -> &ModData {
        self.mods.get(id.0 as usize).unwrap()
    }
    pub fn get_mod_mut(&mut self, id: ModID) -> &mut ModData {
        self.mods.get_mut(id.0 as usize).unwrap()
    }

    pub fn add_scope(&mut self, scope: Scope<'ast>) -> hir::ScopeID {
        let id = hir::ScopeID::new(self.scopes.len());
        self.scopes.push(scope);
        id
    }
    pub fn get_scope(&self, id: hir::ScopeID) -> &Scope<'ast> {
        self.scopes.get(id.index()).unwrap()
    }
    pub fn get_scope_mut(&mut self, id: hir::ScopeID) -> &mut Scope<'ast> {
        self.scopes.get_mut(id.index()).unwrap()
    }

    pub fn symbol_range(&self, symbol: Symbol) -> TextRange {
        match symbol {
            Symbol::Defined { kind } => self.symbol_kind_range(kind),
            Symbol::Imported { use_range, .. } => use_range,
        }
    }

    fn symbol_kind_range(&self, kind: SymbolKind) -> TextRange {
        match kind {
            SymbolKind::Mod(id) => self.get_mod(id).name.range,
            SymbolKind::Proc(id) => self.hir.proc_data(id).name.range,
            SymbolKind::Enum(id) => self.hir.enum_data(id).name.range,
            SymbolKind::Union(id) => self.hir.union_data(id).name.range,
            SymbolKind::Struct(id) => self.hir.struct_data(id).name.range,
            SymbolKind::Const(id) => self.hir.const_data(id).name.range,
            SymbolKind::Global(id) => self.hir.global_data(id).name.range,
        }
    }
}

impl<'ast> Scope<'ast> {
    pub fn new(parent: Option<hir::ScopeID>, module: ast::Module<'ast>) -> Scope {
        Scope {
            parent,
            module,
            symbols: HashMap::new(),
        }
    }

    pub fn parent(&self) -> Option<hir::ScopeID> {
        self.parent
    }

    pub fn file_id(&self) -> vfs::FileID {
        self.module.file_id
    }

    pub fn ast_decls(&self) -> impl Iterator<Item = ast::Decl<'ast>> {
        self.module.decls.into_iter()
    }

    pub fn add_symbol(&mut self, id: InternID, symbol: Symbol) {
        assert!(self.get_symbol(id).is_none());
        self.symbols.insert(id, symbol);
    }

    pub fn get_symbol(&self, id: InternID) -> Option<Symbol> {
        self.symbols.get(&id).cloned()
    }

    pub fn source(&self, range: TextRange) -> SourceRange {
        SourceRange::new(range, self.file_id())
    }
}
