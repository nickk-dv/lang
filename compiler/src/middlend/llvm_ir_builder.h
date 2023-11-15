#ifndef LLVM_IR_BUILDER_H
#define LLVM_IR_BUILDER_H

#include "llvm_ir_builder_context.h"

LLVMModuleRef build_module(Ast_Program* program);

static IR_Terminator build_stmt_block(IR_Builder_Context* bc, Ast_Stmt_Block* block, IR_Block_Flags flags);
static void build_stmt_if(IR_Builder_Context* bc, Ast_Stmt_If* _if, Basic_Block cont_block);
static void build_stmt_for(IR_Builder_Context* bc, Ast_Stmt_For* _for);
static void build_stmt_defer(IR_Builder_Context* bc, IR_Terminator terminator);
static void build_stmt_switch(IR_Builder_Context* bc, Ast_Stmt_Switch* _switch);
static void build_stmt_var_decl(IR_Builder_Context* bc, Ast_Stmt_Var_Decl* var_decl);
static void build_stmt_var_assign(IR_Builder_Context* bc, Ast_Stmt_Var_Assign* var_assign);
static void build_global_var(IR_Builder_Context* bc, Ast_Info_IR_Global* global_info);
static Value build_default_struct(IR_Builder_Context* bc, Ast_Info_IR_Struct* struct_info);
static Value build_default_value(IR_Builder_Context* bc, Ast_Type type);
static Value build_proc_call(IR_Builder_Context* bc, Ast_Proc_Call* proc_call, IR_Proc_Call_Flags flags);
static Value build_expr(IR_Builder_Context* bc, Ast_Expr* expr, bool unary_address = false);
static Value build_expr_auto_cast(IR_Builder_Context* bc, Ast_Expr* expr, Value value);
static Value build_term(IR_Builder_Context* bc, Ast_Term* term, bool unary_address = false);
static IR_Access_Info build_var(IR_Builder_Context* bc, Ast_Var* var);
static IR_Access_Info build_access(IR_Builder_Context* bc, option<Ast_Access*> access_option, Value ptr, Ast_Type ast_type);
static Value build_unary_expr(IR_Builder_Context* bc, Ast_Unary_Expr* unary_expr);
static Value build_binary_expr(IR_Builder_Context* bc, Ast_Binary_Expr* binary_expr);
static Value build_folded_expr(Ast_Folded_Expr folded_expr);
static char* ident_to_cstr(Ast_Ident& ident);
static Type type_from_basic_type(BasicType basic_type);
static Type type_from_ast_type(IR_Builder_Context* bc, Ast_Type type);

#endif
