#include "llvm_ir_builder.h"

#include "llvm-c/Core.h"

LLVMModuleRef LLVM_IR_Builder::build_module(Ast* ast)
{
	module = LLVMModuleCreateWithName("module");
	builder = LLVMCreateBuilder();

	struct_decl_map.init(32);
	proc_decl_map.init(32);
	for (Ast_Enum_Decl* enum_decl : ast->enums) { build_enum_decl(enum_decl); }
	for (Ast_Struct_Decl* struct_decl : ast->structs) { build_struct_decl(struct_decl); }
	for (Ast_Proc_Decl* proc_decl : ast->procs) { build_proc_decl(proc_decl); }
	for (Ast_Proc_Decl* proc_decl : ast->procs) { build_proc_body(proc_decl); }
	
	LLVMDisposeBuilder(builder);
	return module;
}

void LLVM_IR_Builder::build_enum_decl(Ast_Enum_Decl* enum_decl)
{
	for (u32 i = 0; i < enum_decl->variants.size(); i++)
	{
		LLVMValueRef enum_constant = LLVMAddGlobal(module, LLVMInt32Type(), get_c_string(enum_decl->variants[i].token));
		LLVMSetInitializer(enum_constant, LLVMConstInt(LLVMInt32Type(), enum_decl->constants[i], 0));
		LLVMSetGlobalConstant(enum_constant, 1);
	}
}

void LLVM_IR_Builder::build_struct_decl(Ast_Struct_Decl* struct_decl)
{
	std::vector<LLVMTypeRef> members; //@Perf deside on better member storage
	for (const Ast_Ident_Type_Pair& field : struct_decl->fields)
	{
		LLVMTypeRef type_ref = get_type_meta(field.type).type;
		members.emplace_back(type_ref);
	}

	LLVMTypeRef struct_type = LLVMStructCreateNamed(LLVMGetGlobalContext(), get_c_string(struct_decl->type.token));
	LLVMStructSetBody(struct_type, members.data(), (u32)members.size(), 0);
	Struct_Meta meta = { struct_decl, struct_type };
	struct_decl_map.add(struct_decl->type.token.string_value, meta, hash_fnv1a_32(struct_decl->type.token.string_value));
}

void LLVM_IR_Builder::build_proc_decl(Ast_Proc_Decl* proc_decl)
{
	std::vector<LLVMTypeRef> param_types = {};
	param_types.reserve(proc_decl->input_params.size());
	for (u32 i = 0; i < proc_decl->input_params.size(); i += 1)
	param_types.emplace_back(get_type_meta(proc_decl->input_params[i].type).type);

	LLVMTypeRef ret_type = LLVMVoidType();
	if (proc_decl->return_type.has_value())
	ret_type = get_type_meta(proc_decl->return_type.value()).type;

	LLVMTypeRef proc_type = LLVMFunctionType(ret_type, param_types.data(), (u32)param_types.size(), 0); //@Temp Discarding input args
	LLVMValueRef proc_val = LLVMAddFunction(module, get_c_string(proc_decl->ident.token), proc_type);

	Proc_Meta meta = { proc_type, proc_val };
	proc_decl_map.add(proc_decl->ident.token.string_value, meta, hash_fnv1a_32(proc_decl->ident.token.string_value));
}

void LLVM_IR_Builder::build_proc_body(Ast_Proc_Decl* proc_decl)
{
	if (proc_decl->external) return;
	auto proc_meta = proc_decl_map.find(proc_decl->ident.token.string_value, hash_fnv1a_32(proc_decl->ident.token.string_value));
	if (!proc_meta) { error_exit("failed to find proc declaration while building its body"); return; }
	LLVMBasicBlockRef entry_block = LLVMAppendBasicBlock(proc_meta->proc_val, "entry");
	LLVMPositionBuilderAtEnd(builder, entry_block);

	Var_Block_Scope bc = {};
	bc.add_block();
	u32 count = 0;
	for (Ast_Ident_Type_Pair& param : proc_decl->input_params)
	{
		Type_Meta var_type = get_type_meta(param.type);
		LLVMValueRef param_value = LLVMGetParam(proc_meta.value().proc_val, count);
		LLVMValueRef copy_ptr = LLVMBuildAlloca(builder, var_type.type, "copy_ptr");
		LLVMBuildStore(builder, param_value, copy_ptr);
		bc.add_var(Var_Meta{ param.ident.token.string_value, copy_ptr, var_type });
		count += 1;
	}
	build_block(proc_decl->block, entry_block, proc_meta.value().proc_val, &bc);

	//@For non void return values return statement is expected to exist during checking stage
	if (!proc_decl->return_type.has_value()) LLVMBuildRet(builder, NULL);
}

//@Todo investigate order of nested if else blocks, try to reach the logical order
//@Also numbering of if_block / else_blocks is weidly not sequential
// nested after_blocks are inserted in the end resulting in the non sequential ir output
Terminator_Type LLVM_IR_Builder::build_block(Ast_Block* block, LLVMBasicBlockRef basic_block, LLVMValueRef proc_value, Var_Block_Scope* bc, std::optional<Loop_Meta> loop_meta, bool entry)
{
	if (!entry) bc->add_block();
	LLVMPositionBuilderAtEnd(builder, basic_block);

	for (Ast_Statement* statement : block->statements)
	{
		switch (statement->tag)
		{
			case Ast_Statement::Tag::If:
			{
				LLVMBasicBlockRef after_block = LLVMAppendBasicBlock(proc_value, "cont");
				build_if(statement->as_if, basic_block, after_block, proc_value, bc, loop_meta);
				LLVMPositionBuilderAtEnd(builder, after_block);
				basic_block = after_block;
			} break;
			case Ast_Statement::Tag::For:
			{
				LLVMBasicBlockRef after_block = LLVMAppendBasicBlock(proc_value, "loop_exit");
				build_for(statement->as_for, basic_block, after_block, proc_value, bc);
				LLVMPositionBuilderAtEnd(builder, after_block);
				basic_block = after_block;
			} break;
			case Ast_Statement::Tag::Break:
			{
				if (!loop_meta) error_exit("break statement: no loop meta data provided");
				LLVMBuildBr(builder, loop_meta.value().break_target);
				bc->pop_block();
				return Terminator_Type::Break;
			} break;
			case Ast_Statement::Tag::Return:
			{
				Ast_Return* _return = statement->as_return;
				if (_return->expr.has_value())
					LLVMBuildRet(builder, build_expr_value(_return->expr.value(), bc));
				else LLVMBuildRet(builder, NULL);
				bc->pop_block();
				return Terminator_Type::Return;
			} break;
			case Ast_Statement::Tag::Continue:
			{
				if (!loop_meta) error_exit("continue statement: no loop meta data provided");

				//assign variable
				//loop back to condition
				if (loop_meta.value().continue_action)
					build_var_assign(loop_meta.value().continue_action.value(), bc);
				LLVMBuildBr(builder, loop_meta.value().continue_target);

				bc->pop_block();
				return Terminator_Type::Continue;
			} break;
			case Ast_Statement::Tag::Proc_Call: build_proc_call(statement->as_proc_call, bc, true); break;
			case Ast_Statement::Tag::Var_Decl: build_var_decl(statement->as_var_decl, bc); break;
			case Ast_Statement::Tag::Var_Assign: build_var_assign(statement->as_var_assign, bc); break;
			default: break;
		}
	}

	bc->pop_block();
	return Terminator_Type::None;
}

void LLVM_IR_Builder::build_if(Ast_If* _if, LLVMBasicBlockRef basic_block, LLVMBasicBlockRef after_block, LLVMValueRef proc_value, Var_Block_Scope* bc, std::optional<Loop_Meta> loop_meta)
{
	LLVMValueRef cond_value = build_expr_value(_if->condition_expr, bc);
	if (LLVMInt1Type() != LLVMTypeOf(cond_value)) error_exit("if: expected i1(bool) expression value");

	if (_if->_else.has_value())
	{
		LLVMBasicBlockRef then_block = LLVMInsertBasicBlock(after_block, "then");
		LLVMBasicBlockRef else_block = LLVMInsertBasicBlock(after_block, "else");
		LLVMBuildCondBr(builder, cond_value, then_block, else_block);

		Terminator_Type terminator = build_block(_if->block, then_block, proc_value, bc, loop_meta);
		if (terminator == Terminator_Type::None) LLVMBuildBr(builder, after_block);

		Ast_Else* _else = _if->_else.value();
		if (_else->tag == Ast_Else::Tag::If)
		{
			LLVMPositionBuilderAtEnd(builder, else_block);
			build_if(_else->as_if, basic_block, after_block, proc_value, bc, loop_meta);
		}
		else
		{
			Terminator_Type terminator = build_block(_else->as_block, else_block, proc_value, bc, loop_meta);
			if (terminator == Terminator_Type::None) LLVMBuildBr(builder, after_block);
		}
	}
	else
	{
		LLVMBasicBlockRef then_block = LLVMInsertBasicBlock(after_block, "then");
		LLVMBuildCondBr(builder, cond_value, then_block, after_block);

		Terminator_Type terminator = build_block(_if->block, then_block, proc_value, bc, loop_meta);
		if (terminator == Terminator_Type::None) LLVMBuildBr(builder, after_block);
	}
}

void LLVM_IR_Builder::build_for(Ast_For* _for, LLVMBasicBlockRef basic_block, LLVMBasicBlockRef after_block, LLVMValueRef proc_value, Var_Block_Scope* bc)
{
	//declare variable
	if (_for->var_decl) build_var_decl(_for->var_decl.value(), bc);

	//enter conditional block
	LLVMBasicBlockRef cond_block = LLVMInsertBasicBlock(after_block, "loop_cond");
	LLVMBuildBr(builder, cond_block);
	LLVMPositionBuilderAtEnd(builder, cond_block);

	//conditional branch
	LLVMBasicBlockRef body_block = LLVMInsertBasicBlock(after_block, "loop_body");
	if (_for->condition_expr)
	{
		LLVMValueRef cond_value = build_expr_value(_for->condition_expr.value(), bc);
		if (LLVMInt1Type() != LLVMTypeOf(cond_value)) error_exit("if: expected i1(bool) expression value");
		LLVMBuildCondBr(builder, cond_value, body_block, after_block);
	}
	else LLVMBuildBr(builder, body_block);

	Terminator_Type terminator = build_block(_for->block, body_block, proc_value, bc, Loop_Meta{ after_block, cond_block, _for->var_assign });
	if (terminator == Terminator_Type::None)
	{
		//assign variable
		if (_for->var_assign) build_var_assign(_for->var_assign.value(), bc);
		//loop back to condition
		LLVMBuildBr(builder, cond_block);
	}
}

LLVMValueRef LLVM_IR_Builder::build_proc_call(Ast_Proc_Call* proc_call, Var_Block_Scope* bc, bool is_statement)
{
	std::optional<Proc_Meta> proc_meta = proc_decl_map.find(proc_call->ident.token.string_value, hash_fnv1a_32(proc_call->ident.token.string_value));
	if (!proc_meta) { error_exit("failed to find proc declaration while trying to call it"); }

	std::vector<LLVMValueRef> input_values = {};
	input_values.reserve(proc_call->input_exprs.size());
	for (u32 i = 0; i < proc_call->input_exprs.size(); i += 1)
	input_values.emplace_back(build_expr_value(proc_call->input_exprs[i], bc));

	LLVMValueRef ret_val = LLVMBuildCall2(builder, proc_meta.value().proc_type, proc_meta.value().proc_val, input_values.data(), (u32)input_values.size(), is_statement ? "" : "call_val");
	return ret_val;
}

void LLVM_IR_Builder::build_var_decl(Ast_Var_Decl* var_decl, Var_Block_Scope* bc)
{
	if (!var_decl->type.has_value()) error_exit("var decl expected type to be known");
	Type_Meta var_type = get_type_meta(var_decl->type.value());

	LLVMValueRef var_ptr = LLVMBuildAlloca(builder, var_type.type, get_c_string(var_decl->ident.token));
	if (var_decl->expr.has_value())
	{
		LLVMValueRef expr_value = build_expr_value(var_decl->expr.value(), bc);
		expr_value = build_value_cast(expr_value, var_type.type);

		if (var_type.type != LLVMTypeOf(expr_value))
		{
			debug_print_llvm_type("Expected", var_type.type);
			debug_print_llvm_type("GotExpr", LLVMTypeOf(expr_value));
			//error_exit("type mismatch in variable declaration");
		}
		//@Hack using 0 init here, because int upcasts arent implemented yet
		LLVMBuildStore(builder, LLVMConstNull(var_type.type), var_ptr);
		LLVMBuildStore(builder, expr_value, var_ptr);
	}
	else LLVMBuildStore(builder, LLVMConstNull(var_type.type), var_ptr);

	bc->add_var(Var_Meta{ var_decl->ident.token.string_value, var_ptr, var_type });
}

void LLVM_IR_Builder::build_var_assign(Ast_Var_Assign* var_assign, Var_Block_Scope* bc)
{
	if (var_assign->op != ASSIGN_OP_NONE) error_exit("var assign: only = op is supported");

	Ast_Var* var = var_assign->var;
	Var_Access_Meta var_access = get_var_access_meta(var, bc);

	LLVMValueRef expr_value = build_expr_value(var_assign->expr, bc);
	expr_value = build_value_cast(expr_value, var_access.type);

	if (var_access.type != LLVMTypeOf(expr_value))
	{
		debug_print_llvm_type("Expected", var_access.type);
		debug_print_llvm_type("GotExpr", LLVMTypeOf(expr_value));
		error_exit("type mismatch in variable assignment");
	}
	LLVMBuildStore(builder, expr_value, var_access.ptr);
}

LLVMValueRef LLVM_IR_Builder::build_expr_value(Ast_Expr* expr, Var_Block_Scope* bc)
{
	LLVMValueRef value_ref = NULL;

	switch (expr->tag)
	{
	case Ast_Expr::Tag::Term:
	{
		Ast_Term* term = expr->as_term;

		switch (term->tag)
		{
			case Ast_Term::Tag::Var:
			{
				Ast_Var* var = term->as_var;
				Var_Access_Meta var_access = get_var_access_meta(var, bc);
				value_ref = LLVMBuildLoad2(builder, var_access.type, var_access.ptr, "load_val");
			} break;
			case Ast_Term::Tag::Literal:
			{
				Token token = term->as_literal.token;
				if (token.type == TOKEN_BOOL_LITERAL)
				{
					value_ref = LLVMConstInt(LLVMInt1Type(), (int)token.bool_value, 0);
				}
				else if (token.type == TOKEN_FLOAT_LITERAL) //@Choose Double or float? defaulting to double
				{
					value_ref = LLVMConstReal(LLVMDoubleType(), token.float64_value);
				}
				else if (token.type == TOKEN_INTEGER_LITERAL) //@Todo sign extend?
				{
					value_ref = LLVMConstInt(LLVMInt32Type(), token.integer_value, 0);
				}
				else error_exit("unsupported literal type");
			} break;
			case Ast_Term::Tag::Proc_Call:
			{
				value_ref = build_proc_call(term->as_proc_call, bc, false);
			} break;
		}
	} break;
	case Ast_Expr::Tag::Unary_Expr:
	{
		Ast_Unary_Expr* unary_expr = expr->as_unary_expr;
		UnaryOp op = unary_expr->op;

		LLVMValueRef rhs = build_expr_value(unary_expr->right, bc);
		LLVMTypeRef rhs_type = LLVMTypeOf(rhs);
		bool int_kind = type_is_int(rhs_type);
		bool bool_kind = type_is_bool(rhs_type);
		bool float_kind = type_is_float(rhs_type);
		if (!int_kind && !bool_kind && !float_kind)
			error_exit("unary_expr: expected float int or bool type");

		switch (op)
		{
		case UNARY_OP_MINUS:
		{
			if (float_kind) value_ref = LLVMBuildFNeg(builder, rhs, "utmp");
			else if (int_kind) value_ref = LLVMBuildNeg(builder, rhs, "utmp'"); //@Safety NoSignedWrap & NoUnsignedWrap variants exist
			else error_exit("unary_expr - expected fd or i");
		} break;
		case UNARY_OP_LOGIC_NOT:
		{
			if (bool_kind) value_ref = LLVMBuildNot(builder, rhs, "utmp");
			else error_exit("unary_expr ! expected bool");
		} break;
		case UNARY_OP_ADRESS_OF:
		{
			error_exit("unary_expr & not supported");
		} break;
		case UNARY_OP_BITWISE_NOT:
		{
			if (int_kind) value_ref = LLVMBuildNot(builder, rhs, "utmp"); //@Design only allow uint
			else error_exit("unary_expr ~ expected i");
		} break;
		default: error_exit("unary_expr unknown unary op"); break;
		}
	} break;
	case Ast_Expr::Tag::Binary_Expr:
	{
		Ast_Binary_Expr* binary_expr = expr->as_binary_expr;
		BinaryOp op = binary_expr->op;

		LLVMValueRef lhs = build_expr_value(binary_expr->left, bc);
		LLVMTypeRef lhs_type = LLVMTypeOf(lhs);
		LLVMValueRef rhs = build_expr_value(binary_expr->right, bc);
		LLVMTypeRef rhs_type = LLVMTypeOf(rhs);
		bool int_kind = type_is_int(lhs_type) && type_is_int(rhs_type);
		bool bool_kind = type_is_bool(lhs_type) && type_is_bool(rhs_type);
		bool float_kind = type_is_float(lhs_type) && type_is_float(rhs_type);
		if (!int_kind && !bool_kind && !float_kind)
			error_exit("binary_expr: expected matching float int or bool types");

		//@Incomplete for now this performs float-double upcast if nesessary
		build_binary_value_cast(lhs, rhs, lhs_type, rhs_type);

		switch (op)
		{
		// LogicOps [&& ||]
		case BINARY_OP_LOGIC_AND:
		{
			if (!bool_kind) error_exit("bin_expr && expected bool");
			value_ref = LLVMBuildAnd(builder, lhs, rhs, "btmp");
		} break;
		case BINARY_OP_LOGIC_OR:
		{
			if (!bool_kind) error_exit("bin_expr || expected bool");
			value_ref = LLVMBuildOr(builder, lhs, rhs, "btmp");
		} break;
		// CmpOps [< > <= >= == !=]
		case BINARY_OP_LESS: //@RealPredicates using ordered (no nans) variants
		{
			if (float_kind) value_ref = LLVMBuildFCmp(builder, LLVMRealOLT, lhs, rhs, "btmp");
			else if (int_kind) value_ref = LLVMBuildICmp(builder, LLVMIntSLT, lhs, rhs, "btmp"); //@Determine S / U predicates
			else error_exit("bin_expr < expected fd or i got bool");
		} break;
		case BINARY_OP_GREATER:
		{
			if (float_kind) value_ref = LLVMBuildFCmp(builder, LLVMRealOGT, lhs, rhs, "btmp");
			else if (int_kind) value_ref = LLVMBuildICmp(builder, LLVMIntSGT, lhs, rhs, "btmp"); //@Determine S / U predicates
			else error_exit("bin_expr > expected fd or i got bool");
		} break;
		case BINARY_OP_LESS_EQUALS:
		{
			if (float_kind) value_ref = LLVMBuildFCmp(builder, LLVMRealOLE, lhs, rhs, "btmp");
			else if (int_kind) value_ref = LLVMBuildICmp(builder, LLVMIntSLE, lhs, rhs, "btmp"); //@Determine S / U predicates
			else error_exit("bin_expr <= expected fd or i got bool");
		} break;
		case BINARY_OP_GREATER_EQUALS:
		{
			if (float_kind) value_ref = LLVMBuildFCmp(builder, LLVMRealOGE, lhs, rhs, "btmp");
			else if (int_kind) value_ref = LLVMBuildICmp(builder, LLVMIntSGE, lhs, rhs, "btmp"); //@Determine S / U predicates
			else error_exit("bin_expr >= expected fd or i got bool");
		} break;
		case BINARY_OP_IS_EQUALS:
		{
			if (float_kind) value_ref = LLVMBuildFCmp(builder, LLVMRealOEQ, lhs, rhs, "btmp");
			else if (int_kind) value_ref = LLVMBuildICmp(builder, LLVMIntEQ, lhs, rhs, "btmp"); //@Determine S / U predicates
			else error_exit("bin_expr == expected fd or i got bool");
		} break;
		case BINARY_OP_NOT_EQUALS:
		{
			if (float_kind) value_ref = LLVMBuildFCmp(builder, LLVMRealONE, lhs, rhs, "btmp");
			else if (int_kind) value_ref = LLVMBuildICmp(builder, LLVMIntNE, lhs, rhs, "btmp"); //@Determine S / U predicates
			else error_exit("bin_expr != expected fd or i got bool");
		} break;
		// MathOps [+ - * / %]
		case BINARY_OP_PLUS:
		{
			if (float_kind) value_ref = LLVMBuildFAdd(builder, lhs, rhs, "btmp");
			else if (int_kind) value_ref = LLVMBuildAdd(builder, lhs, rhs, "btmp"); //@Safety NoSignedWrap & NoUnsignedWrap variants exist
			else error_exit("bin_expr + expected fd or i got bool");
		} break;
		case BINARY_OP_MINUS:
		{
			if (float_kind) value_ref = LLVMBuildFSub(builder, lhs, rhs, "btmp");
			else if (int_kind) value_ref = LLVMBuildSub(builder, lhs, rhs, "btmp"); //@Safety NoSignedWrap & NoUnsignedWrap variants exist
			else error_exit("bin_expr + expected fd or i got bool");
		} break;
		case BINARY_OP_TIMES:
		{
			if (float_kind) value_ref = LLVMBuildFMul(builder, lhs, rhs, "btmp");
			else if (int_kind) value_ref = LLVMBuildMul(builder, lhs, rhs, "btmp"); //@Safety NoSignedWrap & NoUnsignedWrap variants exist
			else error_exit("bin_expr * expected fd or i got bool");
		} break;
		case BINARY_OP_DIV:
		{
			if (float_kind) value_ref = LLVMBuildFDiv(builder, lhs, rhs, "btmp");
			//@ SU variants: LLVMBuildSDiv, LLVMBuildExactSDiv, LLVMBuildUDiv, LLVMBuildExactUDiv
			else if (int_kind) value_ref = LLVMBuildSDiv(builder, lhs, rhs, "btmp");
			else error_exit("bin_expr / expected fd or i got bool");
		} break;
		case BINARY_OP_MOD: //@Design float modulo is possible but isnt too usefull
		{
			//@ SU rem variants: LLVMBuildSRem, LLVMBuildURem (using SRem always now)
			if (int_kind) value_ref = LLVMBuildSRem(builder, lhs, rhs, "btmp");
			else error_exit("bin_expr % expected i");
		} break;
		// BitwiseOps [& | ^ << >>]
		case BINARY_OP_BITWISE_AND: // @Design only allow those for uints ideally
		{
			if (int_kind) value_ref = LLVMBuildAnd(builder, lhs, rhs, "btmp");
			else error_exit("bin_expr & expected i");
		} break;
		case BINARY_OP_BITWISE_OR:
		{
			if (int_kind) value_ref = LLVMBuildOr(builder, lhs, rhs, "btmp");
			else error_exit("bin_expr | expected i");
		} break;
		case BINARY_OP_BITWISE_XOR:
		{
			if (int_kind) value_ref = LLVMBuildXor(builder, lhs, rhs, "btmp");
			else error_exit("bin_expr ^ expected i");
		} break;
		case BINARY_OP_BITSHIFT_LEFT:
		{
			if (int_kind) value_ref = LLVMBuildShl(builder, lhs, rhs, "btmp");
			else error_exit("bin_expr << expected i");
		} break;
		case BINARY_OP_BITSHIFT_RIGHT: //@LLVMBuildAShr used for maintaining the sign?
		{
			if (int_kind) value_ref = LLVMBuildLShr(builder, lhs, rhs, "btmp");
			else error_exit("bin_expr >> expected i");
		} break;
		default: error_exit("bin_expr unknown binary op"); break;
		}
	} break;
	}

	if (value_ref == NULL) error_exit("build_expr_value: value_ref is null on return");
	return value_ref;
}

LLVMValueRef LLVM_IR_Builder::build_value_cast(LLVMValueRef value, LLVMTypeRef target_type)
{
	LLVMTypeRef value_type = LLVMTypeOf(value);
	if (value_type == target_type) return value;

	if (type_is_float(value_type) && type_is_float(target_type))
		return LLVMBuildFPCast(builder, value, target_type, "fpcast_val");

	return value;
}

void LLVM_IR_Builder::build_binary_value_cast(LLVMValueRef& value_lhs, LLVMValueRef& value_rhs, LLVMTypeRef type_lhs, LLVMTypeRef type_rhs)
{
	if (type_lhs == type_rhs) return;

	if (type_is_float(type_lhs) && type_is_float(type_rhs))
	{
		if (type_is_f32(type_lhs))
			value_lhs = LLVMBuildFPExt(builder, value_lhs, type_rhs, "fpcast_val");
		else value_rhs = LLVMBuildFPExt(builder, value_rhs, type_lhs, "fpcast_val");
	}
}

Type_Meta LLVM_IR_Builder::get_type_meta(Ast_Type* type)
{
	LLVMTypeRef type_ref = NULL;

	switch (type->tag)
	{
	case Ast_Type::Tag::Basic:
	{
		switch (type->as_basic) 
		{
			case BASIC_TYPE_I8: type_ref = LLVMInt8Type(); break;
			case BASIC_TYPE_U8: type_ref = LLVMInt8Type(); break;
			case BASIC_TYPE_I16: type_ref = LLVMInt16Type(); break;
			case BASIC_TYPE_U16: type_ref = LLVMInt16Type(); break;
			case BASIC_TYPE_I32: type_ref = LLVMInt32Type(); break;
			case BASIC_TYPE_U32: type_ref = LLVMInt32Type(); break;
			case BASIC_TYPE_I64: type_ref = LLVMInt64Type(); break;
			case BASIC_TYPE_U64: type_ref = LLVMInt64Type(); break;
			case BASIC_TYPE_F32: type_ref = LLVMFloatType(); break;
			case BASIC_TYPE_F64: type_ref = LLVMDoubleType(); break;
			case BASIC_TYPE_BOOL: type_ref = LLVMInt1Type(); break;
			//@Undefined for now, might use a struct: case BASIC_TYPE_STRING:
			default: error_exit("get_type_meta: basic type not found"); break;
		}
	} break;
	case Ast_Type::Tag::Custom: //@Notice for now custom type is assumed to be a struct
	{
		auto struct_meta = struct_decl_map.find(type->as_custom.token.string_value, hash_fnv1a_32(type->as_custom.token.string_value));
		if (!struct_meta) error_exit("get_type_meta: custom type not found");
		return Type_Meta{ struct_meta.value().struct_type, true, struct_meta.value().struct_decl, false, NULL };
	} break;
	case Ast_Type::Tag::Pointer:
	{
		Type_Meta ptr_type = get_type_meta(type->as_pointer);
		type_ref = LLVMPointerTypeInContext(LLVMGetGlobalContext(), 0);
		return Type_Meta{ type_ref, false, NULL, true, ptr_type.type };
	} break;
	case Ast_Type::Tag::Array:
	{
		error_exit("get_type_meta: arrays not supported");
		return Type_Meta{};
	} break;
	}

	return Type_Meta{ type_ref, false, NULL, false, NULL };
}

Field_Meta LLVM_IR_Builder::get_field_meta(Ast_Struct_Decl* struct_decl, StringView field_str)
{
	u32 count = 0;
	for (const auto& field : struct_decl->fields)
	{
		if (field.ident.token.string_value == field_str)
			return Field_Meta{ count, get_type_meta(field.type) };
		count += 1;
	}
	error_exit("get_field_meta: failed to find the field");
	return Field_Meta{};
}

Var_Access_Meta LLVM_IR_Builder::get_var_access_meta(Ast_Var* var, Var_Block_Scope* bc)
{
	auto var_meta = bc->find_var(var->ident.token.string_value);
	if (!var_meta) error_exit("get_var_access_meta: failed to find var in scope");
	if (!var->access.has_value()) return Var_Access_Meta{ var_meta.value().var_value, var_meta.value().type_meta.type };

	Ast_Access* access = var->access.value();
	LLVMValueRef ptr = var_meta.value().var_value;
	LLVMTypeRef type = var_meta.value().type_meta.type;
	Ast_Struct_Decl* struct_decl = var_meta.value().type_meta.struct_decl;
	while (access != NULL)
	{
		if (access->tag == Ast_Access::Tag::Array)
		{
			if (!var_meta.value().type_meta.is_pointer)
				error_exit("get_var_access_meta: trying array access on non pointer variable");

			Ast_Array_Access* array_access = access->as_array;
			LLVMValueRef index_value = build_expr_value(array_access->index_expr, bc);
			type = var_meta.value().type_meta.pointer_type;
			ptr = LLVMBuildGEP2(builder, type, ptr, &index_value, 1, "array_access_ptr");
			access = NULL;
		}
		else
		{
			Field_Meta field = get_field_meta(struct_decl, access->as_var->ident.token.string_value);
			ptr = LLVMBuildStructGEP2(builder, type, ptr, field.id, "gep_ptr");
			type = field.type_meta.type;
			struct_decl = field.type_meta.struct_decl;

			if (access->as_var->next.has_value())
				access = access->as_var->next.value();
			else access = NULL;
		}
	}

	return Var_Access_Meta{ ptr, type };
}

bool LLVM_IR_Builder::type_is_int(LLVMTypeRef type) { return LLVMGetTypeKind(type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(type) != 1; }
bool LLVM_IR_Builder::type_is_bool(LLVMTypeRef type) { return LLVMGetTypeKind(type) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(type) == 1; }
bool LLVM_IR_Builder::type_is_float(LLVMTypeRef type) { return LLVMGetTypeKind(type) == LLVMFloatTypeKind || LLVMGetTypeKind(type) == LLVMDoubleTypeKind; }
bool LLVM_IR_Builder::type_is_f32(LLVMTypeRef type) { return LLVMGetTypeKind(type) == LLVMFloatTypeKind; }
bool LLVM_IR_Builder::type_is_f64(LLVMTypeRef type) { return LLVMGetTypeKind(type) == LLVMDoubleTypeKind; }

char* LLVM_IR_Builder::get_c_string(Token& token) //@Unsafe hack to get c string from string view of source file string, need to do smth better
{
	token.string_value.data[token.string_value.count] = 0;
	return (char*)token.string_value.data;
}

void LLVM_IR_Builder::error_exit(const char* message)
{
	printf("backend error: %s.\n", message);
	exit(EXIT_FAILURE);
}

void LLVM_IR_Builder::debug_print_llvm_type(const char* message, LLVMTypeRef type)
{
	char* msg = LLVMPrintTypeToString(type);
	printf("%s %s\n", message, msg);
	LLVMDisposeMessage(msg);
}