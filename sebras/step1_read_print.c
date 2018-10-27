#define _GNU_SOURCE

#include <readline/readline.h>
#include <readline/history.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static char *ensure(char *p, size_t s)
{
	return realloc(p, strlen(p) + 1 + s);
}

struct token
{
	struct token *next;
	char *s;
};

static struct token formaterrortoken3 = { NULL, ")" };
static struct token formaterrortoken2 = { &formaterrortoken3, NULL };
static struct token formaterrortoken1 = { &formaterrortoken2, "error" };
static struct token formaterrortoken0 = { &formaterrortoken1, "(" };

static void free_tokens(struct token *tokens)
{
	struct token *next;

	if (tokens == &formaterrortoken0)
		return;

	while (tokens)
	{
		next = tokens->next;
		free(tokens->s);
		free(tokens);
		tokens = next;
	}
}

#define new_error_token(msg) new_error_token_imp("\"" msg "\"")

static struct token *new_error_token_imp(char *msg)
{
	formaterrortoken2.s = msg;
	return &formaterrortoken0;
}

static struct token *append_token(struct token *tokens, struct token ***tail, char *s, char *e)
{
	struct token *token;

	token = malloc(sizeof(struct token));
	if (!token)
	{
		**tail = NULL;
		free_tokens(tokens);
		return new_error_token("cannot allocate token");
	}

	token->s = strndup(s, e - s);
	token->next = NULL;

	if (!token->s)
	{
		**tail = NULL;
		free_tokens(token);
		free_tokens(tokens);
		return new_error_token("cannot format token");
	}

	**tail = token;
	*tail = &token->next;

	return NULL;
}

#define WHITESPACE "\t\n\v\f\r "
#define COMMA ","
#define SPLICEUNQOTE "~@"
#define SPECIAL "[]{}()'`~^@"
#define QUOTE '"'
#define ESCAPEDQUOTE "\\\""
#define SEMICOLON ';'
#define NONSYMBOLCHARS WHITESPACE "[]{}()'\"`,;"

static struct token *tokenize(char *code)
{
	struct token *tokens = NULL;
	struct token **tail = &tokens;
	char *e = code + strlen(code);
	char *p = code;

	while (p < e)
	{
		struct token *err = NULL;

		while (p < e && strchr(WHITESPACE COMMA, *p))
			p++;

		if (e - p >= 2 && !strcmp(SPLICEUNQOTE, p))
		{
			err = append_token(tokens, &tail, p, p + 2);
			p += 2;
		}
		else if (e - p >= 1 && strchr(SPECIAL, *p))
		{
			err = append_token(tokens, &tail, p, p + 1);
			p++;
		}
		else if (e - p >= 1 && *p == QUOTE)
		{
			char *start = p++;
			char *s;

			while (p < e && *p != QUOTE)
			{
				if (!strncmp(p, ESCAPEDQUOTE, 2))
					p += 2;
				else if (*p != QUOTE)
					p++;
			}

			if (p == e)
			{
				free_tokens(tokens);
				return new_error_token("unterminated string encountered");
			}

			if (p < e && *p != QUOTE)
			{
				free_tokens(tokens);
				return new_error_token("string not terminated by quote");
			}

			p++;

			start = s = strndup(start, p - start);

			while (*s)
			{
				if (*s == '\\')
				{
					if (strlen(s) <= 1)
					{
						free_tokens(tokens);
						return new_error_token("unterminated string escape sequence");
					}

					switch (s[1])
					{
					case 'a': *s = '\a'; break;
					case 'b': *s = '\b'; break;
					case 'e': *s = '\x1b'; break;
					case 'f': *s = '\f'; break;
					case 'n': *s = '\n'; break;
					case 'r': *s = '\r'; break;
					case 't': *s = '\t'; break;
					case 'v': *s = '\v'; break;
					case '\\': *s = '\\'; break;
					case '"': *s = '"'; break;
					default:
						free_tokens(tokens);
						return new_error_token("invalid string escape sequence");
					}
					memmove(&s[1], &s[2], strlen(s));
				}

				s++;
			}

			err = append_token(tokens, &tail, start, s);

			free(start);
		}
		else if (e - p >= 1 && *p == SEMICOLON)
			p = e;
		else if (e - p >= 1)
		{
			char *start = p++;

			while (p < e && !strchr(NONSYMBOLCHARS, *p))
				p++;
			err = append_token(tokens, &tail, start, p);
		}

		if (err)
			return err;
	}

	return tokens;
}

struct node
{
	enum
	{
		ERROR = -1,
		FALSE = 0,
		TRUE = 1,
		NIL,
		INTEGER,
		REAL,
		SYMBOL,
		STRING,
		LIST,
		VECTOR,
		KEYWORD,
	} type;
	struct node *down, *next;
	union {
		char *error;
		long long integer;
		double real;
		char *string;
		char *symbol;
		char *keyword;
	} u;
};

struct node parseerror;

static struct node *new_error(char *msg)
{
	parseerror.type = ERROR;
	parseerror.u.error = msg;
	return &parseerror;
}

static void free_node(struct node *node)
{
	struct node *next;

	if (node == &parseerror)
		return;

	while (node)
	{
		next = node->next;

		switch (node->type)
		{
		case VECTOR:
		case LIST:
			free_node(node->down);
			break;
		case SYMBOL:
			free(node->u.symbol);
			break;
		case STRING:
			free(node->u.string);
			break;
		case KEYWORD:
			free(node->u.keyword);
			break;
		}
		free(node);

		node = next;
	}
}

static struct node *new_node(int type)
{
	struct node *node = malloc(sizeof(struct node));
	if (!node)
		return new_error("error allocating node");
	node->next = NULL;
	node->down = NULL;
	node->type = type;
	return node;
}

static struct node *new_nil(void)
{
	return new_node(NIL);
}

static struct node *new_true(void)
{
	return new_node(TRUE);
}

static struct node *new_false(void)
{
	return new_node(FALSE);
}

static struct node *new_integer(long long integer)
{
	struct node *node = new_node(INTEGER);
	if (node->type == ERROR)
		return node;

	node->u.integer = integer;
	return node;
}

static struct node *new_real(double real)
{
	struct node *node = new_node(REAL);
	if (node->type == ERROR)
		return node;

	node->u.real = real;
	return node;
}

static struct node *new_string(char *string)
{
	struct node *node = new_node(STRING);
	if (node->type == ERROR)
		return node;

	node->u.string = strndup(&string[1], strlen(string) - 2);
	if (!node->u.string)
	{
		free_node(node);
		return new_error("error allocating node string");
	}
	return node;
}

static struct node *new_keyword(char *string)
{
	struct node *node = new_node(KEYWORD);
	if (node->type == ERROR)
		return node;

	node->u.keyword = strndup(&string[1], strlen(string) - 1);
	if (!node->u.keyword)
	{
		free_node(node);
		return new_error("error allocating node keyword");
	}
	return node;
}

static struct node *new_symbol(char *symbol)
{
	struct node *node = new_node(SYMBOL);
	if (node->type == ERROR)
		return node;

	node->u.symbol = strdup(symbol);
	if (!node->u.symbol)
	{
		free_node(node);
		return new_error("error allocating node symbol");
	}
	return node;
}

static struct node *new_list(struct node *down)
{
	struct node *node = new_node(LIST);
	if (node->type == ERROR)
		return node;

	node->down = down;
	return node;
}

static struct node *new_vector(struct node *down)
{
	struct node *node = new_node(VECTOR);
	if (node->type == ERROR)
		return node;

	node->down = down;
	return node;
}

static struct node *new_quote(struct node *down)
{
	struct node *quote = new_symbol("quote");
	if (quote->type == ERROR)
		return quote;

	quote->next = down;
	return new_list(quote);
}

static struct node *new_quasiquote(struct node *down)
{
	struct node *quote = new_symbol("quasiquote");
	if (quote->type == ERROR)
		return quote;

	quote->next = down;
	return new_list(quote);
}

static struct node *new_unquote(struct node *down)
{
	struct node *quote = new_symbol("unquote");
	if (quote->type == ERROR)
		return quote;

	quote->next = down;
	return new_list(quote);
}

static struct node *new_splicequote(struct node *down)
{
	struct node *quote = new_symbol("splice-unquote");
	if (quote->type == ERROR)
		return quote;

	quote->next = down;
	return new_list(quote);
}

static int is_integer(char *s)
{
	char *end = NULL;

	errno = 0;
	(void) strtod(s, &end);
	if (errno == ERANGE)
		return 0;

	if (end && *end != '\0')
		return 0;

	return 1;
}

static int is_real(char *s)
{
	char *end = NULL;

	errno = 0;
	(void) strtod(s, &end);
	if (errno == ERANGE)
		return 0;

	if (end && *end != '\0')
		return 0;

	return 1;
}

static struct node *read_atom(struct token **tokens)
{
	struct node *node;
	char *s = (*tokens)->s;

	if (!strcmp(s, "nil"))
		node = new_nil();
	else if (!strcmp(s, "true"))
		node = new_true();
	else if (!strcmp(s, "false"))
		node = new_false();
	else if (is_integer(s))
		node = new_integer(strtoll(s, NULL, 0));
	else if (is_real(s))
		node = new_real(strtod(s, NULL));
	else if (*s == '"')
		node = new_string(s);
	else if (*s == ':')
		node = new_keyword(s);
	else
		node = new_symbol(s);

	if (node->type != ERROR)
		*tokens = (*tokens)->next;

	return node;
}

static struct node *read_form(struct token **tokens);

static struct node *read_list(struct token **tokens)
{
	struct token *token;
	struct node *last = NULL;
	struct node *down = NULL;
	struct node *node = NULL;

	if (!*tokens)
		return NULL;
	if (strcmp((*tokens)->s, "("))
		return new_error("unexpected list initiator");
	*tokens = (*tokens)->next;

	token = *tokens;
	while (token && strcmp(token->s, ")"))
	{
		struct node *next = read_form(&token);

		if (next->type == ERROR)
		{
			free_node(down);
			return next;
		}

		if (last)
			last->next = next;
		else
			down = next;
		last = next;
	}

	if (!token)
	{
		free_node(down);
		return new_error("unterminated list");
	}

	if (strcmp(token->s, ")"))
	{
		free_node(down);
		return new_error("unexpected list terminator");
	}

	*tokens = token->next;

	return new_list(down);
}

static struct node *read_vector(struct token **tokens)
{
	struct token *token;
	struct node *last = NULL;
	struct node *down = NULL;
	struct node *node = NULL;

	if (!*tokens)
		return NULL;
	if (strcmp((*tokens)->s, "["))
		return new_error("unexpected vector initiator");

	*tokens = (*tokens)->next;

	token = *tokens;
	while (token && strcmp(token->s, "]"))
	{
		struct node *next = read_form(&token);

		if (next->type == ERROR)
		{
			free_node(down);
			return next;
		}

		if (last)
			last->next = next;
		else
			down = next;
		last = next;
	}

	if (!token)
	{
		free_node(down);
		return new_error("unterminated vector");
	}

	if (strcmp(token->s, "]"))
	{
		free_node(down);
		return new_error("unexpected vector terminator");
	}

	*tokens = token->next;

	return new_vector(down);
}

static struct node *read_quote(struct token **tokens)
{
	struct node *quoted;

	if (!*tokens)
		return NULL;
	if (strcmp((*tokens)->s, "'"))
		return new_error("unexpected quote initiator");

	*tokens = (*tokens)->next;

	quoted = read_form(tokens);
	if (quoted->type == ERROR)
		return quoted;

	return new_quote(quoted);
}

static struct node *read_quasiquote(struct token **tokens)
{
	struct node *quoted;

	if (!*tokens)
		return NULL;
	if (strcmp((*tokens)->s, "`"))
		return new_error("unexpected quasiquote initiator");

	*tokens = (*tokens)->next;

	quoted = read_form(tokens);
	if (quoted->type == ERROR)
		return quoted;

	return new_quasiquote(quoted);
}

static struct node *read_unquote(struct token **tokens)
{
	struct node *quoted;

	if (!*tokens)
		return NULL;
	if (strcmp((*tokens)->s, "~"))
		return new_error("unexpected unquote initiator");

	*tokens = (*tokens)->next;

	quoted = read_form(tokens);
	if (quoted->type == ERROR)
		return quoted;

	return new_unquote(quoted);
}

static struct node *read_spliceunquote(struct token **tokens)
{
	struct node *quoted;

	if (!*tokens)
		return NULL;
	if (strcmp((*tokens)->s, "~@"))
		return new_error("unexpected splice-unquote initiator");

	*tokens = (*tokens)->next;

	quoted = read_form(tokens);
	if (quoted->type == ERROR)
		return quoted;

	return new_splicequote(quoted);
}

static struct node *read_form(struct token **tokens)
{
	struct node *ast = NULL;
	struct node *node = NULL;

	if (*tokens == NULL)
		return NULL;

	if (!strcmp((*tokens)->s, "("))
		node = read_list(tokens);
	else if (!strcmp((*tokens)->s, "["))
		node = read_vector(tokens);
	else if (!strcmp((*tokens)->s, "'"))
		node = read_quote(tokens);
	else if (!strcmp((*tokens)->s, "`"))
		node = read_quasiquote(tokens);
	else if (!strcmp((*tokens)->s, "~"))
		node = read_unquote(tokens);
	else if (!strcmp((*tokens)->s, "~@"))
		node = read_spliceunquote(tokens);
	else
		node = read_atom(tokens);

	return node;
}

static struct node *read_str(char *code)
{
	struct token *tokens, *orig;
	struct node *node;

	orig = tokens = tokenize(code);
	node = read_form(&tokens);
	free_tokens(orig);

	if (tokens != NULL && node && node->type != ERROR)
	{
		free_node(node);
		return new_error("not all tokens interpreted");
	}


	return node;
}

static char *pr_form(struct node *node, int readable);

static char *pr_list(struct node *node, int readable)
{
	char *prevresult, *result = NULL;
	char *part;
	char *p;

	result = strdup("()");
	if (!result)
		return strdup("Error: cannot format empty list");

	while (node)
	{
		int new = 0;
		int len = 0;
		int used = strlen(result);

		if (node->type == ERROR)
		{
			free(result);
			return pr_form(node, readable);
		}

		part = pr_form(node, readable);
		if (!part)
		{
			free(result);
			return strdup("Error: cannot format list element");
		}

		new = snprintf(NULL, 0, "%s%s", part, node->next ? " " : "");
		if (new < 0)
		{
			free(result);
			free(part);
			return strdup("Error: cannot determine list element length");
		}

		len = used + new + 1;
		result = realloc(prevresult = result, len);
		if (!result)
		{
			free(prevresult);
			free(part);
			return strdup("Error: cannot extend list");
		}

		new = snprintf(result + used - 1, len - used + 1, "%s%s)", part, node->next ? " " : "");
		if (new < 0)
		{
			free(result);
			free(part);
			return strdup("Error: cannot append element to list");
		}

		free(part);
		node = node->next;
	}

	return result;
}

static char *pr_vector(struct node *node, int readable)
{
	char *prevresult, *result = NULL;
	char *part;
	char *p;

	result = strdup("[]");
	if (!result)
		return strdup("Error: cannot format empty list");

	while (node)
	{
		int new = 0;
		int len = 0;
		int used = strlen(result);

		if (node->type == ERROR)
		{
			free(result);
			return pr_form(node, readable);
		}

		part = pr_form(node, readable);
		if (!part)
		{
			free(result);
			return strdup("Error: cannot format list element");
		}

		new = snprintf(NULL, 0, "%s%s", part, node->next ? " " : "");
		if (new < 0)
		{
			free(result);
			free(part);
			return strdup("Error: cannot determine list element length");
		}

		len = used + new + 1;
		result = realloc(prevresult = result, len);
		if (!result)
		{
			free(prevresult);
			free(part);
			return strdup("Error: cannot extend list");
		}

		new = snprintf(result + used - 1, len - used + 1, "%s%s]", part, node->next ? " " : "");
		if (new < 0)
		{
			free(result);
			free(part);
			return strdup("Error: cannot append element to list");
		}

		free(part);
		node = node->next;
	}

	return result;
}

static char *pr_str(struct node *node, int readable)
{
	char *prevresult, *result;
	char c[3] = { '\0', '\0', '\0' };
	char *p;
	int len;

	if (!node)
		return NULL;

	p = node->u.string;

	len = 1 + strlen(p) + 1 + 1;
	result = malloc(len);
	if (!result)
		return strdup("Error: cannot allocate string");
	result[0] = '\0';
	strcat(result, "\"");

	while (*p)
	{
		c[0] = c[1] = '\0';

		if (readable)
		{
			switch (*p)
			{
			case '\a': c[0] = '\\'; c[1] = 'a'; break;
			case '\b': c[0] = '\\'; c[1] = 'b'; break;
			case '\x1b': c[0] = '\\'; c[1] = 'e'; break;
			case '\f': c[0] = '\\'; c[1] = 'f'; break;
			case '\n': c[0] = '\\'; c[1] = 'n'; break;
			case '\r': c[0] = '\\'; c[1] = 'r'; break;
			case '\t': c[0] = '\\'; c[1] = 't'; break;
			case '\v': c[0] = '\\'; c[1] = 'v'; break;
			case '\\': c[0] = '\\'; c[1] = '\\'; break;
			case '"': c[0] = '\\'; c[1] = '"'; break;
			default: c[0] = *p; break;
			}

			if (c[0] == '\\')
			{
				len++;
				result = realloc(prevresult = result, len);
				if (!result)
				{
					free(prevresult);
					return strdup("Error: cannot format string");
				}
			}
		}
		else
			c[0] = *p;

		strcat(result, c);
		p++;
	}

	strcat(result, "\"");

	return result;
}

static char *pr_form(struct node *node, int readable)
{
	char *result;

	if (!node)
		return NULL;

	switch (node->type)
	{
	case ERROR:
		if (asprintf(&result, "Error: %s", node->u.error) < 0)
			result = strdup("Error: cannot format error message");
		break;
	case FALSE: result = strdup("false"); break;
	case TRUE: result = strdup("true"); break;
	case NIL: result = strdup("nil"); break;
	case INTEGER:
		if (asprintf(&result, "%lld", node->u.integer) < 0)
			result = strdup("Error: cannot format integer");
		break;
	case REAL:
		if (asprintf(&result, "%f", node->u.integer) < 0)
			result = strdup("Error: cannot format real");
		break;
	case SYMBOL:
		result = strdup(node->u.symbol);
		break;
	case STRING:
		result = pr_str(node, readable);
		break;
	case LIST:
		result = pr_list(node->down, readable);
		break;
	case VECTOR:
		result = pr_vector(node->down, readable);
		break;
	case KEYWORD:
		if (asprintf(&result, ":%s", node->u.keyword) < 0)
			result = strdup("Error: cannot format keyword");
		break;
	}

	return result;
}


static struct node *READ(char *code)
{
	struct node *ast = read_str(code);
	free(code);
	return ast;
}

static struct node *EVAL(struct node *ast)
{
	return ast;
}

static char *PRINT(struct node *ast)
{
	char *result = pr_form(ast, 1);
	free_node(ast);
	return result;
}

static char *rep(char *code)
{
	return PRINT(EVAL(READ(code)));
}

int main(int argc, char **argv)
{
	char *line, *result;

	do
	{
		line = readline("user> ");
		if (line)
			add_history(line);

		if (line)
		{
			result = rep(line);
			if (result && strlen(result))
				printf("%s\n", result);
		}
		else
			result = NULL;

		free(result);

	} while (line != NULL);

	printf("\n");
}
