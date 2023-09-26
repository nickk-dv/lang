#include "parser.h"

#include "debug_printer.h"

bool Parser::init(const char* file_path)
{
	m_arena.init(1024 * 1024);
	return tokenizer.set_input_from_file(file_path);
}

Ast* Parser::parse()
{
	Ast* ast = m_arena.alloc<Ast>();
	tokenizer.tokenize_buffer();

	while (true) 
	{
		Token token = peek();
		switch (token.type)
		{
			case TOKEN_IDENT:
			{
				if (peek(1).type == TOKEN_DOUBLE_COLON)
				{
					switch (peek(2).type)
					{
						case TOKEN_KEYWORD_STRUCT:
						{
							Ast_Struct_Decl* struct_decl = parse_struct_decl();
							if (!struct_decl) return NULL;
							ast->structs.emplace_back(struct_decl);
						} break;
						case TOKEN_KEYWORD_ENUM:
						{
							Ast_Enum_Decl* enum_decl = parse_enum_decl();
							if (!enum_decl) return NULL;
							ast->enums.emplace_back(enum_decl);
						} break;
						case TOKEN_PAREN_START:
						{
							Ast_Proc_Decl* proc_decl = parse_proc_decl();
							if (!proc_decl) return NULL;
							ast->procs.emplace_back(proc_decl);
						} break;
						default:
						{
							error("Expected struct, enum or procedure declaration", 2);
							return NULL;
						}
					}
				}
				else
				{
					error("Expected '::'", 1);
					return NULL;
				}
			} break;
			case TOKEN_EOF: { return ast; }
			default: { error("Expected global declaration identifier"); return NULL; }
		}
	}

	return ast;
}

Ast_Struct_Decl* Parser::parse_struct_decl()
{
	Ast_Struct_Decl* decl = m_arena.alloc<Ast_Struct_Decl>();
	decl->type = Ast_Ident { consume_get() };
	consume(); consume();
	
	if (!try_consume(TOKEN_BLOCK_START)) { error("Expected '{'"); return {}; }
	while (true)
	{
		auto field = try_consume(TOKEN_IDENT);
		if (!field) break;
		if (!try_consume(TOKEN_COLON)) { error("Expected ':' followed by type identifier"); return {}; }
		auto field_type = try_consume_type_ident();
		if (!field_type) { error("Expected type identifier"); return {}; }

		decl->fields.emplace_back(Ast_Ident_Type_Pair { Ast_Ident { field.value() }, field_type.value() });
		if (!try_consume(TOKEN_COMMA)) break;
	}
	if (!try_consume(TOKEN_BLOCK_END)) { error("Expected '}' after struct declaration"); return {}; }

	return decl;
}

Ast_Enum_Decl* Parser::parse_enum_decl()
{
	Ast_Enum_Decl* decl = m_arena.alloc<Ast_Enum_Decl>();
	decl->type = Ast_Ident { consume_get() };
	consume(); consume();
	int constant = 0;

	if (!try_consume(TOKEN_BLOCK_START)) { error("Expected '{'"); return {}; }
	while (true)
	{
		auto variant = try_consume(TOKEN_IDENT);
		if (!variant) break;

		if (try_consume(TOKEN_ASSIGN))
		{
			Token int_lit = peek();
			if (int_lit.type != TOKEN_NUMBER) //@Notice only i32 numbers allowed, dont have any number flags yet
			{
				error("Expected constant integer literal.\n"); return {};
			}
			consume();
			constant = int_lit.integer_value; //@Notice negative not supported by token integer_value
		}

		decl->variants.emplace_back(Ast_Ident_Type_Pair { Ast_Ident { variant.value() }, {} }); //@Notice type is empty token, might support typed enums
		decl->constants.emplace_back(constant);
		constant += 1;
		if (!try_consume(TOKEN_COMMA)) break;
	}
	if (!try_consume(TOKEN_BLOCK_END)) { error("Expected '}' after enum declaration"); return {}; }

	return decl;
}

Ast_Proc_Decl* Parser::parse_proc_decl()
{
	Ast_Proc_Decl* decl = m_arena.alloc<Ast_Proc_Decl>();
	decl->ident = Ast_Ident { consume_get() };
	consume();
	
	if (!try_consume(TOKEN_PAREN_START)) { error("Expected '('"); return {}; }
	while (true)
	{
		auto param = try_consume(TOKEN_IDENT);
		if (!param) break;
		if (!try_consume(TOKEN_COLON)) { error("Expected ':' followed by type identifier"); return {}; }
		auto param_type = try_consume_type_ident();
		if (!param_type) { error("Expected type identifier"); return {}; }

		decl->input_params.emplace_back(Ast_Ident_Type_Pair{ param.value(), param_type.value() });
		if (!try_consume(TOKEN_COMMA)) break;
	}
	if (!try_consume(TOKEN_PAREN_END)) { error("Expected ')'"); return {}; }

	if (try_consume(TOKEN_DOUBLE_COLON))
	{
		auto return_type = try_consume_type_ident();
		if (!return_type) { error("Expected return type identifier"); return {}; }
		decl->return_type = return_type.value();
	}

	Ast_Block* block = parse_block();
	if (block == NULL) return {};
	decl->block = block;

	return decl;
}

Ast_Ident_Chain* Parser::parse_ident_chain()
{
	Ast_Ident_Chain* ident_chain = m_arena.alloc<Ast_Ident_Chain>();
	ident_chain->ident = Ast_Ident{ consume_get() };
	Ast_Ident_Chain* current = ident_chain;

	while (true)
	{
		if (!try_consume(TOKEN_DOT)) break;
		auto ident = try_consume(TOKEN_IDENT);
		if (!ident) { error("Expected identifier"); return NULL; }
		
		current->next = m_arena.alloc<Ast_Ident_Chain>();
		current->next->ident = Ast_Ident { ident.value() };
		current = current->next;
	}

	return ident_chain;
}

Ast_Term* Parser::parse_term()
{
	Ast_Term* term = m_arena.alloc<Ast_Term>();
	Token token = peek();

	switch (token.type)
	{
		case TOKEN_STRING:
		{
			term->tag = Ast_Term::Tag::Literal;
			term->as_literal = Ast_Literal { token };
			consume();
		} break;
		case TOKEN_NUMBER:
		{
			term->tag = Ast_Term::Tag::Literal;
			term->as_literal = Ast_Literal { token };
			consume();
		} break;
		case TOKEN_KEYWORD_TRUE:
		{
			token.type = TOKEN_BOOL_LITERAL;
			token.bool_value = true;
			term->tag = Ast_Term::Tag::Literal;
			term->as_literal = Ast_Literal { token };
			consume();
		} break;
		case TOKEN_KEYWORD_FALSE:
		{
			token.type = TOKEN_BOOL_LITERAL;
			token.bool_value = false;
			term->tag = Ast_Term::Tag::Literal;
			term->as_literal = Ast_Literal { token };
			consume();
		} break;
		case TOKEN_IDENT:
		{
			Token next = peek(1);
			if (next.type == TOKEN_PAREN_START)
			{
				Ast_Proc_Call* proc_call = parse_proc_call();
				if (!proc_call) return NULL;
				term->tag = Ast_Term::Tag::Proc_Call;
				term->as_proc_call = proc_call;
				break;
			}

			Ast_Ident_Chain* ident_chain = parse_ident_chain();
			if (!ident_chain) return NULL;
			term->tag = Ast_Term::Tag::Ident_Chain;
			term->as_ident_chain = ident_chain;
		} break;
		default:
		{
			error("Expected a valid expression term");
			return NULL;
		}
	}

	return term;
}

Ast_Expr* Parser::parse_expr()
{
	Ast_Expr* expr = parse_sub_expr();
	if (!expr) return NULL;
	if (!try_consume(TOKEN_SEMICOLON)) { error("Expected ';' after expression"); return NULL; }
	return expr;
}

Ast_Expr* Parser::parse_sub_expr(u32 min_prec)
{
	Ast_Expr* expr_lhs = parse_primary_expr();
	if (!expr_lhs) return NULL;

	while (true)
	{
		Token token_op = peek();
		BinaryOp op = token_to_binary_op(token_op.type);
		if (op == BINARY_OP_ERROR) break;
		u32 prec = token_binary_op_prec(op);
		if (prec < min_prec) break;
		consume();

		u32 next_min_prec = prec + 1;
		Ast_Expr* expr_rhs = parse_sub_expr(next_min_prec);
		if (expr_rhs == NULL) return NULL;

		Ast_Expr* expr_lhs_copy = m_arena.alloc<Ast_Expr>();
		expr_lhs_copy->tag = expr_lhs->tag;
		expr_lhs_copy->as_term = expr_lhs->as_term;
		expr_lhs_copy->as_binary_expr = expr_lhs->as_binary_expr;

		Ast_Binary_Expr* bin_expr = m_arena.alloc<Ast_Binary_Expr>();
		bin_expr->op = op;
		bin_expr->left = expr_lhs_copy;
		bin_expr->right = expr_rhs;

		expr_lhs->tag = Ast_Expr::Tag::Binary_Expr;
		expr_lhs->as_binary_expr = bin_expr;
	}

	return expr_lhs;
}

Ast_Expr* Parser::parse_primary_expr()
{
	if (try_consume(TOKEN_PAREN_START))
	{
		Ast_Expr* expr = parse_sub_expr();

		if (!try_consume(TOKEN_PAREN_END))
		{
			error("Expected ')'");
			return NULL;
		}

		return expr;
	}

	Token token = peek();
	UnaryOp op = token_to_unary_op(token.type);
	if (op != UNARY_OP_ERROR)
	{
		consume();
		Ast_Expr* right_expr = parse_primary_expr();
		if (!right_expr) return NULL;

		Ast_Unary_Expr* unary_expr = m_arena.alloc<Ast_Unary_Expr>();
		unary_expr->op = op;
		unary_expr->right = right_expr;

		Ast_Expr* expr = m_arena.alloc<Ast_Expr>();
		expr->tag = Ast_Expr::Tag::Unary_Expr;
		expr->as_unary_expr = unary_expr;
		return expr;
	}

	Ast_Term* term = parse_term();
	if (!term) return NULL;

	Ast_Expr* expr = m_arena.alloc<Ast_Expr>();
	expr->tag = Ast_Expr::Tag::Term;
	expr->as_term = term;

	return expr;
}

Ast_Block* Parser::parse_block()
{
	Ast_Block* block = m_arena.alloc<Ast_Block>();

	if (!try_consume(TOKEN_BLOCK_START)) { error("Expected '{' before code block"); return NULL; }
	while (true)
	{
		if (try_consume(TOKEN_BLOCK_END)) return block;

		Ast_Statement* statement = parse_statement();
		if (!statement) return NULL;
		block->statements.emplace_back(statement);
	}
}

Ast_Statement* Parser::parse_statement()
{
	Ast_Statement* statement = m_arena.alloc<Ast_Statement>();
	Token token = peek();

	switch (token.type)
	{
		case TOKEN_KEYWORD_IF:
		{
			statement->tag = Ast_Statement::Tag::If;
			statement->as_if = parse_if();
			if (!statement->as_if) return NULL;
		} break;
		case TOKEN_KEYWORD_FOR:
		{
			statement->tag = Ast_Statement::Tag::For;
			statement->as_for = parse_for();
			if (!statement->as_for) return NULL;
		} break;
		case TOKEN_KEYWORD_BREAK:
		{
			statement->tag = Ast_Statement::Tag::Break;
			statement->as_break = parse_break();
			if (!statement->as_break) return NULL;
		} break;
		case TOKEN_KEYWORD_RETURN:
		{
			statement->tag = Ast_Statement::Tag::Return;
			statement->as_return = parse_return();
			if (!statement->as_return) return NULL;
		} break;
		case TOKEN_KEYWORD_CONTINUE:
		{
			statement->tag = Ast_Statement::Tag::Continue;
			statement->as_continue = parse_continue();
			if (!statement->as_continue) return NULL;
		} break;
		case TOKEN_IDENT:
		{
			Token next = peek(1);

			if (next.type == TOKEN_PAREN_START)
			{
				statement->tag = Ast_Statement::Tag::Proc_Call;
				statement->as_proc_call = parse_proc_call();
				if (!statement->as_proc_call) return NULL;
				if (!try_consume(TOKEN_SEMICOLON)) { error("Expected ';' after procedure call"); return NULL; }
			}
			else if (next.type == TOKEN_COLON)
			{
				statement->tag = Ast_Statement::Tag::Var_Decl;
				statement->as_var_decl = parse_var_decl();
				if (!statement->as_var_decl) return NULL;
			}
			else
			{
				statement->tag = Ast_Statement::Tag::Var_Assign;
				statement->as_var_assign = parse_var_assign();
				if (!statement->as_var_assign) return NULL;
			}
		} break;
		default: { error("Expected valid statement or '}' after code block"); return NULL; }
	}
	
	return statement;
}

Ast_If* Parser::parse_if()
{
	Ast_If* _if = m_arena.alloc<Ast_If>();
	_if->token = consume_get();

	Ast_Expr* expr = parse_sub_expr();
	if (!expr) return NULL;
	_if->condition_expr = expr;

	Ast_Block* block = parse_block();
	if (!block) return NULL;
	_if->block = block;

	Token next = peek();
	if (next.type == TOKEN_KEYWORD_ELSE)
	{
		Ast_Else* _else = parse_else();
		if (!_else) return NULL;
		_if->_else = _else;
	}

	return _if;
}

Ast_Else* Parser::parse_else()
{
	Ast_Else* _else = m_arena.alloc<Ast_Else>();
	_else->token = consume_get();
	Token next = peek();

	if (next.type == TOKEN_KEYWORD_IF)
	{
		Ast_If* _if = parse_if();
		if (!_if) return NULL;
		_else->tag = Ast_Else::Tag::If;
		_else->as_if = _if;
	}
	else if (next.type == TOKEN_BLOCK_START)
	{
		Ast_Block* block = parse_block();
		if (!block) return NULL;
		_else->tag = Ast_Else::Tag::Block;
		_else->as_block = block;
	}
	else { error("Expected 'if' or code block '{ ... }'"); return NULL; }

	return _else;
}

Ast_For* Parser::parse_for()
{
	Ast_For* _for = m_arena.alloc<Ast_For>();
	_for->token = consume_get();
	Token curr = peek();
	Token next = peek(1);

	//infinite loop
	if (curr.type == TOKEN_BLOCK_START)
	{
		Ast_Block* block = parse_block();
		if (!block) return NULL;
		_for->block = block;
		
		return _for;
	}

	//optional var declaration
	if (curr.type == TOKEN_IDENT && next.type == TOKEN_COLON)
	{
		Ast_Var_Decl* var_decl = parse_var_decl();
		if (!var_decl) return NULL;
		_for->var_decl = var_decl;
	}

	//conditional expr
	Ast_Expr* condition_expr = parse_sub_expr();
	if (!condition_expr) { error("Expected conditional expression"); return NULL; }
	_for->condition_expr = condition_expr;

	//optional post expr
	if (try_consume(TOKEN_SEMICOLON))
	{
		Ast_Var_Assign* var_assignment = parse_var_assign();
		if (!var_assignment) return NULL;
		_for->var_assign = var_assignment;
	}

	Ast_Block* block = parse_block();
	if (!block) return NULL;
	_for->block = block;

	return _for;
}

Ast_Break* Parser::parse_break()
{
	Ast_Break* _break = m_arena.alloc<Ast_Break>();
	_break->token = consume_get();

	if (!try_consume(TOKEN_SEMICOLON)) { error("Expected ';' after 'break'"); return NULL; }
	return _break;
}

Ast_Return* Parser::parse_return()
{
	Ast_Return* _return = m_arena.alloc<Ast_Return>();
	_return->token = consume_get();

	if (try_consume(TOKEN_SEMICOLON)) return _return;

	Ast_Expr* expr = parse_expr();
	if (!expr) return NULL;
	_return->expr = expr;
	return _return;
}

Ast_Continue* Parser::parse_continue()
{
	Ast_Continue* _continue = m_arena.alloc<Ast_Continue>();
	_continue->token = consume_get();

	if (!try_consume(TOKEN_SEMICOLON)) { error("Expected ';' after 'continue'"); return NULL; }
	return _continue;
}

Ast_Proc_Call* Parser::parse_proc_call()
{
	Ast_Proc_Call* proc_call = m_arena.alloc<Ast_Proc_Call>();
	proc_call->ident = Ast_Ident { consume_get() };
	consume();

	while (true)
	{
		if (try_consume(TOKEN_PAREN_END)) return proc_call;

		Ast_Expr* param_expr = parse_sub_expr();
		if (!param_expr) return NULL;
		proc_call->input_exprs.emplace_back(param_expr);

		if (!try_consume(TOKEN_COMMA)) break;
	}

	error("Parse error in parse_proc_call");
	return NULL;
}

Ast_Var_Decl* Parser::parse_var_decl()
{
	Ast_Var_Decl* var_decl = m_arena.alloc<Ast_Var_Decl>();
	var_decl->ident = Ast_Ident { consume_get() };
	consume();

	auto type = try_consume_type_ident();
	if (type) var_decl->type = type.value();

	bool default_init = !try_consume(TOKEN_ASSIGN);
	if (default_init)
	{
		bool has_semicolon = try_consume(TOKEN_SEMICOLON).has_value();
		if (type && has_semicolon) return var_decl;

		if (!type) error("Expected specified type for default initialized variable");
		else if (!has_semicolon) error("Expected ';' after variable declaration");
		return NULL;
	}

	Ast_Expr* expr = parse_expr();
	if (!expr) return NULL;
	var_decl->expr = expr;
	return var_decl;
}

Ast_Var_Assign* Parser::parse_var_assign()
{
	Ast_Var_Assign* var_assign = m_arena.alloc<Ast_Var_Assign>();
	
	Ast_Ident_Chain* ident_chain = parse_ident_chain();
	if (!ident_chain) return NULL;
	var_assign->ident_chain = ident_chain;

	Token token = peek();
	AssignOp op = token_to_assign_op(token.type);
	if (op == ASSIGN_OP_ERROR) { error("Expected assignment operator"); return NULL; }
	var_assign->op = op;
	consume();

	Ast_Expr* expr = parse_expr();
	if (!expr) return NULL;
	var_assign->expr = expr;
	return var_assign;
}

Token Parser::peek(u32 offset)
{
	return tokenizer.tokens[tokenizer.peek_index + offset];
}

std::optional<Token> Parser::try_consume(TokenType token_type)
{
	Token token = peek();
	if (token.type == token_type)
	{
		consume();
		return token;
	}
	return {};
}

std::optional<Ast_Ident> Parser::try_consume_type_ident()
{
	Token token = peek();
	if (token.type == TOKEN_IDENT || (token.type >= TOKEN_TYPE_I8 && token.type <= TOKEN_TYPE_STRING))
	{
		consume();
		return Ast_Ident { token };
	}
	return {};
}

Token Parser::consume_get()
{
	Token token = peek();
	consume();
	return token;
}

void Parser::consume()
{
	tokenizer.peek_index += 1;
	if (tokenizer.peek_index >= (tokenizer.TOKENIZER_BUFFER_SIZE - tokenizer.TOKENIZER_LOOKAHEAD))
	{
		tokenizer.peek_index = 0; 
		tokenizer.tokenize_buffer();
	}
}

void Parser::error(const char* message, u32 peek_offset)
{
	printf("%s.\n", message);
	debug_print_token(peek(peek_offset), true, true);
}
