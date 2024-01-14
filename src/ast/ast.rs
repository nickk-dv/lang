use super::span::Span;
use crate::mem::*;
use std::path::PathBuf;

pub type ScopeID = u32;

pub struct Ast {
    pub arenas: Vec<Arena>,
    pub modules: Vec<P<Module>>,
    pub intern_pool: P<InternPool>,
}

pub struct Module {
    pub file: SourceFile,
    pub decls: List<Decl>,
}

pub struct SourceFile {
    pub path: PathBuf,
    pub source: String,
    pub line_spans: Vec<Span>,
}

#[derive(Copy, Clone)]
pub struct Ident {
    pub id: InternID,
    pub span: Span,
}

#[derive(Copy, Clone)]
pub struct ModuleAccess {
    pub modifier: ModuleAccessModifier,
    pub modifier_span: Span,
    pub names: List<Ident>,
}

#[derive(Copy, Clone, PartialEq)]
pub enum ModuleAccessModifier {
    None,
    Super,
    Package,
}

#[derive(Copy, Clone)]
pub struct Type {
    pub pointer_level: u32,
    pub kind: TypeKind,
}

#[derive(Copy, Clone)]
pub enum TypeKind {
    Basic(BasicType),
    Custom(P<CustomType>),
    ArraySlice(P<ArraySliceType>),
    ArrayStatic(P<ArrayStaticType>),
}

#[derive(Copy, Clone, PartialEq)]
pub enum BasicType {
    Bool,
    S8,
    S16,
    S32,
    S64,
    Ssize,
    U8,
    U16,
    U32,
    U64,
    Usize,
    F32,
    F64,
    Char,
    Rawptr,
}

#[derive(Copy, Clone)]
pub struct CustomType {
    pub module_access: ModuleAccess,
    pub name: Ident,
}

#[derive(Copy, Clone)]
pub struct ArraySliceType {
    pub element: Type,
}

#[derive(Copy, Clone)]
pub struct ArrayStaticType {
    pub size: Expr,
    pub element: Type,
}

#[derive(Copy, Clone, PartialEq)]
pub enum Visibility {
    Public,
    Private,
}

#[derive(Copy, Clone)]
pub enum Decl {
    Mod(P<ModDecl>),
    Proc(P<ProcDecl>),
    Enum(P<EnumDecl>),
    Struct(P<StructDecl>),
    Global(P<GlobalDecl>),
    Import(P<ImportDecl>),
}

#[derive(Copy, Clone)]
pub struct ModDecl {
    pub visibility: Visibility,
    pub name: Ident,
    pub id: Option<ScopeID>,
}

#[derive(Copy, Clone)]
pub struct ProcDecl {
    pub visibility: Visibility,
    pub name: Ident,
    pub params: List<ProcParam>,
    pub is_variadic: bool,
    pub return_type: Option<Type>,
    pub block: Option<P<Block>>,
}

#[derive(Copy, Clone)]
pub struct ProcParam {
    pub name: Ident,
    pub tt: Type,
}

#[derive(Copy, Clone)]
pub struct EnumDecl {
    pub visibility: Visibility,
    pub name: Ident,
    pub basic_type: Option<BasicType>,
    pub variants: List<EnumVariant>,
}

#[derive(Copy, Clone)]
pub struct EnumVariant {
    pub name: Ident,
    pub expr: Option<Expr>,
}

#[derive(Copy, Clone)]
pub struct StructDecl {
    pub visibility: Visibility,
    pub name: Ident,
    pub fields: List<StructField>,
}

#[derive(Copy, Clone)]
pub struct StructField {
    pub name: Ident,
    pub tt: Type,
    pub default: Option<Expr>,
}

#[derive(Copy, Clone)]
pub struct GlobalDecl {
    pub visibility: Visibility,
    pub name: Ident,
    pub expr: Expr,
}

#[derive(Copy, Clone)]
pub struct ImportDecl {
    pub span: Span,
    pub module_access: ModuleAccess,
    pub target: ImportTarget,
}

#[derive(Copy, Clone)]
pub enum ImportTarget {
    AllSymbols,
    Symbol(Ident),
    SymbolList(List<Ident>),
}

#[derive(Copy, Clone)]
pub enum Stmt {
    If(P<If>),
    For(P<For>),
    Block(P<Block>),
    Defer(P<Block>),
    Break,
    Switch(P<Switch>),
    Return(P<Return>),
    Continue,
    VarDecl(P<VarDecl>),
    VarAssign(P<VarAssign>),
    ProcCall(P<ProcCall>),
}

#[derive(Copy, Clone)]
pub struct If {
    pub condition: Expr,
    pub block: P<Block>,
    pub else_: Option<Else>,
}

#[derive(Copy, Clone)]
pub enum Else {
    If(P<If>),
    Block(P<Block>),
}

#[derive(Copy, Clone)]
pub struct For {
    pub var_decl: Option<P<VarDecl>>,
    pub condition: Option<Expr>,
    pub var_assign: Option<P<VarAssign>>,
    pub block: P<Block>,
}

#[derive(Copy, Clone)]
pub struct Block {
    pub stmts: List<Stmt>,
}

#[derive(Copy, Clone)]
pub struct Switch {
    pub expr: Expr,
    pub cases: List<SwitchCase>,
}

#[derive(Copy, Clone)]
pub struct SwitchCase {
    pub expr: Expr,
    pub block: P<Block>,
}

#[derive(Copy, Clone)]
pub struct Return {
    pub expr: Option<Expr>,
}

#[derive(Copy, Clone)]
pub struct VarDecl {
    pub name: Ident,
    pub tt: Option<Type>,
    pub expr: Option<Expr>,
}

#[derive(Copy, Clone)]
pub struct VarAssign {
    pub var: P<Var>,
    pub op: AssignOp,
    pub expr: Expr,
}

#[derive(Copy, Clone)]
pub enum AssignOp {
    Assign,
    BinaryOp(BinaryOp),
}

#[derive(Copy, Clone)]
pub enum Expr {
    Var(P<Var>),
    Enum(P<Enum>),
    Cast(P<Cast>),
    Sizeof(P<Sizeof>),
    Literal(P<Literal>),
    ProcCall(P<ProcCall>),
    ArrayInit(P<ArrayInit>),
    StructInit(P<StructInit>),
    UnaryExpr(P<UnaryExpr>),
    BinaryExpr(P<BinaryExpr>),
}

#[derive(Copy, Clone)]
pub struct Var {
    pub module_access: ModuleAccess,
    pub name: Ident,
    pub access: Option<P<Access>>,
}

#[derive(Copy, Clone)]
pub struct Access {
    pub kind: AccessKind,
    pub next: Option<P<Access>>,
}

#[derive(Copy, Clone)]
pub enum AccessKind {
    Field(Ident),
    Array(Expr),
}

#[derive(Copy, Clone)]
pub struct Enum {
    pub variant: Ident,
}

#[derive(Copy, Clone)]
pub struct Cast {
    pub tt: Type,
    pub expr: Expr,
}

#[derive(Copy, Clone)]
pub struct Sizeof {
    pub tt: Type,
}

#[derive(Copy, Clone)]
pub enum Literal {
    Null,
    Bool(bool),
    Uint(u64, Option<BasicType>),
    Float(f64, Option<BasicType>),
    Char(char),
    String,
}

#[derive(Copy, Clone)]
pub struct ProcCall {
    pub module_access: ModuleAccess,
    pub name: Ident,
    pub input: List<Expr>,
    pub access: Option<P<Access>>,
}

#[derive(Copy, Clone)]
pub struct ArrayInit {
    pub tt: Option<Type>,
    pub input: List<Expr>,
}

#[derive(Copy, Clone)]
pub struct StructInit {
    pub module_access: ModuleAccess,
    pub name: Option<Ident>,
    pub input: List<Expr>,
}

#[derive(Copy, Clone)]
pub struct UnaryExpr {
    pub op: UnaryOp,
    pub rhs: Expr,
}

#[derive(Copy, Clone)]
pub enum UnaryOp {
    Minus,
    BitNot,
    LogicNot,
    AddressOf,
    Dereference,
}

#[derive(Copy, Clone)]
pub struct BinaryExpr {
    pub op: BinaryOp,
    pub lhs: Expr,
    pub rhs: Expr,
}

#[derive(Copy, Clone)]
pub enum BinaryOp {
    LogicAnd,
    LogicOr,
    Less,
    Greater,
    LessEq,
    GreaterEq,
    IsEq,
    NotEq,
    Plus,
    Minus,
    Times,
    Div,
    Mod,
    BitAnd,
    BitOr,
    BitXor,
    Shl,
    Shr,
}
