
struct Block_Info;
struct Block_Checker;

struct Checker
{
	bool check_ast(Ast* ast);
	bool check_types_and_proc_definitions(Ast* ast);
	bool is_proc_in_scope(Ast_Identifier* proc_ident);
	Ast_Procedure_Declaration* get_proc_declaration(Ast_Identifier* proc_ident);
	bool check_enum(Ast_Enum_Declaration* decl);
	bool check_struct(Ast_Struct_Declaration* decl);
	bool check_procedure(Ast_Procedure_Declaration* decl);
	bool check_procedure_block(Ast_Procedure_Declaration* decl);

	bool check_block(Ast_Block* block, Block_Checker* bc, bool is_entry, bool is_inside_loop);
	bool check_if(Ast_If* _if, Block_Checker* bc);
	bool check_else(Ast_Else* _else, Block_Checker* bc);
	bool check_for(Ast_For* _for, Block_Checker* bc);
	bool check_break(Ast_Break* _break, Block_Checker* bc);
	bool check_return(Ast_Return* _return, Block_Checker* bc);
	bool check_continue(Ast_Continue* _continue, Block_Checker* bc);
	std::optional<Type_Info> check_proc_call(Ast_Proc_Call* proc_call, Block_Checker* bc, bool& is_valid);
	bool check_var_assign(Ast_Var_Assign* var_assign, Block_Checker* bc);
	bool check_var_declare(Ast_Var_Declare* var_declare, Block_Checker* bc);
	std::optional<Type_Info> check_access_chain(Ast_Access_Chain* access_chain, Block_Checker* bc);
	std::optional<Type_Info> check_expr(Ast_Expression* expr, Block_Checker* bc);

	std::unordered_map<StringView, Ast_Procedure_Declaration*, StringViewHasher> proc_table;
	Typer typer;
};

struct Block_Info
{
	Ast_Block* block;
	u32 var_count;
	bool is_inside_loop;
};

struct Block_Checker
{
	void block_enter(Ast_Block* block, bool is_inside_loop);
	void block_exit();
	void var_add(const IdentTypePair& ident_type);
	void var_add(const Ast_Identifier& ident, const Ast_Identifier& type);
	bool is_var_declared(const Ast_Identifier& ident);
	bool is_inside_a_loop();
	Ast_Identifier var_get_type(const Ast_Identifier& ident);

	std::vector<Block_Info> block_stack;
	std::vector<IdentTypePair> var_stack; //@Perf this is basic linear search symbol table for the proc block
};

bool Checker::check_ast(Ast* ast)
{
	typer.init_primitive_types();

	if (!check_types_and_proc_definitions(ast)) return false;
	
	bool declarations_valid = true;
	for (auto& decl : ast->structs) if (!check_struct(&decl)) declarations_valid = false;
	for (auto& decl : ast->enums) if (!check_enum(&decl)) declarations_valid = false;
	for (auto& decl : ast->procedures) if (!check_procedure(&decl)) declarations_valid = false;
	if (!declarations_valid) return false;

	bool procedure_blocks_valid = true;
	for (auto& decl : ast->procedures)
	if (!check_procedure_block(&decl)) procedure_blocks_valid = false;
	if (!procedure_blocks_valid) return false;

	return true;
}

bool Checker::check_types_and_proc_definitions(Ast* ast)
{
	for (auto& decl : ast->structs)
	{
		if (typer.is_type_in_scope(&decl.type)) { printf("Struct type redifinition.\n"); return false; }
		typer.add_struct_type(&decl);
	}
	for (auto& decl : ast->enums)
	{
		if (typer.is_type_in_scope(&decl.type)) { printf("Enum type redifinition.\n"); return false; }
		typer.add_enum_type(&decl);
	}
	for (auto& decl : ast->procedures)
	{
		if (is_proc_in_scope(&decl.ident)) { printf("Procedure redifinition"); return false; }
		proc_table.emplace(decl.ident.token.string_value, &decl);
	}
	return true;
}

bool Checker::is_proc_in_scope(Ast_Identifier* proc_ident)
{
	return proc_table.find(proc_ident->token.string_value) != proc_table.end();
}

Ast_Procedure_Declaration* Checker::get_proc_declaration(Ast_Identifier* proc_ident)
{
	return proc_table.at(proc_ident->token.string_value);
}

bool Checker::check_struct(Ast_Struct_Declaration* decl) //@Incomplete allow for multple errors
{
	if (decl->fields.empty()) { printf("Struct must have at least 1 field.\n"); return false; }

	std::unordered_set<StringView, StringViewHasher> names; //@Perf
	for (auto& field : decl->fields)
	{
		if (names.find(field.ident.token.string_value) != names.end()) { printf("Field name redifinition.\n"); return false; }
		if (!typer.is_type_in_scope(&field.type)) { printf("Field type is not in scope.\n"); return false; }
		names.emplace(field.ident.token.string_value);
	}
	return true;
}

bool Checker::check_enum(Ast_Enum_Declaration* decl) //@Incomplete allow for multple errors
{
	if (decl->variants.empty()) { printf("Enum must have at least 1 variant.\n"); return false; }

	std::unordered_set<StringView, StringViewHasher> names; //@Perf
	for (const auto& field : decl->variants)
	{
		if (names.find(field.ident.token.string_value) != names.end()) { printf("Variant name redifinition.\n"); return false; }
		names.emplace(field.ident.token.string_value);
	}
	return true;
}

bool Checker::check_procedure(Ast_Procedure_Declaration* decl)
{
	std::unordered_set<StringView, StringViewHasher> names; //@Perf
	for (auto& param : decl->input_params)
	{
		if (names.find(param.ident.token.string_value) != names.end()) { printf("Procedure parameter name redifinition.\n"); return false; }
		if (!typer.is_type_in_scope(&param.type)) { printf("Procedure parameter type is not in scope.\n"); return false; }
		names.emplace(param.ident.token.string_value);
	}
	if (decl->return_type.has_value() && !typer.is_type_in_scope(&decl->return_type.value())) { printf("Procedure return type is not in scope.\n"); return false; }
	return true;
}

bool Checker::check_procedure_block(Ast_Procedure_Declaration* decl)
{
	Block_Checker bc = {};
	bc.block_enter(decl->block, false);
	for (const auto& param : decl->input_params)
	bc.var_add(param);

	bool result = check_block(decl->block, &bc, true, false);
	return result;
}

void Block_Checker::block_enter(Ast_Block* block, bool is_inside_loop)
{
	if (block_stack.size() > 0 && is_inside_a_loop()) is_inside_loop = true;
	block_stack.emplace_back( Block_Info { block, 0, is_inside_loop });
}

void Block_Checker::block_exit()
{
	Block_Info block = block_stack[block_stack.size() - 1]; //@Perf copying this its small for now
	for (u32 i = 0; i < block.var_count; i++)
		var_stack.pop_back();
	block_stack.pop_back();
}

void Block_Checker::var_add(const IdentTypePair& ident_type)
{
	var_stack.emplace_back(ident_type);
	block_stack[block_stack.size() - 1].var_count += 1;
}

void Block_Checker::var_add(const Ast_Identifier& ident, const Ast_Identifier& type)
{
	var_stack.emplace_back(IdentTypePair { ident, type });
	block_stack[block_stack.size() - 1].var_count += 1;
}

bool Block_Checker::is_var_declared(const Ast_Identifier& ident) //@Perf linear search for now
{
	for (const auto& var : var_stack)
	if (var.ident.token.string_value == ident.token.string_value) return true;
	return false;
}

bool Block_Checker::is_inside_a_loop()
{
	return block_stack[block_stack.size() - 1].is_inside_loop;
}

Ast_Identifier Block_Checker::var_get_type(const Ast_Identifier & ident)
{
	for (const auto& var : var_stack)
	if (var.ident.token.string_value == ident.token.string_value) return var.type;
	printf("FATAL Block_Checker::var_get_type expected to find the var but didnt");
	return {};
}

bool Checker::check_block(Ast_Block* block, Block_Checker* bc, bool is_entry, bool is_inside_loop)
{
	if (!is_entry) 
	bc->block_enter(block, is_inside_loop);

	for (Ast_Statement* stmt : block->statements)
	{
		switch (stmt->tag)
		{
			case Ast_Statement::Tag::If: { if (!check_if(stmt->as_if, bc)) return false; } break;
			case Ast_Statement::Tag::For: { if (!check_for(stmt->as_for, bc)) return false; } break;
			case Ast_Statement::Tag::Break: { if (!check_break(stmt->as_break, bc)) return false; } break;
			case Ast_Statement::Tag::Return: { if (!check_return(stmt->as_return, bc)) return false; } break;
			case Ast_Statement::Tag::Continue: { if (!check_continue(stmt->as_continue, bc)) return false; } break;
			case Ast_Statement::Tag::Proc_Call: 
			{
				bool is_valid = false;
				check_proc_call(stmt->as_proc_call, bc, is_valid);
				if (!is_valid) return false;
			} break;
			case Ast_Statement::Tag::Var_Assign: { if (!check_var_assign(stmt->as_var_assign, bc)) return false; } break;
			case Ast_Statement::Tag::Var_Declare: { if (!check_var_declare(stmt->as_var_declare, bc)) return false; } break;
			default: break;
		}
	}

	bc->block_exit();
	return true;
}

bool Checker::check_if(Ast_If* _if, Block_Checker* bc)
{
	/*
	condition_expr is bool
	condition_expr valid
	block valid
	else is valid or doesnt exist
	*/

	std::optional<Type_Info> expr_type = check_expr(_if->condition_expr, bc);
	if (!expr_type) return false;
	if (!expr_type.value().is_bool())
	{
		printf("Expected conditional expression to evaluate to 'bool' type.\n");
		debug_print_token(_if->token, true, true);
		printf("Expr type: "); typer.debug_print_type_info(&expr_type.value());
		return false;
	}

	if (!check_block(_if->block, bc, false, false)) return false;
	if (_if->_else.has_value() && !check_else(_if->_else.value(), bc)) return false;
	
	return true;
}

bool Checker::check_else(Ast_Else* _else, Block_Checker* bc)
{
	switch (_else->tag)
	{
		case Ast_Else::Tag::If: { if (!check_if(_else->as_if, bc)) return false; } break;
		case Ast_Else::Tag::Block: { if (!check_block(_else->as_block, bc, false, false)) return false; } break;
		default: break;
	}
	return true;
}

bool Checker::check_for(Ast_For* _for, Block_Checker* bc)
{
	/*
	not sure about syntax & semantics yet
	*/

	if (!check_block(_for->block, bc, false, true)) return false;
	return true;
}

bool Checker::check_break(Ast_Break* _break, Block_Checker* bc)
{
	if (!bc->is_inside_a_loop())
	{
		printf("Invalid 'break' outside a for loop.\n");
		debug_print_token(_break->token, true, true);
		return false;
	}
	return true;
}

bool Checker::check_return(Ast_Return* _return, Block_Checker* bc)
{
	/*
	expr matches parent proc return type or lack of type
	*/

	//@Idea put Proc_Declaration* on Ast_Return, which will set during first checker pass

	if (_return->expr.has_value())
	{
		std::optional<Type_Info> expr_type = check_expr(_return->expr.value(), bc);
		if (!expr_type) return false;

		//@Check proc decl return type matches
	}
	else
	{
		//@Check proc decl also has no return type
	}

	return true;
}

bool Checker::check_continue(Ast_Continue* _continue, Block_Checker* bc)
{
	if (!bc->is_inside_a_loop())
	{
		printf("Invalid 'continue' outside a for loop.\n");
		debug_print_token(_continue->token, true, true);
		return false;
	}
	return true;
}

std::optional<Type_Info> Checker::check_proc_call(Ast_Proc_Call* proc_call, Block_Checker* bc, bool& is_valid)
{
	/*
	proc name in scope
	param count and decl param count match
	all param types in scope
	all input expr are valid
	all input expr match param types
	return type in scope
	*/
	
	if (!is_proc_in_scope(&proc_call->ident))
	{
		printf("Calling unknown procedure.\n");
		printf("Procedure: ");
		debug_print_token(proc_call->ident.token, true, true); 
		is_valid = false;
		return {};
	}

	Ast_Procedure_Declaration* proc_decl = get_proc_declaration(&proc_call->ident);

	u64 decl_param_count = proc_decl->input_params.size();
	u64 call_param_count = proc_call->input_expressions.size();
	if (decl_param_count != call_param_count)
	{
		printf("Calling procedure with incorrect number of params. Expected: %llu Calling with: %llu.\n", decl_param_count, call_param_count);
		printf("Procedure: ");
		debug_print_token(proc_call->ident.token, true, true);
		is_valid = false; 
		return {};
	}
	
	u64 counter = 0;
	for (auto& param : proc_decl->input_params)
	{
		if (!typer.is_type_in_scope(&param.type)) //@Redundant function decls should already have verified types, no need to check this
		{
			printf("Calling procedure with param type out of scope.\n");
			printf("Procedure: ");
			debug_print_token(proc_call->ident.token, true, true);
			printf("Type out of scope: ");
			debug_print_token(param.type.token, true, true);
			is_valid = false;
			return {};
		}

		std::optional<Type_Info> param_expr_type = check_expr(proc_call->input_expressions[counter], bc);
		if (!param_expr_type) return {};
		counter += 1;

		Type_Info param_type = typer.get_type_info(&param.type);
		if (!typer.is_type_equals_type(&param_expr_type.value(), &param_type))
		{
			printf("Type mismatch between param type and input expression.\n"); //@Print out type infos to indicate which ones
			printf("Procedure: ");
			debug_print_token(proc_call->ident.token, true, true);
			printf("Expected input type: ");
			typer.debug_print_type_info(&param_type);
			printf("Expression type: ");
			typer.debug_print_type_info(&param_expr_type.value());
			is_valid = false;
			return {};
		}
	}

	if (!proc_decl->return_type.has_value())
	{
		is_valid = true;
		return {};
	}

	if (!typer.is_type_in_scope(&proc_decl->return_type.value()))
	{
		printf("Calling procedure with return type out of scope.\n");
		printf("Procedure: ");
		debug_print_token(proc_call->ident.token, true, true);
		printf("Return type: ");
		debug_print_token(proc_decl->return_type.value().token, true, true);
		is_valid = false;
		return {};
	}

	is_valid = true;
	return typer.get_type_info(&proc_decl->return_type.value());
}

bool Checker::check_var_assign(Ast_Var_Assign* var_assign, Block_Checker* bc)
{
	/*
	access chain must be valid
	expr valid
	AssignOp = : expr evaluates to same type
	AssignOp other: supported by lhs-rhs
	*/

	auto chain_type = check_access_chain(var_assign->access_chain, bc);
	if (!chain_type) return false;
	std::optional<Type_Info> expr_type = check_expr(var_assign->expr, bc);
	if (!expr_type) return false;

	if (var_assign->op == ASSIGN_OP_NONE)
	{
		if (!typer.is_type_equals_type(&chain_type.value(), &expr_type.value()))
		{
			printf("Var assignment type mismatch.\n");
			debug_print_token(var_assign->access_chain->ident.token, true, true); //@Hack just for location, redundant first ident print
			debug_print_access_chain(var_assign->access_chain); //@Only printing first token of the chain
			printf("Var  Type: "); typer.debug_print_type_info(&chain_type.value());
			printf("Expr Type: "); typer.debug_print_type_info(&expr_type.value());
			return false;
		}
	}
	else
	{
		//@Incomplete assign op evaluation (similar to binary ops)
		printf("AssignOps are not supported yet.\n");
		return false;
	}

	return true;
}

bool Checker::check_var_declare(Ast_Var_Declare* var_declare, Block_Checker* bc)
{
	/*
	ident must not be in scope
	[has expr & has type]
	expr valid
	expr evaluates to same type
	[has expr]
	expr valid
	infer var type
	[result]
	existing or inferred type must be in scope
	add ident & type to var_stack
	*/

	if (bc->is_var_declared(var_declare->ident)) 
	{ 
		printf("Can not shadow already declared variable.\n");
		debug_print_token(var_declare->ident.token, true, true);
		return false; 
	}

	Ast_Identifier type_ident = {};
	
	if (var_declare->type.has_value()) //has type, might have expr
	{
		type_ident = var_declare->type.value();

		if (!typer.is_type_in_scope(&type_ident))
		{
			printf("Type is not in scope.\n");
			debug_print_token(type_ident.token, true, true);
			return false;
		}

		if (var_declare->expr.has_value()) //check that expr matches type
		{
			std::optional<Type_Info> expr_type = check_expr(var_declare->expr.value(), bc);
			if (!expr_type) return false;

			Type_Info var_type = typer.get_type_info(&type_ident);
			if (!typer.is_type_equals_type(&expr_type.value(), &var_type))
			{
				printf("Var declaration type mismatch.\n");
				typer.debug_print_type_info(&var_type);
				typer.debug_print_type_info(&expr_type.value());
				return false;
			}
		}
	}
	else //no type, infer from expr
	{
		std::optional<Type_Info> expr_type = check_expr(var_declare->expr.value(), bc);
		if (!expr_type) return false;

		//@Problem no inference possible due to needing AstIdentifier to push var on var_stack
		//cannot go from expr Type_Info to Ast_Identifier
		//will test current setup with all types declared

		printf("Type inference is NOT YET SUPPORTED.\n"); //@Incomplete @Check
		debug_print_token(var_declare->ident.token, true, true);
		return false;
	}

	bc->var_add(var_declare->ident, type_ident);
	
	return true;
}

std::optional<Type_Info> Checker::check_access_chain(Ast_Access_Chain* access_chain, Block_Checker* bc)
{
	/*
	first ident is declared variable
	further idents exist within the type
	return the last type
	*/

	if (!bc->is_var_declared(access_chain->ident))
	{
		printf("Trying to access undeclared variable.\n");
		debug_print_token(access_chain->ident.token, true, true);
		return {};
	}

	Ast_Identifier type = bc->var_get_type(access_chain->ident);
	Type_Info type_info = typer.get_type_info(&type);

	while (true)
	{
		access_chain = access_chain->next;
		if (access_chain == NULL) break;

		if (type_info.tag == TYPE_TAG_STRUCT) //@Perf switch?
		{
			bool found_field = false;

			for (const auto& field : type_info.as_struct_decl->fields)
			{
				if (field.ident.token.string_value == access_chain->ident.token.string_value)
				{
					found_field = true;
					type = field.type;
					type_info = typer.get_type_info(&type);
					break;
				}
			}

			if (!found_field)
			{
				printf("Trying to access struct field which doesnt exist.\n");
				debug_print_token(access_chain->ident.token, true, true);
				return {};
			}
		}
		else if (type_info.tag == TYPE_TAG_ENUM)
		{
			printf("Accessing fields of an Enum is NOT YET SUPPORTED.\n"); //@Incomplete
			debug_print_token(access_chain->ident.token, true, true);
			return {};
		}
		else if (type_info.tag == TYPE_TAG_PRIMITIVE) //@Assuming that primitive types dont have any accesible fields within it
		{
			printf("Trying to access a field of a primitive type.\n");
			debug_print_token(access_chain->ident.token, true, true);
			return {};
		}
	}

	return type_info;
}

std::optional<Type_Info> Checker::check_expr(Ast_Expression* expr, Block_Checker* bc)
{
	Type_Info expr_type = {};

	switch (expr->tag)
	{
		case Ast_Expression::Tag::Term:
		{
			Ast_Term* term = expr->as_term;
			switch (term->tag)
			{
				case Ast_Term::Tag::Literal:
				{
					Ast_Literal lit = term->as_literal;

					if (lit.token.type == TOKEN_NUMBER)
					{
						//@Incomplete only supporting integer token literals, defaulting to i32
						expr_type = typer.get_primitive_type_info(TYPE_I32);
					}
					else if (lit.token.type == TOKEN_STRING)
					{
						//@Incomplete not supporting string literals yet
						expr_type = typer.get_primitive_type_info(TYPE_STRING);
					}
					else if (lit.token.type == TOKEN_BOOL_LITERAL)
					{
						expr_type = typer.get_primitive_type_info(TYPE_BOOL);
					}
				} break;
				case Ast_Term::Tag::Access_Chain:
				{
					auto chain_type = check_access_chain(term->as_access_chain, bc);
					if (!chain_type) return {};

					expr_type = chain_type.value();

				} break;
				case Ast_Term::Tag::Proc_Call:
				{
					bool is_valid = false;
					auto return_type = check_proc_call(term->as_proc_call, bc, is_valid);
					if (!is_valid) return {};
					
					if (!return_type)
					{
						printf("Called procedure as part of the expression must have a return type.\n");
						return {};
					}

					expr_type = return_type.value();

				} break;
			}
		} break;
		case Ast_Expression::Tag::UnaryExpression:
		{
			Ast_Unary_Expression* unary_expr = expr->as_unary_expr;
			auto rhs_type = check_expr(unary_expr->right, bc);
			if (!rhs_type) return {};
			Type_Info type = rhs_type.value();
			UnaryOp op = unary_expr->op;

			switch (op)
			{
				case UNARY_OP_MINUS:
				{
					if (type.is_user_defined()) { printf("Can not apply unary op '-' to user defined type.\n"); return {}; }
					if (type.is_uint()) { printf("Can not apply unary op '-' to unsigned int type.\n"); return {}; }
				} break;
				case UNARY_OP_LOGIC_NOT:
				{
					if (type.is_user_defined()) { printf("Can not apply unary op '!' to user defined type.\n"); return {}; }
					if (!type.is_bool()) { printf("Unary op '!' can only be applied to [bool] expressions.\n"); return {}; }
				} break;
				case UNARY_OP_BITWISE_NOT:
				{
					if (type.is_user_defined()) { printf("Can not apply unary op '~' to user defined type.\n"); return {}; }
					if (!type.is_uint()) { printf("Unary op '!' can only be applied to [unsigned int] expressions.\n"); return {}; }
				} break;
			}

			expr_type = type;

		} break;
		case Ast_Expression::Tag::BinaryExpression:
		{
			Ast_Binary_Expression* bin_expr = expr->as_binary_expr;
			auto lhs_type = check_expr(bin_expr->left, bc);
			if (!lhs_type) return {};
			auto rhs_type = check_expr(bin_expr->left, bc);
			if (!rhs_type) return {};
			BinaryOp op = bin_expr->op;

			Type_Info type_l = lhs_type.value();
			Type_Info type_r = rhs_type.value();
			
			if (type_l.is_user_defined() || type_r.is_user_defined()) { printf("Can not apply binary op to 1 or 2 user defined types.\n"); return {}; }
			if (!typer.is_type_equals_type(&type_l, &type_r)) { printf("Binary expr type missmatch.\n"); return {}; }

			//@Incomplete binary op specific restrictions
			//return bool if comparison op

			expr_type = type_l;

		} break;
	}

	return expr_type;
}
