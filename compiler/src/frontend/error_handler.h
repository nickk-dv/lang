#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include "general/general.h"

struct Span;
struct Ast;
struct Token;
struct ErrorMessage;
struct Check_Context;
enum class TokenType;
enum class Error;

bool err_get_status();
void err_report(Error error);
void err_report_parse(Ast* source, TokenType expected, option<const char*> in, Token token);
void err_context(Check_Context* cc);
void err_context(Check_Context* cc, Span span);
void err_context(const char* message);
void err_internal(const char* message);

static ErrorMessage err_get_message(Error error);

struct Span
{
	u32 start;
	u32 end;
};

struct ErrorMessage
{
	const char* error;
	const char* hint;
};

enum class Error
{
	COMPILER_INTERNAL,
	
	OS_DIR_CREATE_FAILED,
	OS_FILE_CREATE_FAILED,
	OS_FILE_OPEN_FAILED,
	OS_FILE_READ_FAILED,
	
	CMD_NO_ARGS,
	CMD_INVALID,
	CMD_NEW_DIR_ALREADY_EXIST,
	CMD_NEW_GIT_NOT_INSTALLED,
	CMD_NEW_GIT_INIT_FAILED,
	PARSE_SRC_DIR_NOT_FOUND,
	
	MAIN_FILE_NOT_FOUND,
	MAIN_PROC_NOT_FOUND,
	MAIN_PROC_EXTERNAL,
	MAIN_PROC_VARIADIC,
	MAIN_NOT_ZERO_PARAMS,
	MAIN_PROC_NO_RETURN_TYPE,
	MAIN_PROC_WRONG_RETURN_TYPE,
	
	DECL_SYMBOL_ALREADY_DECLARED,
	DECL_IMPORT_PATH_NOT_FOUND,
	DECL_USE_SYMBOL_NOT_FOUND,
	DECL_STRUCT_DUPLICATE_FIELD,
	DECL_STRUCT_SELF_STORAGE,
	DECL_ENUM_ZERO_VARIANTS,
	DECL_ENUM_NON_INTEGER_TYPE,
	DECL_ENUM_DUPLICATE_VARIANT,
	DECL_PROC_DUPLICATE_PARAM,
	
	RESOLVE_IMPORT_NOT_FOUND,
	RESOLVE_TYPE_NOT_FOUND,
	RESOLVE_TYPE_ARRAY_ZERO_SIZE,
	RESOLVE_VAR_GLOBAL_NOT_FOUND,
	RESOLVE_ENUM_NOT_FOUND,
	RESOLVE_ENUM_VARIANT_NOT_FOUND,
	RESOLVE_PROC_NOT_FOUND,
	RESOLVE_ARRAY_WRONG_CONTEXT,
	RESOLVE_ARRAY_TYPE_MISMATCH,
	RESOLVE_ARRAY_NO_CONTEXT,
	RESOLVE_STRUCT_NOT_FOUND,
	RESOLVE_STRUCT_WRONG_CONTEXT,
	RESOLVE_STRUCT_TYPE_MISMATCH,
	RESOLVE_STRUCT_NO_CONTEXT,
	
	CFG_NOT_ALL_PATHS_RETURN,
	CFG_UNREACHABLE_STATEMENT,
	CFG_NESTED_DEFER,
	CFG_RETURN_INSIDE_DEFER,
	CFG_BREAK_INSIDE_DEFER,
	CFG_CONTINUE_INSIDE_DEFER,
	CFG_BREAK_OUTSIDE_LOOP,
	CFG_CONTINUE_OUTSIDE_LOOP,

	VAR_LOCAL_NOT_FOUND,
	RETURN_EXPECTED_NO_EXPR,
	RETURN_EXPECTED_EXPR,
	SWITCH_INCORRECT_EXPR_TYPE,
	SWITCH_ZERO_CASES,
	VAR_DECL_ALREADY_IS_GLOBAL,
	VAR_DECL_ALREADY_IN_SCOPE,

	TYPE_MISMATCH,
	EXPR_EXPECTED_CONSTANT,
	CONST_PROC_IS_NOT_CONST,
	CONST_VAR_IS_NOT_GLOBAL,
	CONSTEVAL_DEPENDENCY_CYCLE,

	CAST_EXPR_NON_BASIC_TYPE,
	CAST_EXPR_BOOL_BASIC_TYPE,
	CAST_EXPR_STRING_BASIC_TYPE,
	CAST_INTO_BOOL_BASIC_TYPE,
	CAST_INTO_STRING_BASIC_TYPE,
	CAST_REDUNDANT_FLOAT_CAST,
	CAST_REDUNDANT_INTEGER_CAST,
	CAST_FOLD_REDUNDANT_INT_CAST,
	CAST_FOLD_REDUNDANT_FLOAT_CAST,

	TEMP_VAR_ASSIGN_OP,
};

#endif
