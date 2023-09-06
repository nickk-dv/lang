#include "lexer.h"

#include "common.h"
#include "error.h"
#include "token.h"

bool is_letter(u8 c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool is_number(u8 c) { return c >= '0' && c <= '9'; }
bool is_ident(u8 c)  { return is_letter(c) || (c == '_') || is_number(c); }
bool is_whitespace(u8 c) { return c == ' ' || c == '\t'; }
bool is_line_break(u8 c) { return c == '\r' || c == '\n'; }

bool is_first_of_ident(u8 c)  { return is_letter(c) || (c == '_'); }
bool is_first_of_number(u8 c) { return is_number(c); }
bool is_first_of_string(u8 c) { return c == '"'; }
bool is_first_of_symbol(u8 c) { return token_get_1_symbol_token_type(c) != TOKEN_ERROR; }

bool Lexer::set_input_from_file(const char* file_path)
{
	input_cursor = 0;
	return os_file_read_all(file_path, &input);
}

LineInfo Lexer::get_next_line()
{
	u64 count = input.count;
	u64 i = input_cursor;

	LineInfo line = { i, i, 0 };

	if (i >= count) 
	{
		line.is_valid = false;
		return line;
	}

	while (i < count && is_whitespace(input.data[i]))
	{
		line.leading_spaces += 1;
		i++;
	}

	line.end_cursor = i;

	while (i < count && !is_line_break(input.data[i]))
	{
		line.end_cursor = i;
		line.is_empty = false;
		i++;
	}

	if (i < count && input.data[i] == '\r') i++;
	if (i < count && input.data[i] == '\n') i++;
	input_cursor = i;

	line.start_cursor += line.leading_spaces;

	return line;
}

LexemeType Lexer::get_lexeme_type(u8 c)
{
	if (is_first_of_ident(c))  return LEXEME_IDENT;
	if (is_first_of_number(c)) return LEXEME_NUMBER;
	if (is_first_of_string(c)) return LEXEME_STRING;
	if (is_first_of_symbol(c)) return LEXEME_SYMBOL;
	return LEXEME_ERROR;
}

void Lexer::tokenize()
{
	LineInfo line = {};
	u32 current_line_number = 0;

	while (line.is_valid)
	{
		line = get_next_line();
		current_line_number += 1;
		if (line.is_empty) continue;

		for (u64 i = line.start_cursor; i <= line.end_cursor; )
		{
			u8 fc = input.data[i];

			if (is_whitespace(fc))
			{
				i++;
				continue;
			}

			LexemeType type = get_lexeme_type(fc);
			u64 lexeme_start = i;
			u64 lexeme_end = i + 1;

			Token token = {};
			token.l0 = current_line_number;
			token.c0 = (u32)(1 + i - (line.start_cursor - line.leading_spaces));

			switch (type)
			{
				case LEXEME_IDENT:
				{
					while (lexeme_end <= line.end_cursor)
					{
						u8 c = input.data[lexeme_end];
						if (!is_ident(c)) break;
						lexeme_end += 1;
					}

					token.type = TOKEN_IDENT;
					token.string_value.data = input.data + lexeme_start;
					token.string_value.count = lexeme_end - lexeme_start;

					TokenType keyword = token_get_keyword_token_type(token.string_value);
					if (keyword != TOKEN_ERROR) token.type = keyword;

					i = lexeme_end;
					lexeme_end -= 1;
				} break;
				case LEXEME_NUMBER:
				{
					while (lexeme_end <= line.end_cursor)
					{
						u8 c = input.data[lexeme_end];
						if (!is_number(c)) break;
						lexeme_end += 1;
					}

					token.type = TOKEN_NUMBER;

					i = lexeme_end;
					lexeme_end -= 1;
				} break;
				case LEXEME_STRING:
				{
					bool terminated = false;

					while (lexeme_end <= line.end_cursor)
					{
						u8 c = input.data[lexeme_end];
						lexeme_end += 1;
						if (c == '"') { terminated = true; break; }
					}

					token.type = TOKEN_STRING;
					token.string_value.data = input.data + lexeme_start;
					token.string_value.count = lexeme_end - lexeme_start;

					i = lexeme_end;
					lexeme_end -= 1;

					if (!terminated)
					{
						error_report(LEXER_ERROR_STRING_NOT_TERMINATED, token);
						token.type = TOKEN_ERROR;
					}
				} break;
				case LEXEME_SYMBOL:
				{
					token.type = token_get_1_symbol_token_type(fc);

					if (lexeme_end <= line.end_cursor)
					{
						u8 c = input.data[lexeme_end];

						TokenType symbol = token_get_2_symbol_token_type(fc, c);
						if (symbol != TOKEN_ERROR) 
						{
							token.type = symbol;
							lexeme_end += 1;
						}
					}

					i = lexeme_end;
					lexeme_end -= 1;
				} break;
				case LEXEME_ERROR:
				{
					i++;
					error_report(LEXER_ERROR_INVALID_CHARACTER, token);
				} break;
			}

			if (token.type != TOKEN_ERROR)
				tokens.emplace_back(token);
		}
	}

	Token token_end = {};
	token_end.type = TOKEN_INPUT_END;
	tokens.emplace_back(token_end);
}

void Lexer::print_debug_metrics()
{
	printf("Lexer: TokenCount:          %llu \n", tokens.size());
	printf("Lexer: MemoryRequired (Mb): %f \n", double(sizeof(Token) * tokens.size()) / (1024.0 * 1024.0));
	printf("Lexer: MemoryUsed     (Mb): %f \n\n", double(sizeof(Token) * tokens.capacity()) / (1024.0 * 1024.0));
}
