#define _GNU_SOURCE

#include <readline/readline.h>
#include <readline/history.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define PROMPT "user> "

static bool print(char *ast, bool readable)
{
        if (!ast)
        {
                printf("\n");
                return false;
        }

        if (strlen(ast))
                printf("%s\n", ast);
        free(ast);

        return true;
}

static char *eval(char *ast)
{
	return ast;
}

static char *parse(char *line)
{
        char *ast;

        if (!line)
                return NULL;

        ast = strdup(line);
        free(line);

        return ast;
}

static char *reed(char *prompt)
{
        char *line = readline(prompt);
        if (line && *line)
                add_history(line);
        return line;
}

static void repl(void)
{
        while (print(eval(parse(reed(PROMPT))), true));
}

int main(int argc, char **argv)
{
        repl();

        return 0;
}
