#define _GNU_SOURCE

#include <readline/readline.h>
#include <readline/history.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#define PROMPT "user> "

enum nodetype
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
        HASHMAP,
};

enum symboltype
{
        VARIABLE,
        FUNCTION
};

struct environment;

struct symbol
{
        enum symboltype type;
        char *name;
        struct symbol *next;
        union {
                struct node *value;
                struct node *(*eval)(struct environment *env, struct node *args);
        } u;
};

struct environment
{
        struct environment *outer;
        struct symbol *symbols;
};

struct node
{
        enum nodetype type;
	struct node *next;
	union {
		char *message;
		long long integer;
		double real;
		char *string;
		char *symbol;
		char *keyword;
                struct node *list;
                struct node *vector;
                struct {
                        struct node *keys;
                        struct node *values;
                } hashmap;
	} u;
};

static struct node staticerror =
{
        ERROR,
        NULL,
};

static char *errortokens[] =
{
        "(",
        "error",
        "\"unknown error\"",
        ")",
        NULL
};

static char *typestring(enum nodetype type)
{
        switch (type)
        {
        case ERROR: return "error";
        case FALSE: return "false";
        case TRUE: return "true";
        case NIL: return "nil";
        case INTEGER: return "integer";
        case REAL: return "real";
        case SYMBOL: return "symbol";
        case STRING: return "string";
        case LIST: return "list";
        case VECTOR: return "vector";
        case KEYWORD: return "keyword";
        case HASHMAP: return "hashmap";
        }
}

static bool free_node(struct node *node)
{
        struct node *orig = node;
        struct node *tmp;

        while (node)
        {
                tmp = node->next;

                if (node->type == ERROR && node != &staticerror)
                        free(node->u.message);
                else if (node->type == STRING)
                        free(node->u.string);
                else if (node->type == SYMBOL)
                        free(node->u.symbol);
                else if (node->type == KEYWORD)
                        free(node->u.keyword);
                else if (node->type == LIST)
                        free_node(node->u.list);
                else if (node->type == VECTOR)
                        free_node(node->u.vector);

                if (node != &staticerror)
                        free(node);
                node = tmp;
        }

        return orig != NULL;
}

static struct node *new_staticerror(char *msg)
{
	staticerror.type = ERROR;
        staticerror.next = NULL;
	staticerror.u.message = msg;
	return &staticerror;
}

static struct node *new_node(int type, long long integer, double real, char *string, struct node *keys, struct node *values)
{
	struct node *node;

        if (type == ERROR && !string)
                return new_staticerror("cannot format error message");
        else if (type == STRING && !string)
                return new_staticerror("cannot allocate node string");
        else if (type == KEYWORD && !string)
                return new_staticerror("cannot allocate node keyword");
        else if (type == SYMBOL && !string)
                return new_staticerror("cannot allocate node symbol");

	node = calloc(1, sizeof (struct node));
	if (!node)
		return new_staticerror("error allocating node");

	node->type = type;
        if (type == INTEGER)
                node->u.integer = integer;
        else if (type == REAL)
                node->u.real = real;
        else if (type == STRING)
                node->u.string = string;
        else if (type == KEYWORD)
                node->u.keyword = string;
        else if (type == SYMBOL)
                node->u.symbol = string;
        else if (type == LIST)
                node->u.list = values;
        else if (type == VECTOR)
                node->u.vector = values;
        else if (type == HASHMAP)
        {
                node->u.hashmap.keys = keys;
                node->u.hashmap.values = values;
        }
        else if (type == ERROR)
                node->u.message = string;

	return node;
}

static struct node *new_error(char *fmt, ...)
{
        va_list args;
        char *msg = NULL;

        va_start(args, fmt);
        if (vasprintf(&msg, fmt, args) < 0)
                msg = NULL;
        va_end(args);

        return new_node(ERROR, 0, 0, msg, NULL, NULL);
}

static struct node *new_nil(void)
{
	return new_node(NIL, 0, 0, NULL, NULL, NULL);
}

static struct node *new_true(void)
{
	return new_node(TRUE, 0, 0, NULL, NULL, NULL);
}

static struct node *new_false(void)
{
	return new_node(FALSE, 0, 0, NULL, NULL, NULL);
}

static struct node *new_integer(long long integer)
{
	return new_node(INTEGER, integer, 0, NULL, NULL, NULL);
}

static struct node *new_real(double real)
{
        return new_node(REAL, 0, real, NULL, NULL, NULL);
}

static struct node *new_string(char *string)
{
        return new_node(STRING, 0, 0, string, NULL, NULL);
}

static struct node *new_keyword(char *keyword)
{
        return new_node(KEYWORD, 0, 0, keyword, NULL, NULL);
}

static struct node *new_symbol(char *symbol)
{
        return new_node(SYMBOL, 0, 0, strdup(symbol), NULL, NULL);
}

static struct node *new_list(struct node *elements)
{
        return new_node(LIST, 0, 0, NULL, NULL, elements);
}

static struct node *new_vector(struct node *elements)
{
        return new_node(VECTOR, 0, 0, NULL, NULL, elements);
}

static struct node *new_hashmap(struct node *keys, struct node *values)
{
        return new_node(HASHMAP, 0, 0, NULL, keys, values);
}

static struct node *clone_atom(struct node *node)
{
        if (!node)
                return NULL;

        switch (node->type)
        {
        case ERROR: return new_error(node->u.message);
        case FALSE: return new_false();
        case TRUE: return new_true();
        case NIL: return new_nil();
        case INTEGER: return new_integer(node->u.integer);
        case REAL: return new_real(node->u.real);
        case SYMBOL: return new_symbol(node->u.symbol);
        case STRING: return new_string(node->u.string);
        case KEYWORD: return new_keyword(node->u.keyword);
        default:
                return new_error("cannot clone non-atom %s node", typestring(node->type));
        }
}

static struct node *clone_list(struct node *node, struct node *(*clone_node)(struct node *))
{
        struct node *list = node->u.list;
        struct node **dst = NULL;
        struct node *elems = NULL;

        while (list)
        {
                struct node *elem = clone_node(list);

                if (!elems)
                {
                        elems = elem;
                        dst = &elems->next;
                }
                else
                {
                        *dst = elem;
                        dst = &elem->next;
                }

                list = list->next;
        }

	return new_list(elems);
}

static struct node *clone_vector(struct node *node, struct node *(*clone_node)(struct node *))
{
        struct node *list = node->u.list;
        struct node **dst = NULL;
        struct node *elems = NULL;

        while (list)
        {
                struct node *elem = clone_node(list);

                if (!elems)
                {
                        elems = elem;
                        dst = &elems->next;
                }
                else
                {
                        *dst = elem;
                        dst = &elem->next;
                }

                list = list->next;
        }

	return new_list(elems);
}

static struct node *clone_hashmap(struct node *node, struct node *(*clone_node)(struct node *))
{
        struct node *hashkeys = node->u.hashmap.keys;
        struct node *hashvalues = node->u.hashmap.values;
        struct node **dst = NULL;
        struct node *keys = NULL;
        struct node *values = NULL;

        while (hashkeys)
        {
                struct node *elem = clone_node(hashkeys);

                if (!keys)
                {
                        keys = elem;
                        dst = &keys->next;
                }
                else
                {
                        *dst = elem;
                        dst = &elem->next;
                }

                hashkeys = hashkeys->next;
        }

        while (hashkeys)
        {
                struct node *elem = clone_node(hashkeys);

                if (!keys)
                {
                        keys = elem;
                        dst = &keys->next;
                }
                else
                {
                        *dst = elem;
                        dst = &elem->next;
                }

                hashkeys = hashkeys->next;
        }

	return new_hashmap(keys, values);
}

static struct node *clone_node(struct node *node)
{
        if (node && node->type == LIST)
                return clone_list(node, clone_node);
        else if (node && node->type == VECTOR)
                return clone_vector(node, clone_node);
        else if (node && node->type == HASHMAP)
                return clone_hashmap(node, clone_node);
        else
                return clone_atom(node);
}

static void print_string_readably(char *s)
{
        printf("\"");
        while (*s)
        {
                switch (*s)
                {
                case '\a': printf("\\a"); break;
                case '\b': printf("\\b"); break;
                case '\x1b': printf("\\e"); break;
                case '\f': printf("\\f"); break;
                case '\n': printf("\\n"); break;
                case '\r': printf("\\r"); break;
                case '\t': printf("\\t"); break;
                case '\v': printf("\\v"); break;
                case '\\': printf("\\\\"); break;
                case '"': printf("\\\""); break;
                default: printf("%c", *s); break;
                }
                s++;
        }
        printf("\"");
}

static void print_imp(struct node *node, bool readably)
{
        switch(node->type)
        {
        case ERROR: printf("Error: %s\n", node->u.message); break;
        case NIL: printf("nil"); break;
        case TRUE: printf("true"); break;
        case FALSE: printf("false"); break;
        case INTEGER: printf("%lld", node->u.integer); break;
        case REAL: printf("%f", node->u.real); break;
        case KEYWORD: printf(":%s", node->u.keyword); break;
        case SYMBOL: printf("%s", node->u.symbol); break;
        case STRING:
                if (readably)
                        print_string_readably(node->u.string);
                else
                        printf("%s", node->u.string);
                break;
        case LIST:
                {
                        struct node *list = node->u.list;

                        printf("(");
                        while (list)
                        {
                                print_imp(list, readably);
                                list = list->next;
                                if (list)
                                        printf(" ");
                        }
                        printf(")");
                }
                break;
        case VECTOR:
                {
                        struct node *vector = node->u.vector;

                        printf("[");
                        while (vector)
                        {
                                print_imp(vector, readably);
                                vector = vector->next;
                                if (vector)
                                        printf(" ");
                        }
                        printf("]");
                }
                break;
        case HASHMAP:
                {
                        struct node *key = node->u.hashmap.keys;
                        struct node *val = node->u.hashmap.values;

                        printf("{");
                        while (key && val)
                        {
                                print_imp(key, readably);
                                printf(" ");
                                print_imp(val, readably);
                                key = key->next;
                                val = val->next;
                                if (key)
                                        printf(" ");
                        }
                        printf("}");
                }
                break;
        }
}

static bool print(struct node *node, bool readably)
{
        if (node)
        {
                print_imp(node, readably);
                free_node(node);
        }
        printf("\n");
        return node != NULL;
}

static void add_function(struct environment *env, char *name, struct node *(*eval)(struct environment *env, struct node *args))
{
        struct symbol *function;

        function = calloc(1, sizeof (struct symbol));
        if (!function)
                return;

        function->type = FUNCTION;
        function->name = name;
        function->u.eval = eval;

        function->next = env->symbols;
        env->symbols = function;
}

static void free_environment(struct environment *env)
{
        struct symbol *symbol, *next;

        if (!env)
                return;

        symbol = env->symbols;
        while (symbol)
        {
                next = symbol->next;
                free(symbol);
                symbol = next;
        }
}
static struct node *eval_add(struct environment *env, struct node *args)
{
        union {
                long long integer;
                double real;
        } sum = { 0 };
        bool isreal = false;

        while (args)
        {
                if (args->type != INTEGER && args->type != REAL)
                        return new_error("expected integer or real argument to +, got %s", typestring(args->type));

                if (!isreal && args->type == REAL)
                {
                        sum.real = sum.integer;
                        isreal = true;
                }

                if (isreal)
                        sum.real += args->type == INTEGER ? args->u.integer : args->u.real;
                else
                        sum.integer += args->type == INTEGER ? args->u.integer : args->u.real;

                args = args->next;
        }

        free_node(args);

        return isreal ? new_real(sum.real) : new_integer(sum.integer);
}

static struct environment *init_environment()
{
        struct environment *env;

        return calloc(1, sizeof (struct environment));
        if (!env)
                return NULL;

        add_function(env, "+", eval_add);

        return env;
}

static struct symbol *lookup_variable(struct environment *env, char *name)
{
        while (env)
        {
                struct symbol *candidate = env->symbols;

                while (candidate)
                {
                        if (candidate->type == VARIABLE && !strcmp(candidate->name, name))
                                return candidate;

                        candidate = candidate->next;
                }

                env = env->outer;
        }

        return NULL;
}

static struct node *eval_list(struct environment *env, struct node *ast, struct node *(*eval)(struct environment *env, struct node *ast))
{
        struct node *list = ast->u.list;
        struct node **dst = NULL;
        struct node *elems = NULL;

        while (list)
        {
                struct node *elem = eval(env, list);

                if (!elems)
                {
                        elems = elem;
                        dst = &elems->next;
                }
                else
                {
                        *dst = elem;
                        dst = &elem->next;
                }

                list = list->next;
        }

	return new_list(elems);

}

static struct node *eval_symbol(struct environment *env, struct node *symbol)
{
        struct symbol *var = lookup_variable(env, symbol->u.symbol);
        if (!var)
                return new_error("unbound variable '%s'", symbol->u.symbol);
        return clone_node(var->u.value);
}

static struct node *eval_ast(struct environment *env, struct node *ast)
{
        if (ast->type == SYMBOL)
                return eval_symbol(env, ast);
        else if (ast->type == LIST)
                return eval_list(env, ast);
        else
                return ast;
}

static struct node *eval(struct environment *env, struct node *ast)
{
        if (ast->type == LIST && !ast->u.list)
                return ast;
        else if (ast->type == LIST)
                return eval_function(env, eval_list(env, ast));
        else
                return eval_ast(env, ast);
}

static int is_integer(char *token, long long *integer)
{
	char *end = NULL;

	errno = 0;
	*integer = strtoll(token, &end, 0);
	if (errno == ERANGE)
		return 0;

	if (end && *end != '\0')
		return 0;

	return 1;
}

static int is_real(char *token, double *real)
{
	char *end = NULL;

	errno = 0;
	*real = strtod(token, &end);
	if (errno == ERANGE)
		return 0;

	if (end && *end != '\0')
		return 0;

	return 1;
}

static struct node *read_keyword(char ***tokens)
{
        char *s, *prevs;

        if (!*tokens)
                return new_error("no keyword token to read");

        s = strdup(**tokens);

        if (!*s)
		return free(s), new_error("expected keyword to start with ':'");
	if (*s != ':')
		return free(s), new_error("expected keyword to start with ':', got '%c'", *s);

        memmove(&s[0], &s[1], strlen(&s[1]) + 1);

        s = realloc(prevs = s, strlen(s) + 1);
        if (!s)
                return free(prevs), new_error("unable to shorten keyword");

        return new_keyword(s);
}

static struct node *read_string(char ***tokens)
{
        char *p, *s, *prevs;

        if (!*tokens)
                return new_error("no string token to read");

        p = s = strdup(**tokens);

        if (!*p)
		return free(s), new_error("expected string to start with '\"'");
	if (*p != '"')
		return free(s), new_error("expected string to start with '\"', got '%c'", *p);

        memmove(&p[0], &p[1], strlen(&p[1]) + 1);

        while (*p && *p != '"')
        {
                if (*p == '\\')
                {
                        char esc;

                        if (strlen(p) <= 1)
                                return free(s), new_error("unterminated escape sequence");

                        esc = p[1];

                        switch (esc)
                        {
                        case 'a': *p = '\a'; break;
                        case 'b': *p = '\b'; break;
                        case 'e': *p = '\x1b'; break;
                        case 'f': *p = '\f'; break;
                        case 'n': *p = '\n'; break;
                        case 'r': *p = '\r'; break;
                        case 't': *p = '\t'; break;
                        case 'v': *p = '\v'; break;
                        case '\\': *p = '\\'; break;
                        case '"': *p = '"'; break;
                        default:
                                return free(s), new_error("invalid escape sequence, got '%c'", esc);
                        }

                        memmove(&p[1], &p[2], strlen(&p[2]) + 1);
                }

                p++;
        }

        if (!*p)
		return free(s), new_error("unterminated string");
	if (*p != '"')
		return new_error("unterminated string, expected '\"' got '%c'", *p);

        *p = '\0';

        s = realloc(prevs = s, strlen(s) + 1);
        if (!s)
                return free(prevs), new_error("unable to shorten string");

        return new_string(s);
}

static struct node *read_atom(char ***tokens)
{
        struct node *atom;
        long long integer;
        double real;

        if (!tokens || !*tokens || !**tokens)
                return new_error("no atom token to read");

        if (***tokens == '"')
                atom = read_string(tokens);
        else if (***tokens == ':')
                atom = read_keyword(tokens);
        else if (!strcmp(**tokens, "nil"))
                atom = new_nil();
        else if (!strcmp(**tokens, "true"))
                atom = new_true();
        else if (!strcmp(**tokens, "false"))
                atom = new_false();
	else if (is_integer(**tokens, &integer))
		atom = new_integer(integer);
	else if (is_real(**tokens, &real))
		atom = new_real(real);
	else
		atom = new_symbol(**tokens);

        (*tokens)++;

        return atom;
}

static struct node *read_hashmap(char ***tokens, struct node *(read_form)(char ***))
{
	struct token *token;
	struct node *lastkey = NULL;
	struct node *lastval = NULL;
	struct node *keys = NULL;
	struct node *vals = NULL;
	struct node *node = NULL;

        if (!*tokens)
                return new_error("no hashmap token to read");

	if (strcmp(**tokens, "{"))
		return new_error("expected '{', got '%s'", **tokens);

        (*tokens)++;

	while (**tokens && strcmp(**tokens, "}"))
	{
                struct node *key, *val;

		key = read_form(tokens);
                if (key->type == ERROR)
                {
                        free_node(keys);
                        free_node(vals);
			return key;
                }
                else if (key->type != STRING && key->type != KEYWORD)
                {
                        int type = key->type;
                        free_node(keys);
                        free_node(vals);
                        free_node(key);
                        return new_error("hashmap key must be string or keyword, got %s", typestring(type));
                }

                if (!**tokens)
                {
                        free_node(keys);
                        free_node(vals);
                        return new_error("last key in hashmap lacks value");
                }

		val = read_form(tokens);
		if (val->type == ERROR)
		{
                        free_node(keys);
                        free_node(vals);
			free_node(key);
			return val;
		}

		if (lastkey)
			lastkey->next = key;
		else
			keys = key;
		lastkey = key;
		if (lastval)
			lastval->next = val;
		else
			vals = val;
		lastval = val;

	}

	if (!**tokens)
	{
                free_node(keys);
                free_node(vals);
		return new_error("unterminated hashmap");
	}

	if (strcmp(**tokens, "}"))
	{
                free_node(keys);
                free_node(vals);
		return new_error("expected '}', got '%s'", **tokens);
	}

        (*tokens)++;

	return new_hashmap(keys, vals);
}

static struct node *read_vector(char ***tokens, struct node *(read_form)(char ***))
{
	struct token *token;
	struct node *last = NULL;
	struct node *elems = NULL;
	struct node *node = NULL;

        if (!*tokens)
                return new_error("no vector token to read");

	if (strcmp(**tokens, "["))
		return new_error("expected '[', got '%s'", **tokens);

        (*tokens)++;

	while (**tokens && strcmp(**tokens, "]"))
	{
		struct node *next = read_form(tokens);
		if (next->type == ERROR)
		{
			free_node(elems);
			return next;
		}

		if (last)
			last->next = next;
		else
			elems = next;
		last = next;
	}

	if (!**tokens)
	{
		free_node(elems);
		return new_error("unterminated vector");
	}

	if (strcmp(**tokens, "]"))
	{
		free_node(elems);
		return new_error("expected ']', got '%s'", **tokens);
	}

        (*tokens)++;

	return new_vector(elems);
}

static struct node *read_list(char ***tokens, struct node *(read_form)(char ***))
{
	struct token *token;
	struct node *last = NULL;
	struct node *elems = NULL;
	struct node *node = NULL;

        if (!*tokens)
                return new_error("no list token to read");

	if (strcmp(**tokens, "("))
		return new_error("expected '(', got '%s'", **tokens);

        (*tokens)++;

	while (**tokens && strcmp(**tokens, ")"))
	{
		struct node *next = read_form(tokens);
		if (next->type == ERROR)
		{
			free_node(elems);
			return next;
		}

		if (last)
			last->next = next;
		else
			elems = next;
		last = next;
	}

	if (!**tokens)
	{
		free_node(elems);
		return new_error("unterminated list");
	}

	if (strcmp(**tokens, ")"))
	{
		free_node(elems);
		return new_error("expected ')', got '%s'", **tokens);
	}

        (*tokens)++;

	return new_list(elems);
}

static struct node *prepend_node(struct node *car, struct node *cdr)
{
        car->next = cdr;
        return car;
}

static struct node *read_quote(char ***tokens, char *quote, struct node *(read_form)(char ***))
{
	struct node *quoted, *symbol;

        if (!*tokens)
                return new_error("no quoted token to read");

        (*tokens)++;

        quoted = read_form(tokens);
        if (quoted->type == ERROR)
                return quoted;

        symbol = new_symbol(quote);
        if (symbol->type == ERROR)
        {
                free_node(quoted);
                return symbol;
        }

        return new_list(prepend_node(symbol, quoted));
}

static struct node *read_metadata(char ***tokens, struct node *(read_form)(char ***))
{
	struct node *func, *meta, *symbol;

        if (!*tokens)
                return new_error("no meta token to read");

        (*tokens)++;

        func = read_form(tokens);
        if (func->type == ERROR)
                return func;

        meta = read_form(tokens);
        if (meta->type == ERROR)
        {
                free_node(func);
                return meta;
        }

        symbol = new_symbol("with-meta");
        if (symbol->type == ERROR)
        {
                free_node(func);
                free_node(meta);
                return symbol;
        }

        return new_list(prepend_node(symbol, prepend_node(meta, func)));
}

static struct node *read_form(char ***tokens)
{
	struct node *ast;

        if (!tokens || !*tokens)
                return NULL;

	else if (!strcmp(**tokens, "("))
		ast = read_list(tokens, read_form);
	else if (!strcmp(**tokens, "["))
		ast = read_vector(tokens, read_form);
	else if (!strcmp(**tokens, "{"))
		ast = read_hashmap(tokens, read_form);
        else if (!strcmp(**tokens, "~@"))
                ast = read_quote(tokens, "splice-unquote", read_form);
        else if (!strcmp(**tokens, "'"))
                ast = read_quote(tokens, "quote", read_form);
        else if (!strcmp(**tokens, "`"))
                ast = read_quote(tokens, "quasiquote", read_form);
        else if (!strcmp(**tokens, "~"))
                ast = read_quote(tokens, "unquote", read_form);
        else if (!strcmp(**tokens, "@"))
                ast = read_quote(tokens, "deref", read_form);
        else if (!strcmp(**tokens, "^"))
                ast = read_metadata(tokens, read_form);
	else
		ast = read_atom(tokens);

	return ast;
}

static void free_tokens(char **tokens)
{
        char **token = tokens;

        if (tokens == errortokens)
                return;

        while (token && *token)
        {
                free(*token);
                token++;
        }

        free(tokens);
}

#define lexerror(tokens, msg)  lexerror_imp((tokens), "\"" msg "\"")

static char **lexerror_imp(char **tokens, char *msg)
{
        free_tokens(tokens);

        errortokens[2] = msg;
        return &errortokens[0];
}

static inline bool ateol(char *line)
{
        return *line == '\0';
}

static char *lex_whitecomma(char *line)
{
        const char *whitecomma = "\t\n\v\f\r ,";
        while (!ateol(line) && strchr(whitecomma, *line))
                line++;
        return line;
}

static char *lex_comment(char *line, char **begin, char **end)
{
        if (*line != ';')
                return line;

        *begin = line;
        *end = line + strlen(line);
        return *end;
}

static char *lex_spliceunquote(char *line, char **begin, char **end)
{
        if (strncmp(line, "~@", 2))
                return line;

        *begin = line;
        *end = line + 2;
        return *end;
}

static char *lex_special(char *line, char **begin, char **end)
{
        const char *special = "[]{}()'`~^@";

        if (*line == '\0')
                return line;
        if (!strchr(special, *line))
                return line;

        *begin = line;
        *end = line + 1;
        return *end;
}

static char *lex_string(char *line, char **begin, char **end)
{
        char *s = line;

        if (*s != '"')
                return line;
        s++;

        while (*s && *s != '"')
        {
                if (!strncmp(s, "\\\"", 2))
                        s += 2;
                else
                        s++;
        }

        if (*s == '"')
                s++;

        *begin = line;
        *end = s;
        return *end;
}

static char *lex_symbol(char *line, char **begin, char **end)
{
        const char *notallowed = "\t\n\v\f\r []{}()'\"`,;";
        char *s = line;

        while (*s && !strchr(notallowed, *s))
                s++;

        if (s == line)
                return line;

        *begin = line;
        *end = s;
        return *end;
}

static char *lex_token(char **tokens, int count, char *line, char **begin, char **end)
{
        char *prevline = line;

        *begin = *end = NULL;

        if (prevline == line)
                line = lex_comment(line, begin, end);
        if (prevline == line)
                line = lex_spliceunquote(line, begin, end);
        if (prevline == line)
                line = lex_special(line, begin, end);
        if (prevline == line)
                line = lex_string(line, begin, end);
        if (prevline == line)
                line = lex_symbol(line, begin, end);

        if (prevline == line)
                return NULL;

        return line;
}

static char **tokenize(char *line)
{
        char **tokens;
        int count;

        count = 1;
        tokens = calloc(count, sizeof (char *));
        if (!tokens)
                return lexerror(tokens, "cannot allocate empty list of tokens");

        while (tokens && !ateol(line))
        {
                char *begin, *end;
                char **prevtokens;

                line = lex_whitecomma(line);
                if (ateol(line))
                        continue;

                line = lex_token(tokens, count, line, &begin, &end);
                if (!line)
                        return lexerror(tokens, "cannot lex token");

                tokens = realloc(prevtokens = tokens, ++count * sizeof (char *));
                if (!tokens)
                        return lexerror(prevtokens, "cannot extend list of tokens");

                tokens[count - 1] = NULL;
                tokens[count - 2] = strndup(begin, end - begin);
                if (!tokens[count - 2])
                        return lexerror(tokens, "cannot append token to list of tokens");
        }

	return tokens;
}

static struct node *parse(char *line)
{
        char **tokens, **alltokens;
	struct node *ast;

        if (!line)
                return NULL;

        alltokens = tokens = tokenize(line);
        free(line);

        ast = read_form(&tokens);
        free_tokens(alltokens);

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
        struct environment *env = init_environment();
        while (print(eval(env, parse(reed(PROMPT))), true));
        free_environment(env);
}

int main(int argc, char **argv)
{
        repl();

        return 0;
}
