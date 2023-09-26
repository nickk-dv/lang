#ifndef PARSER_H
#define PARSER_H

#include "common.h"
#include "tokenizer.h"
#include "ast.h"

struct Parser
{
public:
	bool init(const char* file_path);
	Ast* parse();

private:
	Ast_Struct_Decl* parse_struct_decl();
	Ast_Enum_Decl* parse_enum_decl();
	Ast_Proc_Decl* parse_proc_decl();
	Ast_Type* parse_type();
	Ast_Array* parse_array();
	Ast_Ident_Chain* parse_ident_chain();
	Ast_Term* parse_term();
	Ast_Expr* parse_expr();
	Ast_Expr* parse_sub_expr(u32 min_prec = 0);
	Ast_Expr* parse_primary_expr();
	Ast_Block* parse_block();
	Ast_Statement* parse_statement();
	Ast_If* parse_if();
	Ast_Else* parse_else();
	Ast_For* parse_for();
	Ast_Break* parse_break();
	Ast_Return* parse_return();
	Ast_Continue* parse_continue();
	Ast_Proc_Call* parse_proc_call();
	Ast_Var_Decl* parse_var_decl();
	Ast_Var_Assign* parse_var_assign();
	Token peek(u32 offset = 0);
	std::optional<Token> try_consume(TokenType token_type);
	Token consume_get();
	void consume();
	void error(const char* message, u32 peek_offset = 0);

	Arena m_arena;
	Tokenizer tokenizer;
};

#endif
