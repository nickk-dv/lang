#pragma once

#include "token.h"

#include <vector>

struct Ast;

struct Ast_Struct_Declaration;
struct Ast_Enum_Declaration;
struct Ast_Procedure_Declaration;

struct Ast_Block;
struct Ast_Block_Statement;

struct Ast_If;
struct Ast_For;
struct Ast_While;
struct Ast_Break;
struct Ast_Return;
struct Ast_Continue;
struct Ast_Procedure_Call;
struct Ast_Variable_Assignment;
struct Ast_Variable_Declaration;

struct Ast_Literal;
struct Ast_Unary_Expression;
struct Ast_Binary_Expression;

struct Ast
{
	std::vector<Ast_Struct_Declaration> structs;
	std::vector<Ast_Enum_Declaration> enums;
	std::vector<Ast_Procedure_Declaration> functions;
};

struct NameTypePair
{
	Token ident;
	Token type_ident;
};

struct Ast_Struct_Declaration
{
	Token type_ident;
	std::vector<NameTypePair> struct_fields;
};

struct Ast_Enum_Declaration
{
	Token type_ident;
	std::vector<NameTypePair> enum_variants;
};

struct Ast_Procedure_Declaration
{
	Token ident;
	std::vector<NameTypePair> input_parameters;
	std::optional<Token> return_type_ident;
	Ast_Block* block;
};

struct Ast_Block
{
	std::vector<Ast_Block_Statement> statements;
};

enum class BlockStatement
{
	If, For, While, Break, Return, Continue,
	ProcedureCall, VariableAssignment, VariableDeclaration,
};

struct Ast_Block_Statement
{
	BlockStatement tag;
	union
	{
		Ast_If* _if;
		Ast_For* _for;
		Ast_While* _while;
		Ast_Break* _break;
		Ast_Return* _return;
		Ast_Continue* _continue;
		Ast_Procedure_Call* _proc_call;
		Ast_Variable_Assignment* _var_assignment;
		Ast_Variable_Declaration* _var_declaration;
	} statement;
};

struct Ast_If
{
	//@Incomplete
};

struct Ast_For
{
	//@Incomplete
};

struct Ast_While
{
	//@Incomplete
};

struct Ast_Break
{
	Token token;
};

struct Ast_Return
{
	//@Incomplete
};

struct Ast_Continue
{
	Token token;
};

struct Ast_Procedure_Call
{
	//@Incomplete
};

struct Ast_Variable_Assignment
{
	//@Incomplete
};

struct Ast_Variable_Declaration
{
	//@Incomplete
};

struct Ast_Literal
{
	Token token;
};

struct Ast_Unary_Expression
{
	//@Incomplete
};

struct Ast_Binary_Expression
{
	//@Incomplete
};
