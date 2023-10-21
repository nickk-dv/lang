#include "checker.h"

#include "debug_printer.h"

//@Design currently checking is split into 3 stages
// 1. import paths & decl uniqueness checks
// 2. decl signature validity checks
// 3. proc block cfg & type and other semantics checks 

//@Perf general issues with re-hashing Ast_Ident every time
void check_decl_uniqueness(Checker_Context* cc, Module_Map& modules)
{
	Ast* ast = cc->ast;
	Ast_Program* program = cc->program;

	HashSet<Ast_Ident, u32, match_ident> symbol_table(256);
	ast->import_table.init(64);
	ast->struct_table.init(64);
	ast->enum_table.init(64);
	ast->proc_table.init(64);

	for (Ast_Import_Decl* decl : ast->imports)
	{
		Ast_Ident ident = decl->alias;
		option<Ast_Ident> key = symbol_table.find_key(ident, hash_ident(ident));
		if (!key)
		{ 
			symbol_table.add(ident, hash_ident(ident)); 
			ast->import_table.add(ident, decl, hash_ident(ident)); 
		}
		else { err_set; error_pair("Symbol already declared", "Import", ident, "Symbol", key.value()); }
	}

	for (Ast_Import_Decl* decl : ast->imports)
	{
		if (modules.find(decl->file_path.token.string_literal_value) != modules.end())
		{
			Ast* import_ast = modules.at(decl->file_path.token.string_literal_value);
			decl->import_ast = import_ast;
		}
		else { err_set; error("Import path not found", decl->alias); }
	}

	for (Ast_Use_Decl* decl : ast->uses)
	{
		Ast_Ident ident = decl->alias;
		option<Ast_Ident> key = symbol_table.find_key(ident, hash_ident(ident));
		if (!key) symbol_table.add(ident, hash_ident(ident));
		else { err_set; error_pair("Symbol already declared", "Use", ident, "Symbol", key.value()); }
	}

	for (Ast_Struct_Decl* decl : ast->structs)
	{
		Ast_Ident ident = decl->ident;
		option<Ast_Ident> key = symbol_table.find_key(ident, hash_ident(ident));
		if (!key) 
		{
			symbol_table.add(ident, hash_ident(ident));
			ast->struct_table.add(ident, Ast_Struct_Info { (u32)program->structs.size(), decl }, hash_ident(ident));

			Ast_Struct_IR_Info struct_info = {};
			struct_info.struct_decl = decl;
			program->structs.emplace_back(struct_info);
		}
		else { err_set; error_pair("Symbol already declared", "Struct", ident, "Symbol", key.value()); }
	}

	for (Ast_Enum_Decl* decl : ast->enums)
	{
		Ast_Ident ident = decl->ident;
		option<Ast_Ident> key = symbol_table.find_key(ident, hash_ident(ident));
		if (!key) 
		{ 
			symbol_table.add(ident, hash_ident(ident));
			ast->enum_table.add(ident, Ast_Enum_Info { (u32)program->enums.size(), decl }, hash_ident(ident));

			Ast_Enum_IR_Info enum_info = {};
			enum_info.enum_decl = decl;
			program->enums.emplace_back(enum_info);
		}
		else { err_set; error_pair("Symbol already declared", "Enum", ident, "Symbol", key.value()); }
	}

	for (Ast_Proc_Decl* decl : ast->procs)
	{
		Ast_Ident ident = decl->ident;
		option<Ast_Ident> key = symbol_table.find_key(ident, hash_ident(ident));
		if (!key) 
		{ 
			symbol_table.add(ident, hash_ident(ident));
			ast->proc_table.add(ident, Ast_Proc_Info { (u32)program->procedures.size(), decl }, hash_ident(ident));

			Ast_Proc_IR_Info proc_info = {};
			proc_info.proc_decl = decl;
			program->procedures.emplace_back(proc_info);
		}
		else { err_set; error_pair("Symbol already declared", "Procedure", ident, "Symbol", key.value()); }
	}

	//@Low priority
	//@Rule todo: cant import same thing under multiple names
	//@Rule todo: cant import same type or procedure under multiple names
}

void check_decls(Checker_Context* cc)
{
	Ast* ast = cc->ast;

	// Find and add use symbols to current scope
	for (Ast_Use_Decl* use_decl : ast->uses)
	{
		Ast* import_ast = try_import(cc, { use_decl->import });
		if (import_ast == NULL)
		{
			err_set;
			continue;
		}

		Ast_Ident alias = use_decl->alias;
		Ast_Ident symbol = use_decl->symbol;
		option<Ast_Struct_Info> struct_decl = import_ast->struct_table.find(symbol, hash_ident(symbol));
		if (struct_decl) { ast->struct_table.add(alias, struct_decl.value(), hash_ident(alias)); continue; }
		option<Ast_Enum_Info> enum_decl = import_ast->enum_table.find(symbol, hash_ident(symbol));
		if (enum_decl) { ast->enum_table.add(alias, enum_decl.value(), hash_ident(alias)); continue; }
		option<Ast_Proc_Info> proc_decl = import_ast->proc_table.find(symbol, hash_ident(symbol));
		if (proc_decl) { ast->proc_table.add(alias, proc_decl.value(), hash_ident(alias)); continue; }
		
		err_set;
		error("Use symbol isnt found in imported namespace", symbol);
	}

	for (Ast_Struct_Decl* struct_decl : ast->structs) check_struct_decl(cc, struct_decl);
	for (Ast_Enum_Decl* enum_decl : ast->enums) check_enum_decl(cc, enum_decl);
	for (Ast_Proc_Decl* proc_decl : ast->procs) check_proc_decl(cc, proc_decl);
}

void check_main_proc(Checker_Context* cc)
{
	Ast_Ident ident = {};
	char name_arr[4] = { 'm', 'a', 'i', 'n' };
	ident.str.data = (u8*)name_arr;
	ident.str.count = 4;

	option<Ast_Proc_Info> proc_meta = find_proc(cc->ast, ident);
	if (!proc_meta)
	{
		err_set;
		printf("Main procedure not found. Make sure src/main file has 'main :: () :: i32 { ... }' declared\n\n");
		return;
	}
	Ast_Proc_Decl* proc_decl = proc_meta.value().proc_decl;
	proc_decl->is_main = true;
	
	if (proc_decl->is_external)
	{
		err_set;
		error("Main procedure cannot be specified as external '@'. You must define a main procedure block", proc_decl->ident);
	}

	if (proc_decl->is_variadic)
	{
		err_set;
		error("Main procedure cannot be variadic. Remove '..' from the parameter list", proc_decl->ident);
	}

	if (proc_decl->input_params.size() != 0)
	{
		err_set;
		error("Main procedure cannot have any input parameters. Command line arguments can be accessed using core library", proc_decl->ident);
	}

	if (!proc_decl->return_type)
	{
		err_set;
		error("Main procedure must have i32 return type", proc_decl->ident);
	}
	else
	{
		Ast_Type return_type = proc_decl->return_type.value();
		if (return_type.tag != Ast_Type::Tag::Basic || return_type.as_basic != BasicType::I32)
		{
			err_set;
			printf("Main procedure must have i32 return type\n");
			debug_print_ident(proc_decl->ident);
			printf("\n");
		}
	}
}

void check_program(Checker_Context* cc)
{
	Ast_Program* program = cc->program;

	struct Visit_State
	{
		Ast_Struct_Decl* struct_decl;
		u32 struct_id;
		u32 field_id;
		u32 field_count;
	};

	for (u32 i = 0; i < program->structs.size(); i += 1)
	{
		u32 search_target = i;
		bool found = false;
		std::vector<Visit_State> visit_stack;
		std::vector<u32> visited;

		Ast_Struct_IR_Info meta = program->structs[search_target];
		Visit_State visit = Visit_State { meta.struct_decl, search_target, 0, (u32)meta.struct_decl->fields.size() };
		visit_stack.emplace_back(visit);
		visited.emplace_back(visit.struct_id);

		while (!visit_stack.empty() && !found)
		{
			bool new_visit = false;

			u32 curr_id = (u32)visit_stack.size() - 1;
			Visit_State& state = visit_stack[curr_id];
			while (state.field_id < state.field_count)
			{
				Ast_Type type = state.struct_decl->fields[state.field_id].type;
				if (type_kind(cc, type) == Type_Kind::Struct)
				{
					u32 struct_id = type.as_struct.struct_id;
					if (struct_id == search_target)
					{ 
						found = true; 
						break; 
					}

					bool already_visited = std::find(visited.begin(), visited.end(), struct_id) != visited.end();
					if (!already_visited)
					{
						Ast_Struct_IR_Info visit_meta = program->structs[struct_id];
						Visit_State visit2 = { visit_meta.struct_decl, struct_id, 0, (u32)visit_meta.struct_decl->fields.size() };
						visit_stack.push_back(visit2);
						visited.push_back(struct_id);
						new_visit = true;
						break;
					}
				}
				state.field_id += 1;
			}

			if (!new_visit)
			{
				if (found) break;
				else visit_stack.pop_back();
			}
		}

		if (found)
		{
			err_set;
			printf("Found struct with infinite size: ");
			Visit_State err_visit = visit_stack[0];
			debug_print_ident(err_visit.struct_decl->ident, true, true);
			printf("Field access path: ");
			debug_print_ident(err_visit.struct_decl->fields[err_visit.field_id].ident, false, false);
			for (u32 k = 1; k < visit_stack.size(); k += 1)
			{
				printf(".");
				err_visit = visit_stack[k];
				debug_print_ident(err_visit.struct_decl->fields[err_visit.field_id].ident, false, false);
			}
			printf("\n");
			printf("Hint: struct cannot directly store intance of itself. Use pointer for indirection.\n");
			printf("\n");
		}
	}
}

void check_ast(Checker_Context* cc)
{
	for (Ast_Proc_Decl* proc_decl : cc->ast->procs)
	{
		if (proc_decl->is_external) continue;

		//@Notice this doesnt correctly handle if else on top level, which may allow all paths to return
		Terminator terminator = check_block_cfg(cc, proc_decl->block, false, false);
		if (terminator != Terminator::Return)
		{
			if (proc_decl->return_type)
			{
				err_set;
				error("Not all control flow paths return value", proc_decl->ident);
			}
		}
		
		//@Notice need to add input variables to block stack
		checker_context_block_reset(cc, proc_decl);
		checker_context_block_add(cc);
		for (Ast_Ident_Type_Pair& param : proc_decl->input_params)
		{
			if (checker_context_block_contains_var(cc, param.ident))
			{
				err_set;
				error("Input parameter with same name is already exists", param.ident);
			}
			else checker_context_block_add_var(cc, param.ident, param.type);
		}
		check_block(cc, proc_decl->block, Checker_Block_Flags::Already_Added);
	}
}

void check_struct_decl(Checker_Context* cc, Ast_Struct_Decl* struct_decl)
{
	HashSet<Ast_Ident, u32, match_ident> name_set(16); //@Perf possibly move out from function and re-use hash_set with reset()

	if (struct_decl->fields.empty())
	{
		err_set;
		error("Struct must have at least 1 field", struct_decl->ident);
		return;
	}

	for (Ast_Ident_Type_Pair& field : struct_decl->fields)
	{
		option<Ast_Ident> name = name_set.find_key(field.ident, hash_ident(field.ident));
		if (!name) name_set.add(field.ident, hash_ident(field.ident));
		else { err_set; error("Duplicate struct field identifier", field.ident); }

		check_type_signature(cc, &field.type);
	}
}

void check_enum_decl(Checker_Context* cc, Ast_Enum_Decl* enum_decl)
{
	HashSet<Ast_Ident, u32, match_ident> name_set(16); //@Perf read check_struct_decl comment

	BasicType type = enum_decl->basic_type;
	Ast_Type enum_type = type_from_basic(type);
	Type_Context type_context = { enum_type, true };

	if (!token_basic_type_is_integer(type))
	{
		err_set;
		error("Cannot use non integer type in enum declaration", enum_decl->ident);
	}

	for (Ast_Enum_Variant& variant : enum_decl->variants)
	{
		option<Ast_Ident> name = name_set.find_key(variant.ident, hash_ident(variant.ident));
		if (!name) name_set.add(variant.ident, hash_ident(variant.ident));
		else { err_set; error("Duplicate enum variant identifier", variant.ident); }

		option<Ast_Type> type = check_expr(cc, &type_context, variant.const_expr);
		if (type && !match_type(cc, enum_type, type.value()))
		{
			err_set;
			printf("Variant type doesnt match enum type:\n");
			debug_print_ident(variant.ident);
			printf("Expected: "); debug_print_type(enum_type); printf("\n");
			printf("Got:      "); debug_print_type(type.value()); printf("\n\n");
		}
	}
	
	//@Todo check circular enum dependency when enums as constants are supported
	//@Todo check constant value overlap
}

void check_proc_decl(Checker_Context* cc, Ast_Proc_Decl* proc_decl)
{
	HashSet<Ast_Ident, u32, match_ident> name_set(16); //@Perf read check_struct_decl comment
	
	for (Ast_Ident_Type_Pair& param : proc_decl->input_params)
	{
		check_type_signature(cc, &param.type);
		
		option<Ast_Ident> name = name_set.find_key(param.ident, hash_ident(param.ident));
		if (!name) name_set.add(param.ident, hash_ident(param.ident));
		else { err_set; error("Duplicate procedure input parameter identifier", param.ident); }
	}

	if (proc_decl->return_type)
	{
		check_type_signature(cc, &proc_decl->return_type.value());
	}
}

Ast* try_import(Checker_Context* cc, option<Ast_Ident> import)
{
	if (!import) return cc->ast;

	Ast_Ident import_ident = import.value();
	option<Ast_Import_Decl*> import_decl = cc->ast->import_table.find(import_ident, hash_ident(import_ident));
	if (!import_decl)
	{
		err_set;
		error("Import module not found", import_ident);
		return NULL;
	}

	return import_decl.value()->import_ast;
}

option<Ast_Struct_Info> find_struct(Ast* target_ast, Ast_Ident ident)
{
	return target_ast->struct_table.find(ident, hash_ident(ident));
}

option<Ast_Enum_Info> find_enum(Ast* target_ast, Ast_Ident ident)
{
	return target_ast->enum_table.find(ident, hash_ident(ident));
}

option<Ast_Proc_Info> find_proc(Ast* target_ast, Ast_Ident ident)
{
	return target_ast->proc_table.find(ident, hash_ident(ident));
}

option<u32> find_enum_variant(Ast_Enum_Decl* enum_decl, Ast_Ident ident)
{
	u32 count = 0;
	for (Ast_Enum_Variant& variant : enum_decl->variants)
	{
		if (match_ident(variant.ident, ident)) return count;
		count += 1;
	}
	return {};
}

option<u32> find_struct_field(Ast_Struct_Decl* struct_decl, Ast_Ident ident)
{
	u32 count = 0;
	for (Ast_Ident_Type_Pair& field : struct_decl->fields)
	{
		if (match_ident(field.ident, ident)) return count;
		count += 1;
	}
	return {};
}

Terminator check_block_cfg(Checker_Context* cc, Ast_Block* block, bool is_loop, bool is_defer)
{
	Terminator terminator = Terminator::None;

	for (Ast_Statement* statement : block->statements)
	{
		if (terminator != Terminator::None)
		{
			err_set;
			printf("Unreachable statement:\n");
			debug_print_statement(statement, 0);
			printf("\n");
			break;
		}

		switch (statement->tag)
		{
		case Ast_Statement::Tag::If:
		{
			check_if_cfg(cc, statement->as_if, is_loop, is_defer);
		} break;
		case Ast_Statement::Tag::For: 
		{
			check_block_cfg(cc, statement->as_for->block, true, is_defer);
		} break;
		case Ast_Statement::Tag::Block: 
		{
			terminator = check_block_cfg(cc, statement->as_block, is_loop, is_defer);
		} break;
		case Ast_Statement::Tag::Defer:
		{
			if (is_defer)
			{
				err_set;
				printf("Nested defer blocks are not allowed:\n");
				debug_print_token(statement->as_defer->token, true, true);
				printf("\n");
			}
			else check_block_cfg(cc, statement->as_defer->block, false, true);
		} break;
		case Ast_Statement::Tag::Break:
		{
			if (!is_loop)
			{
				if (is_defer) { err_set; printf("Break statement inside defer block is not allowed:\n"); }
				else { err_set; printf("Break statement outside a loop:\n"); }
				debug_print_token(statement->as_break->token, true, true);
				printf("\n");
			}
			else terminator = Terminator::Break;
		} break;
		case Ast_Statement::Tag::Return:
		{
			if (is_defer)
			{
				err_set;
				printf("Defer block cant contain 'return' statements:\n");
				debug_print_token(statement->as_defer->token, true, true);
				printf("\n");
			}
			else terminator = Terminator::Return;
		} break;
		case Ast_Statement::Tag::Switch:
		{
			check_switch_cfg(cc, statement->as_switch, is_loop, is_defer);
		} break;
		case Ast_Statement::Tag::Continue:
		{
			if (!is_loop)
			{
				if (is_defer) { err_set; printf("Continue statement inside defer block is not allowed:\n"); }
				else { err_set; printf("Continue statement outside a loop:\n"); }
				debug_print_token(statement->as_continue->token, true, true);
				printf("\n");
			}
			else terminator = Terminator::Continue;
		} break;
		case Ast_Statement::Tag::Proc_Call: break;
		case Ast_Statement::Tag::Var_Decl: break;
		case Ast_Statement::Tag::Var_Assign: break;
		}
	}

	return terminator;
}

void check_if_cfg(Checker_Context* cc, Ast_If* _if, bool is_loop, bool is_defer)
{
	check_block_cfg(cc, _if->block, is_loop, is_defer);
	
	if (_if->_else)
	{
		Ast_Else* _else = _if->_else.value();
		if (_else->tag == Ast_Else::Tag::If)
			check_if_cfg(cc, _else->as_if, is_loop, is_defer);
		else check_block_cfg(cc, _else->as_block, is_loop, is_defer);
	}
}

void check_switch_cfg(Checker_Context* cc, Ast_Switch* _switch, bool is_loop, bool is_defer)
{
	for (Ast_Switch_Case& _case : _switch->cases)
	{
		if (_case.block) check_block_cfg(cc, _case.block.value(), is_loop, is_defer);
	}
}

//@Todo store proc context in Block_Stack and 
//type check return type with proc decl return type, create check_return()
static void check_block(Checker_Context* cc, Ast_Block* block, Checker_Block_Flags flags)
{
	if (flags != Checker_Block_Flags::Already_Added) checker_context_block_add(cc);

	for (Ast_Statement* statement: block->statements)
	{
		switch (statement->tag)
		{
		case Ast_Statement::Tag::If: check_if(cc, statement->as_if); break;
		case Ast_Statement::Tag::For: check_for(cc, statement->as_for); break;
		case Ast_Statement::Tag::Block: check_block(cc, statement->as_block, Checker_Block_Flags::None); break;
		case Ast_Statement::Tag::Defer: check_block(cc, statement->as_defer->block, Checker_Block_Flags::None); break;
		case Ast_Statement::Tag::Break: break;
		case Ast_Statement::Tag::Return: check_return(cc, statement->as_return); break;
		case Ast_Statement::Tag::Switch: check_switch(cc, statement->as_switch); break;
		case Ast_Statement::Tag::Continue: break;
		case Ast_Statement::Tag::Proc_Call: check_proc_call(cc, statement->as_proc_call, Checker_Proc_Call_Flags::In_Statement); break;
		case Ast_Statement::Tag::Var_Decl: check_var_decl(cc, statement->as_var_decl); break;
		case Ast_Statement::Tag::Var_Assign: check_var_assign(cc, statement->as_var_assign); break;
		}
	}

	checker_context_block_pop_back(cc);
}

void check_if(Checker_Context* cc, Ast_If* _if)
{
	option<Ast_Type> type = check_expr(cc, {}, _if->condition_expr);
	if (type && type_kind(cc, type.value()) != Type_Kind::Bool)
	{
		err_set;
		printf("Expected conditional expression to be of type 'bool':\n");
		debug_print_token(_if->token, true, true);
		printf("Got: "); debug_print_type(type.value());
		printf("\n\n");
	}

	check_block(cc, _if->block, Checker_Block_Flags::None);

	if (_if->_else)
	{
		Ast_Else* _else = _if->_else.value();
		if (_else->tag == Ast_Else::Tag::If)
			check_if(cc, _else->as_if);
		else check_block(cc, _else->as_block, Checker_Block_Flags::None);
	}
}

void check_for(Checker_Context* cc, Ast_For* _for)
{
	checker_context_block_add(cc);
	if (_for->var_decl) check_var_decl(cc, _for->var_decl.value());
	if (_for->var_assign) check_var_assign(cc, _for->var_assign.value());

	if (_for->condition_expr)
	{
		option<Ast_Type> type = check_expr(cc, {}, _for->condition_expr.value());
		if (type && type_kind(cc, type.value()) != Type_Kind::Bool)
		{
			err_set;
			printf("Expected conditional expression to be of type 'bool':\n");
			debug_print_token(_for->token, true, true);
			printf("Got: "); debug_print_type(type.value());
			printf("\n\n");
		}
	}

	check_block(cc, _for->block, Checker_Block_Flags::Already_Added);
}

void check_return(Checker_Context* cc, Ast_Return* _return)
{
	Ast_Proc_Decl* curr_proc = cc->curr_proc;

	if (_return->expr)
	{
		if (curr_proc->return_type)
		{
			Ast_Type ret_type = curr_proc->return_type.value();
			Type_Context type_context = { ret_type, false };
			option<Ast_Type> expr_type = check_expr(cc, &type_context, _return->expr.value());
			if (!expr_type) return;
			
			if (!match_type(cc, ret_type, expr_type.value()))
			{
				err_set;
				printf("Return type doesnt match procedure declaration:\n");
				debug_print_token(_return->token, true, true);
				printf("Expected: "); debug_print_type(ret_type); printf("\n");
				printf("Got: "); debug_print_type(expr_type.value()); printf("\n\n");
			}
		}
		else
		{
			err_set;
			printf("Return type doesnt match procedure declaration:\n");
			debug_print_token(_return->token, true, true);
			printf("Expected no return expression");
			printf("\n\n");
		}
	}
	else
	{
		if (curr_proc->return_type)
		{
			err_set;
			Ast_Type ret_type = curr_proc->return_type.value();
			printf("Return type doesnt match procedure declaration:\n");
			debug_print_token(_return->token, true, true);
			printf("Expected type: "); debug_print_type(ret_type); printf("\n");
			printf("Got no return expression");
			printf("\n\n");
		}
	}
}

void check_switch(Checker_Context* cc, Ast_Switch* _switch)
{
	//@Very unfinished. Share const expr unique pool logic with EnumVariants
	
	//@Check if switch is exaustive with enums / integers, require
	//add default or discard like syntax _ for default case
	//@Check matching switched on => case expr type
	//@Todo check case constant value overlap
	//@Todo check that cases fall into block, and theres no cases that dont do anything
	
	for (Ast_Switch_Case& _case : _switch->cases)
	{
		if (_case.block)
		{
			check_block(cc, _case.block.value(), Checker_Block_Flags::None);
		}
	}

	//@Todo add context with switch on type and constant requirement
	option<Ast_Type> type = check_term(cc, {}, _switch->term);
	bool switched_type_is_correct = true;
	bool all_terms_correct = true;
	if (type)
	{
		Type_Kind kind = type_kind(cc, type.value());
		if (kind != Type_Kind::Integer && kind != Type_Kind::Enum)
		{
			err_set;
			switched_type_is_correct = false;
			all_terms_correct = false;
			printf("Switching is only allowed on value of enum or integer types\n");
			debug_print_type(type.value());
			printf("\n");
			debug_print_term(_switch->term, 0);
			printf("\n");
		}
	}
	else
	{
		return; //@Check blocks even when switched on type is broken
	}

	if (_switch->cases.empty())
	{
		err_set;
		printf("Switch must have at least one case: \n");
		debug_print_token(_switch->token, true, true);
		return;
	}

	Type_Context type_context = { type.value(), true};
	for (Ast_Switch_Case& _case : _switch->cases)
	{
		//@Notice type check should be full performed inside the check_expr based on context
		// rework this later for all check_expr calls.
		// use top level function for this instead of recursive check_expr
		// only top level will print errors maybe.
		option<Ast_Type> case_type = check_expr(cc, &type_context, _case.const_expr);
	}
}

void check_var_decl(Checker_Context* cc, Ast_Var_Decl* var_decl)
{
	Ast_Ident ident = var_decl->ident;

	if (checker_context_block_contains_var(cc, ident))
	{
		err_set;
		error("Declared variable is already in scope", ident);
		return;
	}

	if (var_decl->type)
	{
		option<Ast_Type> type = check_type_signature(cc, &var_decl->type.value());
		if (!type) return;

		if (var_decl->expr)
		{
			Type_Context type_context = { type.value(), false};
			option<Ast_Type> expr_type = check_expr(cc, &type_context, var_decl->expr.value());

			if (expr_type)
			{
				type_implicit_cast(cc, &expr_type.value(), type.value());
				if (!match_type(cc, type.value(), expr_type.value()))
				{
					err_set;
					printf("Type mismatch in variable declaration:\n"); 
					debug_print_ident(var_decl->ident);
					printf("Expected: "); debug_print_type(type.value()); printf("\n");
					printf("Got:      "); debug_print_type(expr_type.value()); printf("\n\n");
				}
			}
		}
		
		checker_context_block_add_var(cc, ident, type.value());
	}
	else
	{
		// @Errors this might produce "var not found" error in later checks, might be solved by flagging
		// not adding var to the stack, when inferred type is not valid
		option<Ast_Type> expr_type = check_expr(cc, {}, var_decl->expr.value());
		if (expr_type)
		{
			var_decl->type = expr_type.value();
			checker_context_block_add_var(cc, ident, expr_type.value());
		}
	}
}

void check_var_assign(Checker_Context* cc, Ast_Var_Assign* var_assign)
{
	option<Ast_Type> var_type = check_var(cc, var_assign->var);
	if (!var_type) return;

	if (var_assign->op != AssignOp::NONE)
	{
		err_set;
		printf("Check var assign: only '=' assign op is supported\n");
		debug_print_var_assign(var_assign, 0);
		printf("\n");
		return;
	}

	Type_Context type_context = { var_type.value(), false};
	option<Ast_Type> expr_type = check_expr(cc, &type_context, var_assign->expr);
	if (expr_type)
	{
		type_implicit_cast(cc, &expr_type.value(), var_type.value());

		if (!match_type(cc, var_type.value(), expr_type.value()))
		{
			err_set;
			printf("Type mismatch in variable assignment:\n");
			debug_print_ident(var_assign->var->ident);
			printf("Expected: "); debug_print_type(var_type.value()); printf("\n");
			printf("Got:      "); debug_print_type(expr_type.value()); printf("\n\n");
		}
	}
}

Type_Kind type_kind(Checker_Context* cc, Ast_Type type)
{
	if (type.pointer_level > 0) return Type_Kind::Pointer;

	switch (type.tag)
	{
	case Ast_Type::Tag::Basic:
	{
		switch (type.as_basic)
		{
		case BasicType::F32:
		case BasicType::F64: return Type_Kind::Float;
		case BasicType::BOOL: return Type_Kind::Bool;
		case BasicType::STRING: return Type_Kind::String;
		default: return Type_Kind::Integer;
		}
	}
	case Ast_Type::Tag::Array: return Type_Kind::Array;
	case Ast_Type::Tag::Struct: return Type_Kind::Struct;
	case Ast_Type::Tag::Enum: return Type_Kind::Enum;
	default:
	{
		err_set; 
		printf("[COMPILER ERROR] Ast_Type signature wasnt checked, tag cannot be Tag::Custom\n");
		printf("Hint: submit a bug report if you see this error message\n");
		debug_print_type(type);
		printf("\n");
		return Type_Kind::Integer;
	}
	}
}

Ast_Type type_from_basic(BasicType basic_type)
{
	Ast_Type type = {};
	type.tag = Ast_Type::Tag::Basic;
	type.as_basic = basic_type;
	return type;
}

bool match_type(Checker_Context* cc, Ast_Type type_a, Ast_Type type_b)
{
	if (type_a.pointer_level != type_b.pointer_level) return false;
	if (type_a.tag != type_b.tag) return false;

	switch (type_a.tag)
	{
	case Ast_Type::Tag::Basic: return type_a.as_basic == type_b.as_basic;
	case Ast_Type::Tag::Struct: return type_a.as_struct.struct_id == type_b.as_struct.struct_id;
	case Ast_Type::Tag::Enum: return type_a.as_enum.enum_id == type_b.as_enum.enum_id;
	case Ast_Type::Tag::Array:
	{
		Ast_Array_Type* array_a = type_a.as_array;
		Ast_Array_Type* array_b = type_b.as_array;
		if (array_a->is_dynamic != array_b->is_dynamic) return false;
		if (array_a->fixed_size != array_b->fixed_size) return false;
		return match_type(cc, array_a->element_type, array_b->element_type);
	}
	default:
	{
		err_set; printf("match_type: Unexpected Ast_Type::Tag. Disambiguate Tag::Custom by using check_type first:\n");
		debug_print_type(type_a); printf("\n");
		debug_print_type(type_b); printf("\n");
		return false;
	}
	}
}

void type_implicit_cast(Checker_Context* cc, Ast_Type* type, Ast_Type target_type)
{
	if (type->tag != Ast_Type::Tag::Basic) return;
	if (target_type.tag != Ast_Type::Tag::Basic) return;
	if (type->as_basic == target_type.as_basic) return;
	Type_Kind kind = type_kind(cc, *type);
	Type_Kind target_kind = type_kind(cc, target_type);

	if (kind == Type_Kind::Float && target_kind == Type_Kind::Float)
	{
		type->as_basic = target_type.as_basic;
		return;
	}
	
	if (kind == Type_Kind::Integer && target_kind == Type_Kind::Integer)
	{
		//
	}
}

void type_implicit_binary_cast(Checker_Context* cc, Ast_Type* type_a, Ast_Type* type_b)
{
	if (type_a->tag != Ast_Type::Tag::Basic) return;
	if (type_b->tag != Ast_Type::Tag::Basic) return;
	if (type_a->as_basic == type_b->as_basic) return;
	Type_Kind kind_a = type_kind(cc, *type_a);
	Type_Kind kind_b = type_kind(cc, *type_b);

	if (kind_a == Type_Kind::Float && kind_b == Type_Kind::Float)
	{
		if (type_a->as_basic == BasicType::F32) 
			type_a->as_basic = BasicType::F64;
		else type_b->as_basic = BasicType::F64;
		return;
	}

	if (kind_a == Type_Kind::Integer && kind_b == Type_Kind::Integer)
	{
		//
	}
}

option<Ast_Type> check_type_signature(Checker_Context* cc, Ast_Type* type)
{
	switch (type->tag)
	{
	case Ast_Type::Tag::Basic:
	{
		return *type;
	}
	case Ast_Type::Tag::Array:
	{
		option<Ast_Type> element_type = check_type_signature(cc, &type->as_array->element_type);
		if (!element_type) return {};
		return *type;
	}
	case Ast_Type::Tag::Custom:
	{
		Ast* target_ast = try_import(cc, type->as_custom->import);
		if (target_ast == NULL) return {};

		option<Ast_Struct_Info> struct_meta = find_struct(target_ast, type->as_custom->ident);
		if (struct_meta)
		{
			type->tag = Ast_Type::Tag::Struct;
			type->as_struct.struct_id = struct_meta.value().struct_id;
			type->as_struct.struct_decl = struct_meta.value().struct_decl;
			return *type;
		}

		option<Ast_Enum_Info> enum_meta = find_enum(target_ast, type->as_custom->ident);
		if (enum_meta)
		{
			type->tag = Ast_Type::Tag::Enum;
			type->as_enum.enum_id = enum_meta.value().enum_id;
			type->as_enum.enum_decl = enum_meta.value().enum_decl;
			return *type;
		}

		err_set;
		printf("Failed to find the custom type: ");
		debug_print_custom_type(type->as_custom);
		printf("\n");
		debug_print_ident(type->as_custom->ident);
		printf("\n");
		return {};
	}
	default:
	{
		err_set;
		printf("[COMPILER ERROR] Ast_Type signature cannot be checked multiple times\n");
		printf("Hint: submit a bug report if you see this error message\n");
		debug_print_type(*type);
		printf("\n");
		return {};
	}
	}
}

//@Not used, unsure if top level thing like this is needed
option<Ast_Type> check_expr_type(Checker_Context* cc, option<Type_Context*> context, Ast_Expr* expr)
{
	option<Ast_Type> type = check_expr(cc, context, expr);
	if (!type) return {};
	if (!context) return type;
	if (!match_type(cc, type.value(), context.value()->expect_type))
	{
		err_set;
		printf("Type mismatch:\n");
		printf("Expected: "); debug_print_type(context.value()->expect_type); printf("\n");
		printf("Got:      "); debug_print_type(type.value()); printf("\n\n");
		return {};
	}
	return type;
}

option<Ast_Type> check_expr(Checker_Context* cc, option<Type_Context*> context, Ast_Expr* expr)
{
	if (!check_is_const_expr(expr))
	{
		if (context && context.value()->expect_constant)
		{
			err_set;
			printf("Expected constant expression. Got: \n");
			debug_print_expr(expr, 0);
			printf("\n\n");
			return {};
		}

		switch (expr->tag)
		{
		case Ast_Expr::Tag::Term: return check_term(cc, context, expr->as_term);
		case Ast_Expr::Tag::Unary_Expr: return check_unary_expr(cc, context, expr->as_unary_expr);
		case Ast_Expr::Tag::Binary_Expr: return check_binary_expr(cc, context, expr->as_binary_expr);
		}
	}
	else
	{
		option<Literal> lit_result = check_const_expr(expr);
		if (!lit_result)
		{
			err_set; //propagate err inside check const expr instead of here
			printf("Invalid const expr");
			debug_print_expr(expr, 0);
			printf("\n\n");
			return {};
		}

		Ast_Const_Expr const_expr = {};
		Literal lit = lit_result.value();

		if (context)
		{
			switch (lit.kind)
			{
				case Literal_Kind::Bool: 
				{
					const_expr.basic_type = BasicType::BOOL;
					const_expr.as_bool = lit.as_bool;
					expr->tag = Ast_Expr::Tag::Const_Expr;
					expr->as_const_expr = const_expr;
					return type_from_basic(BasicType::BOOL);
				}
				case Literal_Kind::Float:
				{
					//@Base on context
					const_expr.basic_type = BasicType::F64;
					const_expr.as_f64 = lit.as_f64;
					expr->tag = Ast_Expr::Tag::Const_Expr;
					expr->as_const_expr = const_expr;
					return type_from_basic(BasicType::F64);
				}
				case Literal_Kind::Int:
				{
					//@Base on context
					//@Todo range based
					const_expr.basic_type = BasicType::I32;
					const_expr.as_i64 = lit.as_i64;
					expr->tag = Ast_Expr::Tag::Const_Expr;
					expr->as_const_expr = const_expr;
					return type_from_basic(BasicType::I32);
				}
				case Literal_Kind::UInt:
				{
					//@Base on context
					//@Todo range based
					const_expr.basic_type = BasicType::I32;
					const_expr.as_u64 = lit.as_u64;
					expr->tag = Ast_Expr::Tag::Const_Expr;
					expr->as_const_expr = const_expr;
					return type_from_basic(BasicType::I32);
				}
			}
		}
		else
		{
			switch (lit.kind)
			{
			case Literal_Kind::Bool:
			{
				const_expr.basic_type = BasicType::BOOL;
				const_expr.as_bool = lit.as_bool;
				expr->tag = Ast_Expr::Tag::Const_Expr;
				expr->as_const_expr = const_expr;
				return type_from_basic(BasicType::BOOL);
			}
			case Literal_Kind::Float:
			{
				const_expr.basic_type = BasicType::F64;
				const_expr.as_f64 = lit.as_f64;
				expr->tag = Ast_Expr::Tag::Const_Expr;
				expr->as_const_expr = const_expr;
				return type_from_basic(BasicType::F64);
			}
			case Literal_Kind::Int:
			{
				//@Todo range based default int type
				const_expr.basic_type = BasicType::I32;
				const_expr.as_i64 = lit.as_i64;
				expr->tag = Ast_Expr::Tag::Const_Expr;
				expr->as_const_expr = const_expr;
				return type_from_basic(BasicType::I32);
			}
			case Literal_Kind::UInt:
			{
				//@Todo range based default int type
				//@Might become a u64 if its too big
				const_expr.basic_type = BasicType::I32;
				const_expr.as_u64 = lit.as_u64;
				expr->tag = Ast_Expr::Tag::Const_Expr;
				expr->as_const_expr = const_expr;
				return type_from_basic(BasicType::I32);
			}
			}
		}
	}
}

option<Ast_Type> check_term(Checker_Context* cc, option<Type_Context*> context, Ast_Term* term)
{
	switch (term->tag)
	{
	case Ast_Term::Tag::Var: return check_var(cc, term->as_var);
	case Ast_Term::Tag::Enum: //@Const fold this should be part of constant evaluation
	{
		Ast_Enum* _enum = term->as_enum;
		Ast* target_ast = try_import(cc, _enum->import);
		if (target_ast == NULL) return {};

		option<Ast_Enum_Info> enum_meta = find_enum(target_ast, _enum->ident);
		if (!enum_meta) { err_set; error("Accessing undeclared enum", _enum->ident); return {}; }
		Ast_Enum_Decl* enum_decl = enum_meta.value().enum_decl;
		_enum->enum_id = enum_meta.value().enum_id;
		
		option<u32> variant_id = find_enum_variant(enum_decl, _enum->variant);
		if (!variant_id) { err_set; error("Accessing undeclared enum variant", _enum->variant); }
		else _enum->variant_id = variant_id.value();

		Ast_Type type = {};
		type.tag = Ast_Type::Tag::Enum;
		type.as_enum.enum_id = _enum->enum_id;
		type.as_enum.enum_decl = enum_meta.value().enum_decl;
		return type;
	}
	case Ast_Term::Tag::Sizeof: //@Const fold this should be part of constant evaluation
	{
		//@Notice not doing sizing of types yet, cant know the numeric range
		option<Ast_Type> type = check_type_signature(cc, &term->as_sizeof->type);
		if (type) return type_from_basic(BasicType::U64);
		return {};
	}
	case Ast_Term::Tag::Literal:
	{
		Ast_Literal literal = term->as_literal;
		switch (literal.token.type)
		{
		case TokenType::STRING_LITERAL: //@ Strings are just *i8 cstrings for now
		{
			Ast_Type string_ptr = type_from_basic(BasicType::I8);
			string_ptr.pointer_level += 1;
			return string_ptr;
		}
		default:
		{
			err_set;
			printf("Check_term: Unsupported literal term, only string literals are allowed to be proccesed:\n");
			debug_print_token(literal.token, true, true);
			printf("\n");
			return {};
		}
		}
	}
	case Ast_Term::Tag::Proc_Call: 
	{
		return check_proc_call(cc, term->as_proc_call, Checker_Proc_Call_Flags::In_Expr);
	}
	case Ast_Term::Tag::Struct_Init:
	{
		Ast_Struct_Init* struct_init = term->as_struct_init;
		Ast* target_ast = try_import(cc, struct_init->import);
		if (target_ast == NULL) return {};

		// find struct
		Ast_Struct_Decl* struct_decl = NULL;
		u32 struct_id = 0;
		
		if (struct_init->ident)
		{
			Ast_Ident ident = struct_init->ident.value();
			option<Ast_Struct_Info> struct_meta = find_struct(target_ast, ident);
			if (!struct_meta) 
			{ 
				err_set; 
				error("Struct type identifier wasnt found", ident); 
				return {}; 
			}
			struct_decl = struct_meta.value().struct_decl;
			struct_id = struct_meta.value().struct_id;
		}

		if (context)
		{
			Type_Context* t_context = context.value();
			Ast_Type expect_type = t_context->expect_type;
			if (expect_type.tag == Ast_Type::Tag::Struct)
			{
				Ast_Struct_Type expected_struct = expect_type.as_struct;
				if (struct_decl == NULL)
				{
					struct_decl = expected_struct.struct_decl;
					struct_id = expected_struct.struct_id;
				}
				else
				{
					if (struct_id != expected_struct.struct_id)
					{
						err_set;
						printf("Struct initializer struct type doesnt match the expected type:\n");
						debug_print_struct_init(struct_init, 0); printf("\n");
						return {};
					}
				}
			}
		}

		if (struct_decl == NULL)
		{
			err_set;
			printf("Cannot infer the struct initializer type without a context\n");
			printf("Hint: specify type on varible: var : Type = .{ ... }, or on initializer var := Type.{ ... }\n");
			debug_print_struct_init(struct_init, 0); printf("\n");
			return {};
		}

		// check input count
		u32 field_count = (u32)struct_decl->fields.size();
		u32 input_count = (u32)struct_init->input_exprs.size();
		if (field_count != input_count)
		{
			err_set;
			printf("Unexpected number of fields in struct initializer:\n");
			printf("Expected: %lu Got: %lu \n", field_count, input_count);
			debug_print_struct_init(struct_init, 0); printf("\n");
		}

		// check input exprs
		for (u32 i = 0; i < input_count; i += 1)
		{
			if (i < field_count)
			{
				Ast_Type param_type = struct_decl->fields[i].type;
				Type_Context type_context = { param_type, false };
				option<Ast_Type> expr_type = check_expr(cc, &type_context, struct_init->input_exprs[i]);
				if (expr_type)
				{
					if (!match_type(cc, param_type, expr_type.value()))
					{
						err_set;
						printf("Type mismatch in struct initializer input argument with id: %lu\n", i);
						printf("Expected: "); debug_print_type(param_type); printf("\n");
						printf("Got:      "); debug_print_type(expr_type.value()); printf("\n");
						debug_print_expr(struct_init->input_exprs[i], 0); printf("\n");
					}
				}
			}
		}

		struct_init->struct_id = struct_id;
		
		Ast_Type type = {};
		type.tag = Ast_Type::Tag::Struct;
		type.as_struct.struct_id = struct_id;
		type.as_struct.struct_decl = struct_decl;

		return type;
	}
	}
}

option<Ast_Type> check_var(Checker_Context* cc, Ast_Var* var)
{
	option<Ast_Type> type = checker_context_block_find_var_type(cc, var->ident);
	if (type) return check_access(cc, type.value(), var->access);

	err_set;
	error("Check var: var is not found or has not valid type", var->ident);
	return {};
}

option<Ast_Type> check_access(Checker_Context* cc, Ast_Type type, option<Ast_Access*> optional_access)
{
	if (!optional_access) return type;
	Ast_Access* access = optional_access.value();
	
	switch (access->tag)
	{
	case Ast_Access::Tag::Var:
	{
		Ast_Var_Access* var_access = access->as_var;

		Type_Kind kind = type_kind(cc, type);
		if (kind == Type_Kind::Pointer && type.pointer_level == 1 && type.tag == Ast_Type::Tag::Struct) kind = Type_Kind::Struct;
		if (kind != Type_Kind::Struct)
		{
			err_set;
			error("Field access might only be used on variables of struct or pointer to a struct type", var_access->ident);
			return {};
		}

		Ast_Struct_Decl* struct_decl = type.as_struct.struct_decl;
		option<u32> field_id = find_struct_field(struct_decl, var_access->ident);
		if (!field_id)
		{
			err_set;
			error("Failed to find struct field during access", var_access->ident);
			return {};
		}
		var_access->field_id = field_id.value();

		Ast_Type result_type = struct_decl->fields[var_access->field_id].type;
		return check_access(cc, result_type, var_access->next);
	}
	case Ast_Access::Tag::Array:
	{
		Ast_Array_Access* array_access = access->as_array;

		Type_Kind kind = type_kind(cc, type);
		if (kind != Type_Kind::Array)
		{
			err_set;
			printf("Array access might only be used on variables of array type:\n");
			debug_print_access(access);
			printf("\n\n");
			return {};
		}

		option<Ast_Type> expr_type = check_expr(cc, {}, array_access->index_expr);
		if (expr_type)
		{
			Type_Kind expr_kind = type_kind(cc, expr_type.value());
			if (expr_kind != Type_Kind::Integer)
			{
				err_set;
				printf("Array access expression must be of integer type:\n");
				debug_print_expr(array_access->index_expr, 0);
				printf("\n\n");
			}
		}

		Ast_Type result_type = type.as_array->element_type;
		return check_access(cc, result_type, array_access->next);
	}
	}
}

option<Ast_Type> check_proc_call(Checker_Context* cc, Ast_Proc_Call* proc_call, Checker_Proc_Call_Flags flags)
{
	Ast* target_ast = try_import(cc, proc_call->import);
	if (target_ast == NULL) return {};

	// find proc
	Ast_Ident ident = proc_call->ident;
	option<Ast_Proc_Info> proc_meta = find_proc(target_ast, ident);
	if (!proc_meta)
	{
		err_set;
		error("Calling undeclared procedure", ident);
		return {};
	}
	Ast_Proc_Decl* proc_decl = proc_meta.value().proc_decl;
	proc_call->proc_id = proc_meta.value().proc_id;

	// check input count
	u32 param_count = (u32)proc_decl->input_params.size();
	u32 input_count = (u32)proc_call->input_exprs.size();
	bool is_variadic = proc_decl->is_variadic;
	
	if (is_variadic)
	{
		if (input_count < param_count)
		{
			err_set;
			printf("Unexpected number of arguments in variadic procedure call:\n");
			printf("Expected at least: %lu Got: %lu \n", param_count, input_count);
			debug_print_ident(ident, true, true); printf("\n");
		}
	}
	else
	{
		if (param_count != input_count)
		{
			err_set;
			printf("Unexpected number of arguments in procedure call:\n");
			printf("Expected: %lu Got: %lu \n", param_count, input_count);
			debug_print_ident(ident, true, true); printf("\n");
		}
	}

	// check input exprs
	for (u32 i = 0; i < input_count; i += 1)
	{
		if (i < param_count)
		{
			Ast_Type param_type = proc_decl->input_params[i].type;
			Type_Context type_context = { param_type, false };
			option<Ast_Type> expr_type = check_expr(cc, &type_context, proc_call->input_exprs[i]);
			if (expr_type)
			{
				if (!match_type(cc, param_type, expr_type.value()))
				{
					err_set;
					printf("Type mismatch in procedure call input argument with id: %lu\n", i);
					debug_print_ident(proc_call->ident);
					printf("Expected: "); debug_print_type(param_type); printf("\n");
					printf("Got:      "); debug_print_type(expr_type.value()); printf("\n\n");
				}
			}
		}
		else if (is_variadic)
		{
			//on variadic inputs no context is available
			option<Ast_Type> expr_type = check_expr(cc, {}, proc_call->input_exprs[i]);
		}
	}

	// check access & return
	if (flags == Checker_Proc_Call_Flags::In_Expr)
	{
		if (!proc_decl->return_type)
		{
			err_set;
			printf("Procedure call inside expression must have a return type:\n");
			debug_print_proc_call(proc_call, 0);
			printf("\n");
			return {};
		}

		return check_access(cc, proc_decl->return_type.value(), proc_call->access);
	}
	else
	{
		if (proc_call->access)
		{
			err_set;
			printf("Procedure call statement cannot have access chains:\n");
			debug_print_proc_call(proc_call, 0);
			printf("\n");
		}

		if (proc_decl->return_type)
		{
			err_set;
			printf("Procedure call result cannot be discarded:\n");
			debug_print_proc_call(proc_call, 0);
			printf("\n");
		}

		return {};
	}
}

option<Ast_Type> check_unary_expr(Checker_Context* cc, option<Type_Context*> context, Ast_Unary_Expr* unary_expr)
{
	err_set;
	printf("[TODO] Unary expr is not checked\n\n");
	return {};
}

option<Ast_Type> check_binary_expr(Checker_Context* cc, option<Type_Context*> context, Ast_Binary_Expr* binary_expr)
{
	err_set;
	printf("[TODO] Binary expr is not checked\n\n");
	return {};
}

bool check_is_const_expr(Ast_Expr* expr)
{
	switch (expr->tag)
	{
	case Ast_Expr::Tag::Term:
	{
		Ast_Term* term = expr->as_term;
		switch (term->tag)
		{
		//@Notice not handling enum as constexpr yet 
		//case Ast_Term::Tag::Enum: return true;
		case Ast_Term::Tag::Literal: return term->as_literal.token.type != TokenType::STRING_LITERAL;
		default: return false;
		}
	}
	case Ast_Expr::Tag::Unary_Expr: return check_is_const_expr(expr->as_unary_expr->right);
	case Ast_Expr::Tag::Binary_Expr: return check_is_const_expr(expr->as_binary_expr->left) && check_is_const_expr(expr->as_binary_expr->right);
	}
}

// perform operations on highest sized types and only on call site determine
// and type check / infer the type based on the resulting value
// the only possible errors are:
// 1. int range overflow
// 2. invalid op semantics
// 3. float NaN
// 4. division / mod by 0

option<Literal> check_const_expr(Ast_Expr* expr)
{
	switch (expr->tag)
	{
	case Ast_Expr::Tag::Term: return check_const_term(expr->as_term);
	case Ast_Expr::Tag::Unary_Expr: return check_const_unary_expr(expr->as_unary_expr);
	case Ast_Expr::Tag::Binary_Expr: return check_const_binary_expr(expr->as_binary_expr);
	}
}

option<Literal> check_const_term(Ast_Term* term)
{
	switch (term->tag)
	{
	case Ast_Term::Tag::Literal:
	{
		Token token = term->as_literal.token;
		Literal lit = {};

		if (token.type == TokenType::BOOL_LITERAL)
		{
			lit.kind = Literal_Kind::Bool;
			lit.as_bool = token.bool_value;
		}
		else if (token.type == TokenType::FLOAT_LITERAL)
		{
			lit.kind = Literal_Kind::Float;
			lit.as_f64 = token.float64_value;
		}
		else
		{
			lit.kind = Literal_Kind::UInt;
			lit.as_u64 = token.integer_value;
		}

		return lit;
	}
	default: return {};
	}
}

option<Literal> check_const_unary_expr(Ast_Unary_Expr* unary_expr)
{
	option<Literal> rhs_result = check_const_expr(unary_expr->right);
	if (!rhs_result) return {};

	UnaryOp op = unary_expr->op;
	Literal rhs = rhs_result.value();
	Literal_Kind rhs_kind = rhs.kind;

	switch (op)
	{
	case UnaryOp::MINUS:
	{
		if (rhs_kind == Literal_Kind::Bool) { printf("Cannot apply unary - to bool expression\n"); return {}; }
		if (rhs_kind == Literal_Kind::Float) { rhs.as_f64 = -rhs.as_f64; return rhs; }
		if (rhs_kind == Literal_Kind::Int) { rhs.as_i64 = -rhs.as_i64; return rhs; }

		if (rhs.as_u64 <= 1 + static_cast<u64>(std::numeric_limits<i64>::max()))
		{
			rhs.kind = Literal_Kind::Int;
			rhs.as_i64 = -static_cast<i64>(rhs.as_u64);
			return rhs;
		}
		printf("Unary - results in integer oveflow\n");
		return {};
	}
	case UnaryOp::LOGIC_NOT:
	{
		if (rhs_kind == Literal_Kind::Bool) { rhs.as_bool = !rhs.as_bool; return rhs; }
		printf("Unary ! can only be applied to bool expression\n");
		return {};
	}
	case UnaryOp::BITWISE_NOT:
	{
		if (rhs_kind == Literal_Kind::Bool) { printf("Cannot apply unary ~ to bool expression\n"); return {}; }
		if (rhs_kind == Literal_Kind::Float) { printf("Cannot apply unary ~ to float expression\n"); return {}; }
		if (rhs_kind == Literal_Kind::Int) { rhs.as_i64 = ~rhs.as_i64; return rhs; }
		rhs.as_u64 = ~rhs.as_u64; return rhs;
	}
	case UnaryOp::ADDRESS_OF:
	{
		printf("Unary adress of * cannot be used on temporary values\n");
		return {};
	}
	case UnaryOp::DEREFERENCE:
	{
		printf("Unary dereference << cannot be used on temporary values\n");
		return {};
	}
	}
}

option<Literal> check_const_binary_expr(Ast_Binary_Expr* binary_expr)
{
	option<Literal> lhs_result = check_const_expr(binary_expr->left);
	if (!lhs_result) return {};
	option<Literal> rhs_result = check_const_expr(binary_expr->right);
	if (!rhs_result) return {};

	BinaryOp op = binary_expr->op;
	Literal lhs = lhs_result.value();
	Literal_Kind lhs_kind = lhs.kind;
	Literal rhs = rhs_result.value();
	Literal_Kind rhs_kind = rhs.kind;
	bool same_kind = lhs_kind == rhs_kind;

	//@Need apply i64 to u64 conversion if possible

	switch (op)
	{
	case BinaryOp::LOGIC_AND:
	{
		if (same_kind && lhs_kind == Literal_Kind::Bool) return Literal{ Literal_Kind::Bool, lhs.as_bool && rhs.as_bool };
		printf("Binary && can only be applied to bool expressions\n");
		return {};
	}
	case BinaryOp::LOGIC_OR:
	{
		if (same_kind && lhs_kind == Literal_Kind::Bool) return Literal{ Literal_Kind::Bool, lhs.as_bool || rhs.as_bool };
		printf("Binary || can only be applied to bool expressions\n");
		return {};
	}
	case BinaryOp::LESS:
	{
		if (!same_kind) { printf("Binary < can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind == Literal_Kind::Bool) { printf("Cannot apply < to binary expressions\n"); return {}; }
		if (lhs_kind == Literal_Kind::Float) return Literal{ Literal_Kind::Bool, lhs.as_f64 < rhs.as_f64 };
		if (lhs_kind == Literal_Kind::Int) return Literal{ Literal_Kind::Bool, lhs.as_i64 < rhs.as_i64 };
		return Literal{ Literal_Kind::Bool, lhs.as_u64 < rhs.as_u64 };
	}
	case BinaryOp::GREATER:
	{
		if (!same_kind) { printf("Binary > can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind == Literal_Kind::Bool) { printf("Cannot apply > to binary expressions\n"); return {}; }
		if (lhs_kind == Literal_Kind::Float) return Literal{ Literal_Kind::Bool, lhs.as_f64 > rhs.as_f64 };
		if (lhs_kind == Literal_Kind::Int) return Literal{ Literal_Kind::Bool, lhs.as_i64 > rhs.as_i64 };
		return Literal{ Literal_Kind::Bool, lhs.as_u64 > rhs.as_u64 };
	}
	case BinaryOp::LESS_EQUALS:
	{
		if (!same_kind) { printf("Binary <= can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind == Literal_Kind::Bool) { printf("Cannot apply <= to binary expressions\n"); return {}; }
		if (lhs_kind == Literal_Kind::Float) return Literal{ Literal_Kind::Bool, lhs.as_f64 <= rhs.as_f64 };
		if (lhs_kind == Literal_Kind::Int) return Literal{ Literal_Kind::Bool, lhs.as_i64 <= rhs.as_i64 };
		return Literal{ Literal_Kind::Bool, lhs.as_u64 <= rhs.as_u64 };
	}
	case BinaryOp::GREATER_EQUALS:
	{
		if (!same_kind) { printf("Binary >= can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind == Literal_Kind::Bool) { printf("Cannot apply >= to binary expressions\n"); return {}; }
		if (lhs_kind == Literal_Kind::Float) return Literal{ Literal_Kind::Bool, lhs.as_f64 >= rhs.as_f64 };
		if (lhs_kind == Literal_Kind::Int) return Literal{ Literal_Kind::Bool, lhs.as_i64 >= rhs.as_i64 };
		return Literal{ Literal_Kind::Bool, lhs.as_u64 >= rhs.as_u64 };
	}
	case BinaryOp::IS_EQUALS:
	{
		if (!same_kind) { printf("Binary == can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind == Literal_Kind::Bool) return Literal{ Literal_Kind::Bool, lhs.as_bool == rhs.as_bool };
		if (lhs_kind == Literal_Kind::Float) return Literal{ Literal_Kind::Bool, lhs.as_f64 == rhs.as_f64 };
		if (lhs_kind == Literal_Kind::Int) return Literal{ Literal_Kind::Bool, lhs.as_i64 == rhs.as_i64 };
		return Literal{ Literal_Kind::Bool, lhs.as_u64 == rhs.as_u64 };
	}
	case BinaryOp::NOT_EQUALS:
	{
		if (!same_kind) { printf("Binary != can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind == Literal_Kind::Bool) return Literal{ Literal_Kind::Bool, lhs.as_bool != rhs.as_bool };
		if (lhs_kind == Literal_Kind::Float) return Literal{ Literal_Kind::Bool, lhs.as_f64 != rhs.as_f64 };
		if (lhs_kind == Literal_Kind::Int) return Literal{ Literal_Kind::Bool, lhs.as_i64 != rhs.as_i64 };
		return Literal{ Literal_Kind::Bool, lhs.as_u64 != rhs.as_u64 };
	}
	case BinaryOp::PLUS:
	{
		if (!same_kind) { printf("Binary + can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind == Literal_Kind::Bool) { printf("Cannot apply + to binary expressions\n"); return {}; }
		if (lhs_kind == Literal_Kind::Float) { lhs.as_f64 += rhs.as_f64; return lhs; }
		if (lhs_kind == Literal_Kind::Int) { lhs.as_i64 += rhs.as_i64; return lhs; } //@Range
		lhs.as_u64 += rhs.as_u64; return lhs; //@Range
	}
	case BinaryOp::MINUS:
	{
		if (!same_kind) { printf("Binary - can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind == Literal_Kind::Bool) { printf("Cannot apply - to binary expressions\n"); return {}; }
		if (lhs_kind == Literal_Kind::Float) { lhs.as_f64 -= rhs.as_f64; return lhs; }
		if (lhs_kind == Literal_Kind::Int) { lhs.as_i64 -= rhs.as_i64; return lhs; } //@Range
		lhs.as_u64 -= rhs.as_u64; return lhs; //@Undeflow
	}
	case BinaryOp::TIMES:
	{
		if (!same_kind) { printf("Binary * can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind == Literal_Kind::Bool) { printf("Cannot apply * to binary expressions\n"); return {}; }
		if (lhs_kind == Literal_Kind::Float) { lhs.as_f64 *= rhs.as_f64; return lhs; }
		if (lhs_kind == Literal_Kind::Int) { lhs.as_i64 *= rhs.as_i64; return lhs; } //@Range
		lhs.as_u64 *= rhs.as_u64; return lhs; //@Range
	}
	case BinaryOp::DIV:
	{
		if (!same_kind) { printf("Binary / can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind == Literal_Kind::Bool) { printf("Cannot apply / to binary expressions\n"); return {}; }
		if (lhs_kind == Literal_Kind::Float) { lhs.as_f64 /= rhs.as_f64; return lhs; } //@Nan
		if (lhs_kind == Literal_Kind::Int) { lhs.as_i64 /= rhs.as_i64; return lhs; } //@Div 0
		lhs.as_u64 /= rhs.as_u64; return lhs; //@Div 0
	}
	case BinaryOp::MOD:
	{
		if (!same_kind) { printf("Binary %% can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind == Literal_Kind::Bool) { printf("Cannot apply %% to binary expressions\n"); return {}; }
		if (lhs_kind == Literal_Kind::Float) { printf("Cannot apply %% to float expressions\n"); return {}; }
		if (lhs_kind == Literal_Kind::Int) { lhs.as_i64 %= rhs.as_i64; return lhs; } //@Mod 0
		lhs.as_u64 %= rhs.as_u64; return lhs; //@Mod 0
	}
	case BinaryOp::BITWISE_AND: //@Unnesesary error messages on same_kind
	{
		if (!same_kind) { printf("Binary & can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind != Literal_Kind::UInt) { printf("Binary & can only be applied to unsigned integers\n"); return {}; }
		lhs.as_u64 &= rhs.as_u64;
		return lhs;
	}
	case BinaryOp::BITWISE_OR:
	{
		if (!same_kind) { printf("Binary | can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind != Literal_Kind::UInt) { printf("Binary | can only be applied to unsigned integers\n"); return {}; }
		lhs.as_u64 |= rhs.as_u64;
		return lhs;
	}
	case BinaryOp::BITWISE_XOR:
	{
		if (!same_kind) { printf("Binary ^ can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind != Literal_Kind::UInt) { printf("Binary ^ can only be applied to unsigned integers\n"); return {}; }
		lhs.as_u64 ^= rhs.as_u64;
		return lhs;
	}
	case BinaryOp::BITSHIFT_LEFT:
	{
		if (!same_kind) { printf("Binary << can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind != Literal_Kind::UInt) { printf("Binary << can only be applied to unsigned integers\n"); return {}; }
		//@Check bitshift amount to be 64
		lhs.as_u64 <<= rhs.as_u64;
		return lhs;
	}
	case BinaryOp::BITSHIFT_RIGHT:
	{
		if (!same_kind) { printf("Binary >> can only be applied to expressions of same kind\n"); return {}; }
		if (lhs_kind != Literal_Kind::UInt) { printf("Binary >> can only be applied to unsigned integers\n"); return {}; }
		//@Check bitshift amount to be 64
		lhs.as_u64 >>= rhs.as_u64;
		return lhs;
	}
	}
}

void error_pair(const char* message, const char* labelA, Ast_Ident identA, const char* labelB, Ast_Ident identB)
{
	printf("%s:\n", message);
	printf("%s: ", labelA);
	debug_print_ident(identA, true, true);
	printf("%s: ", labelB);
	debug_print_ident(identB, true, true);
	printf("\n");
}

void error(const char* message, Ast_Ident ident)
{
	printf("%s:\n", message);
	debug_print_ident(ident, true, true);
	printf("\n");
}
