#include "lexer.h"

static bool is_number(u8 c)       { return c >= '0' && c <= '9'; }
static bool is_letter(u8 c)       { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static bool is_whitespace(u8 c)   { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static bool is_ident_start(u8 c)  { return c == '_' || is_letter(c); }
static bool is_ident_middle(u8 c) { return c == '_' || is_letter(c) || is_number(c); }

enum class Lexeme
{
	CHAR,
	STRING,
	NUMBER,
	IDENT,
	SYMBOL,
};

Lexeme lex_lexeme(u8 c)
{
	switch (c)
	{
	case '\'': return Lexeme::CHAR;
	case '"': return Lexeme::STRING;
	default:
	{
		if (is_number(c)) return Lexeme::NUMBER;
		if (is_ident_start(c)) return Lexeme::IDENT;
		return Lexeme::SYMBOL;
	}
	}
}

void Lexer::init(StringView source, StringStorage* strings, std::vector<Span>* line_spans)
{
	this->cursor = 0;
	this->source = source;
	this->strings = strings;
	this->line_spans = line_spans;
	this->line_spans->emplace_back(Span { .start = 0, .end = 0 });
}

void Lexer::lex_token_buffer(Token* tokens)
{
	u32 copy_count = this->cursor == 0 ? 0 : TOKEN_LOOKAHEAD;

	for (u32 k = 0; k < copy_count; k++)
	{
		tokens[k] = tokens[TOKEN_BUFFER_SIZE - TOKEN_LOOKAHEAD + k];
	}

	for (u32 k = copy_count; k < TOKEN_BUFFER_SIZE; k++)
	{
		skip_whitespace();

		if (!peek().has_value())
		{
			if (this->line_spans->back().end != this->cursor)
				this->line_spans->back().end = this->cursor - 1;

			for (u32 i = k; i < TOKEN_BUFFER_SIZE; i++)
			{
				tokens[i].type = TokenType::INPUT_END;
			}
			return;
		}

		tokens[k] = lex_token();
	}
}

Token Lexer::lex_token()
{
	Token token = {};
	u8 c = peek().value();
	
	u32 span_start = this->cursor;
	switch (lex_lexeme(c))
	{
	case Lexeme::CHAR:   token = lex_char(); break;
	case Lexeme::STRING: token = lex_string(); break;
	case Lexeme::NUMBER: token = lex_number(); break;
	case Lexeme::IDENT:  token = lex_ident(); break;
	case Lexeme::SYMBOL: token = lex_symbol(); break;
	}
	u32 span_end = this->cursor - 1;
	
	token.span = Span { .start = span_start, .end = span_end };
	return token;
}

Token Lexer::lex_char()
{
	Token token = { .type = TokenType::ERROR };
	consume();

	if (!peek().has_value()) return token; //@err no char literal
	u8 c = peek().value();
	switch (c)
	{
	case '\\':
	{
		consume();
		if (!peek().has_value()) return token; //@err no ecs character
		u8 esc = peek().value();
		switch (esc)
		{
		case 't':  c = '\t'; break;
		case 'r':  c = '\r'; break;
		case 'n':  c = '\n'; break;
		case '0':  c = '\0'; break;
		case '\\': c = '\\'; break;
		case '\'': c = '\''; break;
		default: return token; //@err invalid ecs character
		}
		consume();
	} break;
	case '\'': return token; //@err should contain at least 1 char
	default: consume(); break;
	}

	if (!peek().has_value()) return token; //@err missing '
	if (peek().value() != '\'') return token; //@err missing '
	consume();
	
	token.type = TokenType::INTEGER_LITERAL;
	token.integer_value = c; //@ char literal is represented by int literal currently
	return token;
}

Token Lexer::lex_string()
{
	Token token = { .type = TokenType::ERROR };
	this->strings->start_str();
	consume();

	if (!peek().has_value()) return token; //@err missing "
	while (peek().has_value())
	{
		bool terminate = false;
		u8 c = peek().value();
		switch (c)
		{
		case '\\':
		{
			consume();
			if (!peek().has_value()) return token; //@err no ecs character
			u8 esc = peek().value();
			switch (esc)
			{
			case 't':  this->strings->put_char('\t'); break;
			case 'r':  this->strings->put_char('\r'); break;
			case 'n':  this->strings->put_char('\n'); break;
			case '0':  this->strings->put_char('\0'); break;
			case '\\': this->strings->put_char('\\'); break;
			case '"':  this->strings->put_char('"'); break;
			default: return token; //@err invalid ecs character
			}
			consume();
		} break;
		case '"': terminate = true; break;
		case '\n': return token; //@err missing "
		default:
		{
			this->strings->put_char(c);
			consume();
		} break;
		}
		if (terminate) break;
	}

	if (!peek().has_value()) return token; //@err missing "
	if (peek().value() != '"') return token; //@err missing "
	consume();

	token.type = TokenType::STRING_LITERAL;
	token.string_literal_value = this->strings->end_str();
	return token;
}

Token Lexer::lex_number() //@rework
{
	Token token = { .type = TokenType::ERROR };
	u8 fc = peek().value();
	consume();

	u32 offset = 0;
	bool is_float = false;
	while (peek(offset).has_value())
	{
		u8 c = peek(offset).value();
		if (!is_float && c == '.')
		{
			is_float = true;
		}
		else if (!is_number(c)) break;
		offset += 1;
	}

	if (is_float)
	{
		u64 expected_len = offset + 1;
		u8 last_c = this->source.data[this->cursor + expected_len];
		this->source.data[this->cursor + expected_len] = '\0';
		char* start = (char*)this->source.data + (this->cursor - 1);
		char* end = start + 1;
		f64 float64_value = strtod(start, &end); //@Later replace this with custom to avoid \0 hacks and ensure valid number grammar
		this->source.data[this->cursor + expected_len] = last_c;

		for (u32 i = 0; i < offset; i += 1)
		{
			consume();
		}

		if (end != start)
		{
			token.type = TokenType::FLOAT_LITERAL;
			token.float64_value = float64_value;
		}
	}
	else
	{
		//@catch u64 overflows
		u64 integer = fc - '0';

		while (peek().has_value())
		{
			u8 c = peek().value();
			if (!is_number(c)) break;
			consume();
			integer *= 10;
			integer += c - '0';
		}

		token.type = TokenType::INTEGER_LITERAL;
		token.integer_value = integer;
	}

	return token;
}

Token Lexer::lex_ident()
{
	Token token = { .type = TokenType::ERROR };
	
	u32 ident_start = this->cursor;
	consume();
	while (peek().has_value() && is_ident_middle(peek().value())) consume();
	u32 ident_end = this->cursor;
	
	token.type = TokenType::IDENT;
	token.string_value = StringView { .data = this->source.data + ident_start, .count = ident_end - ident_start };

	TokenType keyword = lex_ident_keyword(token.string_value);
	switch (keyword)
	{
	case TokenType::ERROR: break;
	case TokenType::KEYWORD_TRUE:  { token.type = TokenType::BOOL_LITERAL; token.bool_value = true; } break;
	case TokenType::KEYWORD_FALSE: { token.type = TokenType::BOOL_LITERAL; token.bool_value = false; } break;
	default: token.type = keyword; break;
	}

	return token;
}

Token Lexer::lex_symbol()
{
	Token token = { .type = TokenType::ERROR };
	
	option<TokenType> symbol_1 = lex_symbol_1(peek().value());
	consume();
	if (!symbol_1) return token; //@err invalid symbol
	token.type = symbol_1.value();

	if (!peek().has_value()) return token; 
	option<TokenType> symbol_2 = lex_symbol_2(peek().value(), token.type);
	if (!symbol_2) return token;
	token.type = symbol_2.value();
	consume();

	if (!peek().has_value()) return token;
	option<TokenType> symbol_3 = lex_symbol_3(peek().value(), token.type);
	if (!symbol_3) return token;
	token.type = symbol_3.value();
	consume();

	return token;
}

#include <unordered_map>

static const std::unordered_map<u64, TokenType> keyword_map =
{
	{ hash_ascii_9("struct"),   TokenType::KEYWORD_STRUCT },
	{ hash_ascii_9("enum"),     TokenType::KEYWORD_ENUM },
	{ hash_ascii_9("if"),       TokenType::KEYWORD_IF },
	{ hash_ascii_9("else"),     TokenType::KEYWORD_ELSE },
	{ hash_ascii_9("true"),     TokenType::KEYWORD_TRUE },
	{ hash_ascii_9("false"),    TokenType::KEYWORD_FALSE },
	{ hash_ascii_9("for"),      TokenType::KEYWORD_FOR },
	{ hash_ascii_9("cast"),     TokenType::KEYWORD_CAST },
	{ hash_ascii_9("defer"),    TokenType::KEYWORD_DEFER },
	{ hash_ascii_9("break"),    TokenType::KEYWORD_BREAK },
	{ hash_ascii_9("return"),   TokenType::KEYWORD_RETURN },
	{ hash_ascii_9("switch"),   TokenType::KEYWORD_SWITCH },
	{ hash_ascii_9("continue"), TokenType::KEYWORD_CONTINUE },
	{ hash_ascii_9("sizeof"),   TokenType::KEYWORD_SIZEOF },
	{ hash_ascii_9("import"),   TokenType::KEYWORD_IMPORT },
	{ hash_ascii_9("use"),      TokenType::KEYWORD_USE },
	{ hash_ascii_9("impl"),     TokenType::KEYWORD_IMPL },
	{ hash_ascii_9("self"),     TokenType::KEYWORD_SELF },
	{ hash_ascii_9("i8"),       TokenType::TYPE_I8 },
	{ hash_ascii_9("u8"),       TokenType::TYPE_U8 },
	{ hash_ascii_9("i16"),      TokenType::TYPE_I16 },
	{ hash_ascii_9("u16"),      TokenType::TYPE_U16 },
	{ hash_ascii_9("i32"),      TokenType::TYPE_I32 },
	{ hash_ascii_9("u32"),      TokenType::TYPE_U32 },
	{ hash_ascii_9("i64"),      TokenType::TYPE_I64 },
	{ hash_ascii_9("u64"),      TokenType::TYPE_U64 },
	{ hash_ascii_9("f32"),      TokenType::TYPE_F32 },
	{ hash_ascii_9("f64"),      TokenType::TYPE_F64 },
	{ hash_ascii_9("bool"),     TokenType::TYPE_BOOL },
	{ hash_ascii_9("string"),   TokenType::TYPE_STRING },
};

TokenType Lexer::lex_ident_keyword(StringView str)
{
	if (str.count > 8 || str.count < 2) return TokenType::ERROR;
	u64 hash = hash_str_ascii_9(str);
	bool is_keyword = keyword_map.find(hash) != keyword_map.end();
	return is_keyword ? keyword_map.at(hash) : TokenType::ERROR;
}

option<TokenType> Lexer::lex_symbol_1(u8 c)
{
	switch (c)
	{
	case '.': return TokenType::DOT;
	case ':': return TokenType::COLON;
	case ',': return TokenType::COMMA;
	case ';': return TokenType::SEMICOLON;
	case '{': return TokenType::BLOCK_START;
	case '}': return TokenType::BLOCK_END;
	case '[': return TokenType::BRACKET_START;
	case ']': return TokenType::BRACKET_END;
	case '(': return TokenType::PAREN_START;
	case ')': return TokenType::PAREN_END;
	case '@': return TokenType::AT;
	case '=': return TokenType::ASSIGN;
	case '+': return TokenType::PLUS;
	case '-': return TokenType::MINUS;
	case '*': return TokenType::TIMES;
	case '/': return TokenType::DIV;
	case '%': return TokenType::MOD;
	case '&': return TokenType::BITWISE_AND;
	case '|': return TokenType::BITWISE_OR;
	case '^': return TokenType::BITWISE_XOR;
	case '<': return TokenType::LESS;
	case '>': return TokenType::GREATER;
	case '!': return TokenType::LOGIC_NOT;
	case '~': return TokenType::BITWISE_NOT;
	default: return {};
	}
}

option<TokenType> Lexer::lex_symbol_2(u8 c, TokenType type)
{
	switch (c)
	{
	case '.': if (type == TokenType::DOT)         return TokenType::DOUBLE_DOT;    else return {};
	case ':': if (type == TokenType::COLON)       return TokenType::DOUBLE_COLON;  else return {};
	case '&': if (type == TokenType::BITWISE_AND) return TokenType::LOGIC_AND;     else return {};
	case '|': if (type == TokenType::BITWISE_OR)  return TokenType::LOGIC_OR;      else return {};
	case '<': if (type == TokenType::LESS)        return TokenType::BITSHIFT_LEFT; else return {};
	case '>':
	{
		switch (type)
		{
		case TokenType::MINUS:   return TokenType::ARROW;
		case TokenType::GREATER: return TokenType::BITSHIFT_RIGHT;
		default: return {};
		}
	}
	case '=':
	{
		switch (type)
		{
		case TokenType::ASSIGN:      return TokenType::IS_EQUALS;
		case TokenType::PLUS:        return TokenType::PLUS_EQUALS;
		case TokenType::MINUS:       return TokenType::MINUS_EQUALS;
		case TokenType::TIMES:       return TokenType::TIMES_EQUALS;
		case TokenType::DIV:         return TokenType::DIV_EQUALS;
		case TokenType::MOD:         return TokenType::MOD_EQUALS;
		case TokenType::BITWISE_AND: return TokenType::BITWISE_AND_EQUALS;
		case TokenType::BITWISE_OR:  return TokenType::BITWISE_OR_EQUALS;
		case TokenType::BITWISE_XOR: return TokenType::BITWISE_XOR_EQUALS;
		case TokenType::LESS:        return TokenType::LESS_EQUALS;
		case TokenType::GREATER:     return TokenType::GREATER_EQUALS;
		case TokenType::LOGIC_NOT:   return TokenType::NOT_EQUALS;
		default: return {};
		}
	}
	default: return {};
	}
}

option<TokenType> Lexer::lex_symbol_3(u8 c, TokenType type)
{
	switch (c)
	{
	case '=':
	{
		switch (type)
		{
		case TokenType::BITSHIFT_LEFT:  return TokenType::BITSHIFT_LEFT_EQUALS;
		case TokenType::BITSHIFT_RIGHT: return TokenType::BITSHIFT_RIGHT_EQUALS;
		default: return {};
		}
	}
	default: return {};
	}
}

void Lexer::skip_whitespace()
{
	while (peek().has_value())
	{
		u8 c = peek().value();
		if (is_whitespace(c))
		{
			if (c == '\n')
			{
				this->line_spans->back().end = this->cursor;
				this->line_spans->emplace_back(Span{ .start = this->cursor + 1, .end = this->cursor + 1 });
			}
			consume();
		}
		else if (c == '/' && peek(1).has_value() && peek(1).value() == '/')
		{
			consume();
			consume();
			while (peek().has_value() && peek().value() != '\n') consume();
		}
		else if (c == '/' && peek(1).has_value() && peek(1).value() == '*')
		{
			consume();
			consume();
			u32 depth = 1;
			while (peek().has_value() && depth != 0)
			{
				u8 mc = peek().value();
				if (mc == '\n')
				{
					this->line_spans->back().end = this->cursor;
					this->line_spans->emplace_back(Span{ .start = this->cursor + 1, .end = this->cursor + 1 });
				}
				consume();

				if (mc == '/' && peek().has_value() && peek().value() == '*')
				{
					consume();
					depth += 1;
				}
				else if (mc == '*' && peek().has_value() && peek().value() == '/')
				{
					consume();
					depth -= 1;
				}
			}
		}
		else break;
	}
}

void Lexer::consume()
{
	this->cursor += 1;
}

option<u8> Lexer::peek(u32 offset)
{
	if (this->cursor + offset >= this->source.count) return {};
	return this->source.data[this->cursor + offset];
}
