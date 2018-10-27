#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static const char *seb_read(const char *s)
{
	return s;
}

static const char *seb_eval(const char *s)
{
	return s;
}

static const char *seb_print(const char *s)
{
	return s;
}

static char *seb_rep(const char *s)
{
	s = seb_read(s);
	s = seb_eval(s);
	seb_print(s);
}

static char *prompt(const char *prompt)
{
	char *prevline = NULL;
	char *line = NULL;
	size_t cap = 0;
	size_t len = 0;
	int c;

	printf("%s ", prompt);

	c = fgetc(stdin);
	if (c == EOF)
		return NULL;

	while (c != EOF)
	{
		if (len + 1 > cap)
		{
			cap = cap > 0 ? 2 * cap : 1;
			line = realloc(prevline = line, cap);
			if (line == NULL && cap != 0)
			{
				free(prevline);
				return "Failed to allocate input line buffer";
			}
		}

		if (c == '\n' || c == '\r' || c == '\0')
		{
			line[len++] = '\0';
			c = EOF;
		}
		else
		{
			line[len++] = c;
			c = fgetc(stdin);
		}

	}

	return line;
}

int main(int argc, char **argv)
{
	char *line;
	char *result;

	do
	{
		line = prompt("user>");
		if (line == NULL)
			result = strdup("");
		else
			result = seb_rep(line);
		printf("%s\n", result);
		free(result);
	} while (line != NULL);
}

#if 0
int main(int argc, char **argv)
{
	char *prevline, *line, *result;
	size_t cap, len;
	int c;

	do
	{
		result = NULL;
		line = NULL;
		cap = 0;
		len = 0;

		printf("user> ");

		c = fgetc(stdin);
		while (c != EOF)
		{
			if (len + 1 > cap)
			{
				cap = cap > 0 ? 2 * cap : 1;
				line = realloc(prevline = line, cap);
				if (!line && cap)
				{
					free(prevline);
					result = strdup("cannot allocate input line buffer");
					c = EOF;
					continue;
				}
			}

			if (c == '\n' || c == '\r' || c == '\0')
			{
				line[len++] = '\0';
				c = EOF;
				continue;
			}

			line[len++] = c;
			c = fgetc(stdin);
		}

		if (line && !result)
			result = rep(line);

		if (result && strlen(result))
			printf("%s\n", result);

		free(result);
		result = NULL;

	} while (line != NULL);

	printf("\n");
}
#endif
