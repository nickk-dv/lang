
enum TokenType
{
	//[Lexemes]
	TOKEN_IDENT,              // name
	TOKEN_NUMBER,             // 10
	TOKEN_STRING,             // "string"
	TOKEN_BOOL_LITERAL,       // true false

	//[Keywords]
	TOKEN_KEYWORD_STRUCT,     // struct
	TOKEN_KEYWORD_ENUM,       // enum
	TOKEN_KEYWORD_FN,         // fn
	TOKEN_KEYWORD_IF,         // if
	TOKEN_KEYWORD_ELSE,       // else
	TOKEN_KEYWORD_TRUE,       // true
	TOKEN_KEYWORD_FALSE,      // false
	TOKEN_KEYWORD_FOR,        // for
	TOKEN_KEYWORD_BREAK,      // break
	TOKEN_KEYWORD_RETURN,     // return
	TOKEN_KEYWORD_CONTINUE,   // continue

	//[Types]
	TOKEN_TYPE_I8,            // i8
	TOKEN_TYPE_U8,            // u8
	TOKEN_TYPE_I16,           // i16
	TOKEN_TYPE_U16,           // u16
	TOKEN_TYPE_I32,           // i32
	TOKEN_TYPE_U32,           // u32
	TOKEN_TYPE_I64,           // i64
	TOKEN_TYPE_U64,           // u64
	TOKEN_TYPE_F32,           // f32
	TOKEN_TYPE_F64,           // f64
	TOKEN_TYPE_BOOL,          // bool
	TOKEN_TYPE_STRING,        // string

	//[Punctuation]
	TOKEN_DOT,                // .
	TOKEN_COMMA,              // ,
	TOKEN_COLON,              // :
	TOKEN_SEMICOLON,          // ;
	TOKEN_BLOCK_START,        // {
	TOKEN_BLOCK_END,          // }
	TOKEN_BRACKET_START,      // [
	TOKEN_BRACKET_END,        // ]
	TOKEN_PAREN_START,        // (
	TOKEN_PAREN_END,          // )
	TOKEN_DOUBLE_COLON,       // ::

	//[Operators]
	TOKEN_ASSIGN,             // =
	TOKEN_PLUS,               // +
	TOKEN_MINUS,              // -
	TOKEN_TIMES,              // *
	TOKEN_DIV,                // /
	TOKEN_MOD,                // %
	TOKEN_BITWISE_AND,        // &
	TOKEN_BITWISE_OR,         // |
	TOKEN_BITWISE_XOR,        // ^
	TOKEN_LESS,               // <
	TOKEN_GREATER,            // >
	TOKEN_LOGIC_NOT,          // !
	TOKEN_IS_EQUALS,          // ==
	TOKEN_PLUS_EQUALS,        // +=
	TOKEN_MINUS_EQUALS,       // -=
	TOKEN_TIMES_EQUALS,       // *=
	TOKEN_DIV_EQUALS,         // /=
	TOKEN_MOD_EQUALS,         // %=
	TOKEN_BITWISE_AND_EQUALS, // &=
	TOKEN_BITWISE_OR_EQUALS,  // |=
	TOKEN_BITWISE_XOR_EQUALS, // ^=
	TOKEN_LESS_EQUALS,        // <=
	TOKEN_GREATER_EQUALS,     // >=
	TOKEN_NOT_EQUALS,         // !=
	TOKEN_LOGIC_AND,          // &&
	TOKEN_LOGIC_OR,           // ||
	TOKEN_BITWISE_NOT,        // ~
	TOKEN_BITSHIFT_LEFT,      // <<
	TOKEN_BITSHIFT_RIGHT,     // >>
	TOKEN_BITSHIFT_LEFT_EQUALS,  // <<=
	TOKEN_BITSHIFT_RIGHT_EQUALS, // >>=

	TOKEN_INPUT_END,
	TOKEN_ERROR,
};

struct Token
{
	TokenType type = TOKEN_ERROR;
	u32 l0 = 0;
	u32 c0 = 0;

	union
	{
		bool bool_value;
		double float64_value;
		u64 integer_value;
		StringView string_value;
	};
};

struct LineInfo
{
	u64 start_cursor = 0;
	u64 end_cursor = 0;
	u32 leading_spaces = 0;
	bool is_valid = true;
	bool is_empty = true;
};

struct Tokenizer
{
	bool set_input_from_file(const char* file_path);
	std::vector<Token> tokenize();

	LineInfo get_next_line();
	TokenType get_keyword_token_type(const StringView& str);
	bool is_letter(u8 c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
	bool is_number(u8 c) { return c >= '0' && c <= '9'; }
	bool is_ident(u8 c) { return is_letter(c) || (c == '_') || is_number(c); }
	bool is_whitespace(u8 c) { return c == ' ' || c == '\t'; }
	bool is_line_break(u8 c) { return c == '\r' || c == '\n'; }

	String input;
	u64 input_cursor = 0;
};

bool Tokenizer::set_input_from_file(const char* file_path)
{
	input_cursor = 0;
	return os_file_read_all(file_path, &input);
}

std::vector<Token> Tokenizer::tokenize()
{
	std::vector<Token> tokens;
	//@Performance testing pre allocation of vector
	tokens.reserve(770000);

	TokenType c_to_sym[128] = {};
	for (u8 i = 0; i < 128; i++)
	{
		c_to_sym[i] = TOKEN_ERROR;
	}

	c_to_sym['.'] = TOKEN_DOT;
	c_to_sym[','] = TOKEN_COMMA;
	c_to_sym[':'] = TOKEN_COLON;
	c_to_sym[';'] = TOKEN_SEMICOLON;
	c_to_sym['{'] = TOKEN_BLOCK_START;
	c_to_sym['}'] = TOKEN_BLOCK_END;
	c_to_sym['['] = TOKEN_BRACKET_START;
	c_to_sym[']'] = TOKEN_BRACKET_END;
	c_to_sym['('] = TOKEN_PAREN_START;
	c_to_sym[')'] = TOKEN_PAREN_END;
	c_to_sym['='] = TOKEN_ASSIGN;
	c_to_sym['+'] = TOKEN_PLUS;
	c_to_sym['-'] = TOKEN_MINUS;
	c_to_sym['*'] = TOKEN_TIMES;
	c_to_sym['/'] = TOKEN_DIV;
	c_to_sym['%'] = TOKEN_MOD;
	c_to_sym['&'] = TOKEN_BITWISE_AND;
	c_to_sym['|'] = TOKEN_BITWISE_OR;
	c_to_sym['^'] = TOKEN_BITWISE_XOR;
	c_to_sym['<'] = TOKEN_LESS;
	c_to_sym['>'] = TOKEN_GREATER;
	c_to_sym['!'] = TOKEN_LOGIC_NOT;
	c_to_sym['~'] = TOKEN_BITWISE_NOT;

	enum LexemeType
	{
		LEXEME_IDENT,
		LEXEME_NUMBER,
		LEXEME_STRING,
		LEXEME_SYMBOL,
		LEXEME_ERROR
	};

	LexemeType lexeme_types[128] = {};
	for (u8 c = 0; c < 128; c++)
	{
		if (is_letter(c) || (c == '_')) lexeme_types[c] = LEXEME_IDENT;
		else if (c_to_sym[c] != TOKEN_ERROR) lexeme_types[c] = LEXEME_SYMBOL;
		else if (is_number(c)) lexeme_types[c] = LEXEME_NUMBER;
		else if (c == '"') lexeme_types[c] = LEXEME_STRING;
		else lexeme_types[c] = LEXEME_ERROR;
	}

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

			LexemeType type = fc < 128 ? lexeme_types[fc] : LEXEME_ERROR;
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

					TokenType keyword = get_keyword_token_type(token.string_value);
					if (keyword != TOKEN_ERROR) token.type = keyword;

					i = lexeme_end;
					lexeme_end -= 1;
				} break;
				case LEXEME_NUMBER:
				{
					u64 integer = fc - '0';

					while (lexeme_end <= line.end_cursor)
					{
						u8 c = input.data[lexeme_end];
						if (!is_number(c)) break;
						lexeme_end += 1;

						integer *= 10;
						integer += c - '0';
					}

					token.type = TOKEN_NUMBER;
					token.integer_value = integer;

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
						//error_report(LEXER_ERROR_STRING_NOT_TERMINATED, token);
						token.type = TOKEN_ERROR;
					}
				} break;
				case LEXEME_SYMBOL:
				{
					token.type = c_to_sym[fc];

					if (lexeme_end <= line.end_cursor)
					{
						u8 c = input.data[lexeme_end];

						constexpr u32 equal_composable_symbol_token_offset = 12;
						constexpr u32 double_composable_symbol_token_offset = 18;
						constexpr u32 bitshift_to_bitshift_equals_offset = 2;

						u32 sym2 = TOKEN_ERROR;
						if (c == '=' && token.type >= TOKEN_ASSIGN && token.type <= TOKEN_LOGIC_NOT) sym2 = token.type + equal_composable_symbol_token_offset;
						else if ((c == fc) && (c == '&' || c == '|' || c == '<' || c == '>'))
						{
							sym2 = token.type + double_composable_symbol_token_offset;
							if (lexeme_end + 1 <= line.end_cursor && input.data[lexeme_end + 1] == '=')
							{
								sym2 += bitshift_to_bitshift_equals_offset;
								lexeme_end += 1;
							}
						}
						else if (c == ':' && fc == ':') sym2 = TOKEN_DOUBLE_COLON;

						if (sym2 != TOKEN_ERROR)
						{
							token.type = (TokenType)sym2;
							lexeme_end += 1;
						}
					}

					i = lexeme_end;
					lexeme_end -= 1;
				} break;
				case LEXEME_ERROR:
				{
					i++;
					//error_report(LEXER_ERROR_INVALID_CHARACTER, token); @Error handling disabled single char errors
				} break;
			}

			if (token.type != TOKEN_ERROR)
				tokens.emplace_back(token);
		}
	}

	Token token_end = {};
	token_end.type = TOKEN_INPUT_END;
	tokens.emplace_back(token_end);

	return tokens;
}

LineInfo Tokenizer::get_next_line()
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
	bool comment_started = false;

	while (i < count && !is_line_break(input.data[i]))
	{
		if (!comment_started)
		{
			comment_started = input.data[i] == '/' && ((i + 1) < count) && input.data[i + 1] == '/';
			if (!comment_started)
			{
				line.end_cursor = i;
				line.is_empty = false;
			}
		}

		i++;
	}

	if (i < count && input.data[i] == '\r') i++;
	if (i < count && input.data[i] == '\n') i++;
	input_cursor = i;

	line.start_cursor += line.leading_spaces;

	return line;
}

//@Performance find a way to not use maps an use logic or table lookups instead
static const std::unordered_map<u64, TokenType> keyword_hash_to_token_type =
{
	{ hash_ascii_9("struct"),   TOKEN_KEYWORD_STRUCT },
	{ hash_ascii_9("enum"),     TOKEN_KEYWORD_ENUM },
	{ hash_ascii_9("fn"),       TOKEN_KEYWORD_FN },
	{ hash_ascii_9("if"),       TOKEN_KEYWORD_IF },
	{ hash_ascii_9("else"),     TOKEN_KEYWORD_ELSE },
	{ hash_ascii_9("true"),     TOKEN_KEYWORD_TRUE },
	{ hash_ascii_9("false"),    TOKEN_KEYWORD_FALSE },
	{ hash_ascii_9("for"),      TOKEN_KEYWORD_FOR },
	{ hash_ascii_9("break"),    TOKEN_KEYWORD_BREAK },
	{ hash_ascii_9("return"),   TOKEN_KEYWORD_RETURN },
	{ hash_ascii_9("continue"), TOKEN_KEYWORD_CONTINUE },

	{ hash_ascii_9("i8"),       TOKEN_TYPE_I8 },
	{ hash_ascii_9("u8"),       TOKEN_TYPE_U8 },
	{ hash_ascii_9("i16"),      TOKEN_TYPE_I16 },
	{ hash_ascii_9("u16"),      TOKEN_TYPE_U16 },
	{ hash_ascii_9("i32"),      TOKEN_TYPE_I32 },
	{ hash_ascii_9("u32"),      TOKEN_TYPE_U32 },
	{ hash_ascii_9("i64"),      TOKEN_TYPE_I64 },
	{ hash_ascii_9("u64"),      TOKEN_TYPE_U64 },
	{ hash_ascii_9("f32"),      TOKEN_TYPE_F32 },
	{ hash_ascii_9("f64"),      TOKEN_TYPE_F64 },
	{ hash_ascii_9("bool"),     TOKEN_TYPE_BOOL },
	{ hash_ascii_9("string"),   TOKEN_TYPE_STRING },
};

TokenType Tokenizer::get_keyword_token_type(const StringView& str)
{
	if (str.count > 8) return TOKEN_ERROR;
	u64 hash = string_hash_ascii_9(str);
	bool is_keyword = keyword_hash_to_token_type.find(hash) != keyword_hash_to_token_type.end();
	return is_keyword ? keyword_hash_to_token_type.at(hash) : TOKEN_ERROR;
}
