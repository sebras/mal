#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <readline/readline.h>
#include <readline/history.h>

#define PROMPT "user> "

static bool print(char *line)
{
    if (line)
    {
        printf("%s\n", line);
        free(line);
    }

    return line != NULL;
}

static char *eval(char *line)
{
    return line;
}

static char *reed(const char *prompt)
{
    char *line = readline(prompt);
    if (line && *line)
        add_history(line);
    return line;
}

static void repl()
{
    while (print(eval(reed(PROMPT))));
}

int main(int argc, char **argv)
{
    repl();
    return 0;
}
