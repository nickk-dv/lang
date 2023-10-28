#include "check.h"

#include "check_general.h"
#include "check_type.h"
#include "debug_printer.h"

//@Design currently checking is split into 3 stages
// 1. import paths & decl uniqueness checks
// 2. decl signature validity checks
// 3. proc block cfg & type and other semantics checks 
//@Todo check cant import same file under multiple names
//@Todo check cant use same type or procedure under multiple names

bool check_program(Ast_Program* program)
{
	Check_Context cc = {};
	Error_Handler err = {}; //@Temp until all errors are replaced with err_report(Error)
	Ast* main_ast = NULL;

	//1. check global symbols
	for (Ast* ast : program->modules)
	{
		check_context_init(&cc, ast, program, &err);
		check_decl_uniqueness(&cc);
		if (ast->filepath == "main") main_ast = ast;
	}
	if (main_ast == NULL) err_report(Error::MAIN_FILE_NOT_FOUND);
	if (err.has_err || err_get_status()) return false;

	//2. checks decls & main proc
	check_context_init(&cc, main_ast, program, &err);
	check_main_proc(&cc);
	for (Ast* ast : program->modules)
	{
		check_context_init(&cc, ast, program, &err);
		check_decls(&cc);
	}
	if (err.has_err || err_get_status()) return false;

	//3. checks struct self storage
	check_context_init(&cc, NULL, program, &err);
	check_perform_struct_sizing(&cc);
	if (err.has_err || err_get_status()) return false;

	//4. checks proc blocks
	for (Ast* ast : program->modules)
	{
		check_context_init(&cc, ast, program, &err);
		check_ast(&cc);
	}
	if (err.has_err || err_get_status()) return false;

	return true;
}

void check_decl_uniqueness(Check_Context* cc)
{
	Ast* ast = cc->ast;
	ast->import_table.init(64);
	ast->struct_table.init(64);
	ast->enum_table.init(64);
	ast->proc_table.init(64);
	ast->global_table.init(64);
	HashSet<Ast_Ident, u32, match_ident> symbol_table(256);
	Ast_Program* program = cc->program;

	for (Ast_Import_Decl* decl : ast->imports)
	{
		char* path = decl->file_path.token.string_literal_value;
		option<Ast*> import_ast = cc->program->module_map.find(std::string(path), hash_fnv1a_32(string_view_from_string(std::string(path))));
		if (!import_ast) { err_report(Error::IMPORT_PATH_NOT_FOUND); continue; }
		decl->import_ast = import_ast.value();
	}
	
	for (Ast_Import_Decl* decl : ast->imports)
	{
		Ast_Ident ident = decl->alias;
		option<Ast_Ident> key = symbol_table.find_key(ident, hash_ident(ident));
		if (key) { err_report(Error::SYMBOL_ALREADY_DECLARED); continue; }
		symbol_table.add(ident, hash_ident(ident));
		ast->import_table.add(ident, decl, hash_ident(ident));
	}

	for (Ast_Use_Decl* decl : ast->uses)
	{
		Ast_Ident ident = decl->alias;
		option<Ast_Ident> key = symbol_table.find_key(ident, hash_ident(ident));
		if (key) { err_report(Error::SYMBOL_ALREADY_DECLARED); continue; }
		symbol_table.add(ident, hash_ident(ident));
	}

	for (Ast_Struct_Decl* decl : ast->structs)
	{
		Ast_Ident ident = decl->ident;
		option<Ast_Ident> key = symbol_table.find_key(ident, hash_ident(ident));
		if (key) { err_report(Error::SYMBOL_ALREADY_DECLARED); continue; }
		symbol_table.add(ident, hash_ident(ident));
		ast->struct_table.add(ident, Ast_Struct_Info { (u32)program->structs.size(), decl }, hash_ident(ident));
		program->structs.emplace_back(Ast_Struct_IR_Info { decl });
	}

	for (Ast_Enum_Decl* decl : ast->enums)
	{
		Ast_Ident ident = decl->ident;
		option<Ast_Ident> key = symbol_table.find_key(ident, hash_ident(ident));
		if (key) { err_report(Error::SYMBOL_ALREADY_DECLARED); continue; }
		symbol_table.add(ident, hash_ident(ident));
		ast->enum_table.add(ident, Ast_Enum_Info { (u32)program->enums.size(), decl }, hash_ident(ident));
		program->enums.emplace_back(Ast_Enum_IR_Info { decl });
	}

	for (Ast_Proc_Decl* decl : ast->procs)
	{
		Ast_Ident ident = decl->ident;
		option<Ast_Ident> key = symbol_table.find_key(ident, hash_ident(ident));
		if (key) { err_report(Error::SYMBOL_ALREADY_DECLARED); continue; }
		symbol_table.add(ident, hash_ident(ident));
		ast->proc_table.add(ident, Ast_Proc_Info { (u32)program->procs.size(), decl }, hash_ident(ident));
		program->procs.emplace_back(Ast_Proc_IR_Info { decl });
	}

	for (Ast_Global_Decl* decl : ast->globals)
	{
		Ast_Ident ident = decl->ident;
		option<Ast_Ident> key = symbol_table.find_key(ident, hash_ident(ident));
		if (key) { err_report(Error::SYMBOL_ALREADY_DECLARED); continue; }
		symbol_table.add(ident, hash_ident(ident));
		ast->global_table.add(ident, Ast_Global_Info { (u32)program->globals.size(), decl }, hash_ident(ident));
		program->globals.emplace_back(Ast_Global_IR_Info { decl });
	}
}

void check_decls(Check_Context* cc)
{
	Ast* ast = cc->ast;

	for (Ast_Use_Decl* use_decl : ast->uses)
	{
		Ast* import_ast = find_import(cc, { use_decl->import });
		if (import_ast == NULL) continue;

		Ast_Ident alias = use_decl->alias;
		Ast_Ident symbol = use_decl->symbol;
		option<Ast_Struct_Info> struct_info = import_ast->struct_table.find(symbol, hash_ident(symbol));
		if (struct_info) { ast->struct_table.add(alias, struct_info.value(), hash_ident(alias)); continue; }
		option<Ast_Enum_Info> enum_info = import_ast->enum_table.find(symbol, hash_ident(symbol));
		if (enum_info) { ast->enum_table.add(alias, enum_info.value(), hash_ident(alias)); continue; }
		option<Ast_Proc_Info> proc_info = import_ast->proc_table.find(symbol, hash_ident(symbol));
		if (proc_info) { ast->proc_table.add(alias, proc_info.value(), hash_ident(alias)); continue; }
		option<Ast_Global_Info> global_info = import_ast->global_table.find(symbol, hash_ident(symbol));
		if (global_info) { ast->global_table.add(alias, global_info.value(), hash_ident(alias)); continue; }

		err_report(Error::USE_SYMBOL_NOT_FOUND);
	}

	HashSet<Ast_Ident, u32, match_ident> name_set(32);

	for (Ast_Struct_Decl* struct_decl : ast->structs)
	{
		if (!struct_decl->fields.empty()) name_set.zero_reset();
		
		for (Ast_Struct_Field& field : struct_decl->fields)
		{
			if (!check_type_signature(cc, &field.type)) continue;

			if (field.const_expr)
			{
				check_expr_type(cc, field.const_expr.value(), field.type, true);
			}
			
			option<Ast_Ident> name = name_set.find_key(field.ident, hash_ident(field.ident));
			if (name) err_report(Error::STRUCT_DUPLICATE_FIELD);
			else name_set.add(field.ident, hash_ident(field.ident));
		}
	}

	for (Ast_Enum_Decl* enum_decl : ast->enums)
	{
		if (!enum_decl->variants.empty()) name_set.zero_reset();
		else
		{
			err_report(Error::ENUM_ZERO_VARIANTS);
			continue;
		}

		BasicType type = enum_decl->basic_type;
		Ast_Type enum_type = type_from_basic(type);

		if (!token_basic_type_is_integer(type))
		{
			err_report(Error::ENUM_NON_INTEGER_TYPE);
			continue;
		}

		for (Ast_Enum_Variant& variant : enum_decl->variants)
		{
			option<Ast_Ident> name = name_set.find_key(variant.ident, hash_ident(variant.ident));
			if (name) err_report(Error::ENUM_DUPLICATE_VARIANT);
			else name_set.add(variant.ident, hash_ident(variant.ident));

			check_expr_type(cc, variant.const_expr, enum_type, true);
		}
	}
	
	for (Ast_Proc_Decl* proc_decl : ast->procs)
	{
		if (!proc_decl->input_params.empty()) name_set.zero_reset();

		for (Ast_Proc_Param& param : proc_decl->input_params)
		{
			check_type_signature(cc, &param.type);
			
			option<Ast_Ident> name = name_set.find_key(param.ident, hash_ident(param.ident));
			if (name) err_report(Error::PROC_DUPLICATE_PARAM);
			else name_set.add(param.ident, hash_ident(param.ident));
		}

		if (proc_decl->return_type)
		{
			check_type_signature(cc, &proc_decl->return_type.value());
		}
	}

	for (Ast_Global_Decl* global_decl : ast->globals)
	{
		global_decl->type = check_expr_type(cc, global_decl->const_expr, {}, true);
	}
}

void check_main_proc(Check_Context* cc)
{
	option<Ast_Proc_Info> proc_meta = find_proc(cc->ast, Ast_Ident { 0, 0, { (u8*)"main", 4} });
	if (!proc_meta) { err_report(Error::MAIN_PROC_NOT_FOUND); return; }
	Ast_Proc_Decl* proc_decl = proc_meta.value().proc_decl;
	proc_decl->is_main = true;
	if (proc_decl->is_external) err_report(Error::MAIN_PROC_EXTERNAL);
	if (proc_decl->is_variadic) err_report(Error::MAIN_PROC_VARIADIC);
	if (proc_decl->input_params.size() != 0) err_report(Error::MAIN_NOT_ZERO_PARAMS);
	if (!proc_decl->return_type) { err_report(Error::MAIN_PROC_NO_RETURN_TYPE); return; }
	if (!match_type(cc, proc_decl->return_type.value(), type_from_basic(BasicType::I32))) err_report(Error::MAIN_PROC_WRONG_RETURN_TYPE);
}

void check_perform_struct_sizing(Check_Context* cc)
{
	std::vector<u32> visited_ids;
	std::vector<Ast_Ident> field_chain;

	for (u32 i = 0; i < cc->program->structs.size(); i += 1)
	{
		visited_ids.clear();
		field_chain.clear();
		Ast_Struct_Decl* in_struct = cc->program->structs[i].struct_decl;
		bool is_infinite = check_struct_self_storage(cc, in_struct, i, visited_ids, field_chain);
		if (is_infinite)
		{
			err_report(Error::STRUCT_INFINITE_SIZE);
			printf("Field access path: ");
			debug_print_ident(field_chain[field_chain.size() - 1], false, false);
			for (int k = (int)field_chain.size() - 2; k >= 0; k -= 1)
			{
				printf(".");
				debug_print_ident(field_chain[k], false, false);
			}
			printf("\n\n");
		}
		else
		{
			check_struct_size(&cc->program->structs[i]);
		}
	}

	//@Todo sizing on valid structs
	//@Notice struct sizing must be done in earlier stages
	//to allow for correct const folding and range checking on sizeof expressions
}

bool check_struct_self_storage(Check_Context* cc, Ast_Struct_Decl* in_struct, u32 struct_id, std::vector<u32>& visited_ids, std::vector<Ast_Ident>& field_chain)
{
	for (Ast_Struct_Field& field : in_struct->fields)
	{
		option<Ast_Struct_Type> struct_type = check_extract_struct_value_type(field.type);
		if (!struct_type) continue;
		if (struct_type.value().struct_id == struct_id) { field_chain.emplace_back(field.ident); return true; }
		
		bool already_visited = false;
		for (u32 id : visited_ids) if (struct_type.value().struct_id == id) { already_visited = true; break; }
		if (already_visited) continue;
		
		visited_ids.emplace_back(struct_type.value().struct_id);
		bool is_infinite = check_struct_self_storage(cc, struct_type.value().struct_decl, struct_id, visited_ids, field_chain);
		if (is_infinite) { field_chain.emplace_back(field.ident); return true; }
	}

	return false;
}

option<Ast_Struct_Type> check_extract_struct_value_type(Ast_Type type)
{
	if (type.pointer_level > 0) return {};

	switch (type.tag)
	{
	case Ast_Type_Tag::Array: return check_extract_struct_value_type(type.as_array->element_type);
	case Ast_Type_Tag::Struct: return type.as_struct;
	default: return {};
	}
}

void check_struct_size(Ast_Struct_IR_Info* struct_info)
{
	Ast_Struct_Decl* struct_decl = struct_info->struct_decl;
	u32 field_count = struct_decl->fields.size();
	
	u32 total_size = 0;
	u32 max_align = 0;

	for (u32 i = 0; i < field_count; i += 1)
	{
		u32 field_size = check_get_type_size(struct_decl->fields[i].type);
		total_size += field_size;

		if (i + 1 < field_count)
		{
			u32 align = check_get_type_align(struct_decl->fields[i + 1].type);
			if (align > field_size)
			{
				u32 padding = align - field_size;
				total_size += padding;
			}
			if (align > max_align) max_align = align;
		}
		else
		{
			u32 align = max_align;
			if (align > field_size)
			{
				u32 padding = align - field_size;
				total_size += padding;
			}
		}
	}

	struct_info->is_sized = true;
	struct_info->struct_size = total_size;
	struct_info->max_align = max_align;

	printf("size: %lu struct: ", total_size);
	debug_print_ident(struct_info->struct_decl->ident, true, false);
}

u32 check_get_basic_type_size(BasicType basic_type)
{
	switch (basic_type)
	{
	case BasicType::I8: return 1;
	case BasicType::U8: return 1;
	case BasicType::I16: return 2;
	case BasicType::U16: return 2;
	case BasicType::I32: return 4;
	case BasicType::U32: return 4;
	case BasicType::I64: return 8;
	case BasicType::U64: return 8;
	case BasicType::BOOL: return 1;
	case BasicType::F32: return 4;
	case BasicType::F64: return 8;
	case BasicType::STRING: return 0; //@Not implemented
	}
}

u32 check_get_basic_type_align(BasicType basic_type)
{
	switch (basic_type)
	{
	case BasicType::I8: return 1;
	case BasicType::U8: return 1;
	case BasicType::I16: return 2;
	case BasicType::U16: return 2;
	case BasicType::I32: return 4;
	case BasicType::U32: return 4;
	case BasicType::I64: return 8;
	case BasicType::U64: return 8;
	case BasicType::BOOL: return 1;
	case BasicType::F32: return 4;
	case BasicType::F64: return 8;
	case BasicType::STRING: return 0; //@Not implemented
	}
}

u32 check_get_type_size(Ast_Type type)
{
	if (type.pointer_level > 0) return 8; //@Assume 64bit

	switch (type.tag)
	{
	case Ast_Type_Tag::Basic: return check_get_basic_type_size(type.as_basic);
	case Ast_Type_Tag::Array:
	{
		printf("array type size not implemented\n");
		return 0;
	}
	case Ast_Type_Tag::Struct:
	{
		printf("struct type size not implemented\n");
		return 0;
	}
	case Ast_Type_Tag::Enum: return check_get_basic_type_size(type.as_enum.enum_decl->basic_type);
	}
}

u32 check_get_type_align(Ast_Type type)
{
	if (type.pointer_level > 0) return 8; //@Assume 64bit

	switch (type.tag)
	{
	case Ast_Type_Tag::Basic: return check_get_basic_type_align(type.as_basic);
	case Ast_Type_Tag::Array:
	{
		printf("array type allign not implemented\n");
		return 0;
	}
	case Ast_Type_Tag::Struct:
	{
		printf("struct type allign not implemented\n");
		return 0;
	}
	case Ast_Type_Tag::Enum: return check_get_basic_type_align(type.as_enum.enum_decl->basic_type);
	}
}

void check_ast(Check_Context* cc)
{
	for (Ast_Proc_Decl* proc_decl : cc->ast->procs)
	{
		if (proc_decl->is_external) continue;

		//@Notice this doesnt correctly handle if else on top level, which may allow all paths to return
		// const exprs arent considered
		Terminator terminator = check_block_cfg(cc, proc_decl->block, false, false);
		if (terminator != Terminator::Return && proc_decl->return_type) err_report(Error::CFG_NOT_ALL_PATHS_RETURN);
		
		check_context_block_reset(cc, proc_decl);
		check_context_block_add(cc);
		for (Ast_Proc_Param& param : proc_decl->input_params)
		{
			option<Ast_Global_Info> global_info = find_global(cc->ast, param.ident);
			if (global_info)
			{
				err_set;
				error("Global variable with same identifier is already in scope", param.ident);
			}
			else
			{
				//@Notice this is checked in proc_decl but might be usefull for err recovery later
				if (!check_context_block_contains_var(cc, param.ident))
				check_context_block_add_var(cc, param.ident, param.type);
			}
		}
		check_block(cc, proc_decl->block, Checker_Block_Flags::Already_Added);
	}
}

Terminator check_block_cfg(Check_Context* cc, Ast_Block* block, bool is_loop, bool is_defer)
{
	Terminator terminator = Terminator::None;

	for (Ast_Statement* statement : block->statements)
	{
		if (terminator != Terminator::None)
		{
			err_report(Error::CFG_UNREACHABLE_STATEMENT);
			debug_print_statement(statement, 0);
			printf("\n");
			break;
		}

		switch (statement->tag)
		{
		case Ast_Statement_Tag::If:
		{
			check_if_cfg(cc, statement->as_if, is_loop, is_defer);
		} break;
		case Ast_Statement_Tag::For: 
		{
			check_block_cfg(cc, statement->as_for->block, true, is_defer);
		} break;
		case Ast_Statement_Tag::Block: 
		{
			terminator = check_block_cfg(cc, statement->as_block, is_loop, is_defer);
		} break;
		case Ast_Statement_Tag::Defer:
		{
			if (is_defer)
			{
				err_report(Error::CFG_NESTED_DEFER);
				debug_print_token(statement->as_defer->token, true, true);
				printf("\n");
			}
			else check_block_cfg(cc, statement->as_defer->block, false, true);
		} break;
		case Ast_Statement_Tag::Break:
		{
			if (!is_loop)
			{
				if (is_defer) err_report(Error::CFG_BREAK_INSIDE_DEFER);
				else err_report(Error::CFG_BREAK_OUTSIDE_LOOP);
				debug_print_token(statement->as_break->token, true, true);
				printf("\n");
			}
			else terminator = Terminator::Break;
		} break;
		case Ast_Statement_Tag::Return:
		{
			if (is_defer)
			{
				err_report(Error::CFG_RETURN_INSIDE_DEFER);
				debug_print_token(statement->as_defer->token, true, true);
				printf("\n");
			}
			else terminator = Terminator::Return;
		} break;
		case Ast_Statement_Tag::Switch:
		{
			check_switch_cfg(cc, statement->as_switch, is_loop, is_defer);
		} break;
		case Ast_Statement_Tag::Continue:
		{
			if (!is_loop)
			{
				if (is_defer) err_report(Error::CFG_CONTINUE_INSIDE_DEFER);
				else err_report(Error::CFG_CONTINUE_OUTSIDE_LOOP);
				debug_print_token(statement->as_continue->token, true, true);
				printf("\n");
			}
			else terminator = Terminator::Continue;
		} break;
		case Ast_Statement_Tag::Proc_Call: break;
		case Ast_Statement_Tag::Var_Decl: break;
		case Ast_Statement_Tag::Var_Assign: break;
		}
	}

	return terminator;
}

void check_if_cfg(Check_Context* cc, Ast_If* _if, bool is_loop, bool is_defer)
{
	check_block_cfg(cc, _if->block, is_loop, is_defer);
	
	if (_if->_else)
	{
		Ast_Else* _else = _if->_else.value();
		if (_else->tag == Ast_Else_Tag::If)
			check_if_cfg(cc, _else->as_if, is_loop, is_defer);
		else check_block_cfg(cc, _else->as_block, is_loop, is_defer);
	}
}

void check_switch_cfg(Check_Context* cc, Ast_Switch* _switch, bool is_loop, bool is_defer)
{
	for (Ast_Switch_Case& _case : _switch->cases)
	{
		if (_case.block) check_block_cfg(cc, _case.block.value(), is_loop, is_defer);
	}
}

static void check_block(Check_Context* cc, Ast_Block* block, Checker_Block_Flags flags)
{
	if (flags != Checker_Block_Flags::Already_Added) check_context_block_add(cc);

	for (Ast_Statement* statement: block->statements)
	{
		switch (statement->tag)
		{
		case Ast_Statement_Tag::If: check_if(cc, statement->as_if); break;
		case Ast_Statement_Tag::For: check_for(cc, statement->as_for); break;
		case Ast_Statement_Tag::Block: check_block(cc, statement->as_block, Checker_Block_Flags::None); break;
		case Ast_Statement_Tag::Defer: check_block(cc, statement->as_defer->block, Checker_Block_Flags::None); break;
		case Ast_Statement_Tag::Break: break;
		case Ast_Statement_Tag::Return: check_return(cc, statement->as_return); break;
		case Ast_Statement_Tag::Switch: check_switch(cc, statement->as_switch); break;
		case Ast_Statement_Tag::Continue: break;
		case Ast_Statement_Tag::Proc_Call: check_proc_call(cc, statement->as_proc_call, Checker_Proc_Call_Flags::In_Statement); break;
		case Ast_Statement_Tag::Var_Decl: check_var_decl(cc, statement->as_var_decl); break;
		case Ast_Statement_Tag::Var_Assign: check_var_assign(cc, statement->as_var_assign); break;
		}
	}

	check_context_block_pop_back(cc);
}

void check_if(Check_Context* cc, Ast_If* _if)
{
	check_expr_type(cc, _if->condition_expr, type_from_basic(BasicType::BOOL), false);
	check_block(cc, _if->block, Checker_Block_Flags::None);

	if (_if->_else)
	{
		Ast_Else* _else = _if->_else.value();
		if (_else->tag == Ast_Else_Tag::If) check_if(cc, _else->as_if);
		else check_block(cc, _else->as_block, Checker_Block_Flags::None);
	}
}

void check_for(Check_Context* cc, Ast_For* _for)
{
	check_context_block_add(cc);
	if (_for->var_decl) check_var_decl(cc, _for->var_decl.value());
	if (_for->var_assign) check_var_assign(cc, _for->var_assign.value());
	if (_for->condition_expr) check_expr_type(cc, _for->condition_expr.value(), type_from_basic(BasicType::BOOL), false);
	check_block(cc, _for->block, Checker_Block_Flags::Already_Added);
}

void check_return(Check_Context* cc, Ast_Return* _return)
{
	Ast_Proc_Decl* curr_proc = cc->curr_proc;

	if (_return->expr)
	{
		if (curr_proc->return_type)
		{
			check_expr_type(cc, _return->expr.value(), curr_proc->return_type, false);
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

void check_switch(Check_Context* cc, Ast_Switch* _switch)
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

	option<Ast_Type> type = check_expr_type(cc, _switch->expr, {}, false);
	if (!type) return;

	Type_Kind kind = type_kind(cc, type.value());
	if (kind != Type_Kind::Integer && kind != Type_Kind::Enum)
	{
		err_set;
		printf("Switching is only allowed on value of enum or integer types\n");
		debug_print_type(type.value());
		printf("\n");
		debug_print_expr(_switch->expr, 0);
		printf("\n");
	}

	if (_switch->cases.empty())
	{
		err_set;
		printf("Switch must have at least one case: \n");
		debug_print_token(_switch->token, true, true);
		return;
	}

	for (Ast_Switch_Case& _case : _switch->cases)
	{
		check_expr_type(cc, _case.const_expr, type.value(), true);
	}
}

void check_var_decl(Check_Context* cc, Ast_Var_Decl* var_decl)
{
	Ast_Ident ident = var_decl->ident;

	option<Ast_Global_Info> global_info = find_global(cc->ast, ident);
	if (global_info)
	{
		err_set;
		error("Global variable with same identifier is already in scope", ident);
		return;
	}

	if (check_context_block_contains_var(cc, ident))
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
			option<Ast_Type> expr_type = check_expr_type(cc, var_decl->expr.value(), type.value(), false);
		}
		
		check_context_block_add_var(cc, ident, type.value());
	}
	else
	{
		// @Errors this might produce "var not found" error in later checks, might be solved by flagging
		// not adding var to the stack, when inferred type is not valid
		option<Ast_Type> expr_type = check_expr_type(cc, var_decl->expr.value(), {}, false);
		if (expr_type)
		{
			var_decl->type = expr_type.value();
			check_context_block_add_var(cc, ident, expr_type.value());
		}
	}
}

void check_var_assign(Check_Context* cc, Ast_Var_Assign* var_assign)
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

	check_expr_type(cc, var_assign->expr, var_type.value(), false);
}
