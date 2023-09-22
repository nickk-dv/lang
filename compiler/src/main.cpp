#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <vector>

#include "common.cpp"
#include "tokenizer.cpp"
#include "ast.cpp"
#include "debug_printer.cpp"
#include "parser.cpp"
#include "typer.cpp"
#include "checker.cpp"
#include "llvm_backend.cpp"

int main()
{
	Tokenizer lexer = {};

	Timer timer;
	timer.start();
	const char* file_path = "../../test.txt";
	if (!lexer.set_input_from_file(file_path))
	{
		printf("Failed to open a file.\n");
		return 1;
	}
	timer.end("Lexer init");

	timer.start();
	std::vector<Token> tokens = lexer.tokenize();
	timer.end("Lexer");

	debug_print_tokenizer_info(tokens);

	timer.start();
	Parser parser(std::move(tokens));
	timer.end("Parser init");

	timer.start();
	Ast* ast = parser.parse();
	timer.end("Parser");

	if (ast == NULL) return 1;
	debug_print_ast(ast);
	printf("Parse result: Success\n\n");

	timer.start();
	Checker checker = {};
	bool check = checker.check_ast(ast);
	timer.end("Check");
	if (!check)
	{
		printf("Check result: Failed\n");
		return 1;
	}
	printf("Check result: Success\n\n");

	//timer.start();
	//llvm_build(ast);
	//timer.end("LLVM IR build");

	return 0;
}

//@Incomplete basic CLI. Build this up when there are multiple options required
int main_cli(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) 
	{
		printf("Arg: %s\n", argv[i]);
	}
	return 0;
}
