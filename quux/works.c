// https://github.com/kanaka/mal/blob/master/process/guide.md#step-2-eval
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdarg.h>

#define PROMPT "user> "

typedef enum nodetype_e nodetype;
typedef struct node_s node;
typedef struct symbol_s symbol;
typedef enum symboltype_e symboltype;
typedef struct environment_s environment;

enum nodetype_e
{
    MAL_EOF,
    MAL_ERROR,
    MAL_NIL,
    MAL_TRUE,
    MAL_FALSE,
    MAL_NUMBER,
    MAL_SYMBOL,
    MAL_KEYWORD,
    MAL_STRING,
    MAL_LIST,
    MAL_VECTOR,
    MAL_HASHMAP,
};

struct node_s
{
    nodetype type;
    node *next;
    union {
        char *message;
        int number;
        char *sym;
        node *list;
        char *string;
        char *keyword;
        struct {
            node *keys;
            node *values;
        } hash;
    } u;
};

enum symboltype_e
{
    SYM_VARIABLE,
    SYM_FUNCTION,
};

struct symbol_s
{
    symboltype type;
    symbol *next;
    char *name;
    union {
        node *value;
        node *(*eval)(environment *env, node *args, node *(*eval_)(environment *, node *));
    } u;
};

struct environment_s
{
    symbol *symbols;
};

static node eof =
{
    MAL_EOF,
    NULL,
};

static node error =
{
    MAL_ERROR,
    NULL,
};

static void freenode(node *n)
{
    node *next = NULL;

    while (n)
    {
        next = n->next;
        if (n->type == MAL_SYMBOL)
            free(n->u.sym);
        else if (n->type == MAL_STRING)
            free(n->u.string);
        else if (n->type == MAL_KEYWORD)
            free(n->u.keyword);
        else if (n->type == MAL_LIST || n->type == MAL_VECTOR)
            freenode(n->u.list);
        else if (n->type == MAL_HASHMAP)
        {
            freenode(n->u.hash.keys);
            freenode(n->u.hash.values);
        }
        else if (n->type == MAL_ERROR && n != &error)
            free(n->u.message);
        if (n != &eof && n != &error)
            free(n);
        n = next;
    }
}

static node *alloceof()
{
    return &eof;
}

static node *allocnode(int type)
{
    node *n = calloc(1, sizeof(node));
    if (!n)
    {
        n = &error;
        n->u.message = "cannot allocate node";
    }
    else
        n->type = type;
    return n;
}

static node *allocerror(char *fmt, ...)
{
    node *n;
    int len;
    va_list args;

    n = allocnode(MAL_ERROR);
    if (n != &error)
    {
        va_start(args, fmt);
        len = vsnprintf(NULL, 0, fmt, args);
        if (len >= 0)
        {
            n->u.message = calloc(len + 1, sizeof(char));
            if (n->u.message)
            len = vsnprintf(n->u.message, len + 1, fmt, args);
        }
        if (len < 0 || n->u.message == NULL)
        {
            n = &error;
            n->u.message = "error formatting error message";
        }
        va_end(args);
    }

    return n;
}

static node *allocnil()
{
    node *n = allocnode(MAL_NIL);
    return n;
}

static node *alloctrue()
{
    node *n = allocnode(MAL_TRUE);
    return n;
}

static node *allocfalse()
{
    node *n = allocnode(MAL_FALSE);
    return n;
}

static node *allocnumber(int i)
{
    node *n = allocnode(MAL_NUMBER);
    if (n->type == MAL_NUMBER)
        n->u.number = i;
    return n;
}

static node *allocsymbol(char *s)
{
    node *n = allocnode(MAL_SYMBOL);
    if (n->type == MAL_SYMBOL)
    {
        n->u.sym = strdup(s);
        if (!n->u.sym)
        {
            freenode(n);
            n = allocerror("cannot allocate symbol");
        }
    }
    return n;
}

static node *allockeyword(char *s, char *e)
{
    node *n = allocnode(MAL_KEYWORD);
    if (n->type == MAL_ERROR);
        return n;

    if (e != NULL)
        n->u.keyword = calloc(e - s + 1, sizeof(char));
    else
        n->u.keyword = strdup(s);
    if (!n->u.keyword)
    {
        freenode(n);
        return allocerror("cannot allocate keyword");
    }

    if (e)
        memcpy(n->u.keyword, s, e - s);

    return n;
}

static node *allocstring(char *s, char *e)
{
    node *n = allocnode(MAL_STRING);
    if (n->type == MAL_ERROR);
        return n;

    if (e != NULL)
        n->u.string = calloc(e - s + 1, sizeof(char));
    else
        n->u.string = strdup(s);
    if (!n->u.string)
    {
        freenode(n);
        return allocerror("cannot allocate string");
    }

    if (e)
    {
        char *dst = n->u.string;
        char *src = s;

        while (src < e)
        {
            *dst = *src++;

            if (*dst == '\\')
            {
                if (src >= e)
                {
                    freenode(n);
                    return allocerror("unterminated escape sequence at end of string");
                }

                switch(*src++)
                {
                case '\\': *dst = '\\'; break;
                case '\n': *dst = '\n'; break;
                case '"': *dst = '"'; break;
                default:
                    freenode(n);
                    return allocerror("unknown escape sequence '%c' in string", *--src);
                }
            }
        }
    }

    return n;
}

static node *alloclist(node *list)
{
    node *n = allocnode(MAL_LIST);
    if (n->type == MAL_LIST)
        n->u.list = list;
    return n;
}

static node *allocvector(node *vector)
{
    node *n = allocnode(MAL_VECTOR);
    if (n->type == MAL_VECTOR)
        n->u.list = vector;
    return n;
}

static node *allochashmap(node *keys, node *values)
{
    node *n = allocnode(MAL_HASHMAP);
    if (n->type == MAL_HASHMAP)
    {
        n->u.hash.keys = keys;
        n->u.hash.values = values;
    }
    return n;
}

static node *copy_atom(node *atom)
{
    node *result = NULL;

    switch (atom->type)
    {
        case MAL_EOF:
            return alloceof();

        case MAL_ERROR:
            return allocerror(atom->u.message);

        case MAL_NIL:
            return allocnil();

        case MAL_TRUE:
            return alloctrue();

        case MAL_FALSE:
            return allocfalse();

        case MAL_NUMBER:
            return allocnumber(atom->u.number);

        case MAL_SYMBOL:
             return allocsymbol(atom->u.sym);

        case MAL_KEYWORD:
             return allockeyword(atom->u.keyword, NULL);

        case MAL_STRING:
            return allocstring(atom->u.string, NULL);
    }

    return result;
}

static node *copy_elements(node *src, node *(*copy_node)(node *))
{
    node *elements = NULL;
    node **dst = &elements;

    while (src)
    {
        node *element = copy_node(src);
        if (element->type == MAL_ERROR)
        {
            freenode(elements);
            return element;
        }

        *dst = element;
        dst = &((*dst)->next);
        src = src->next;
    }

    return elements;
}

static node *copy_list(node *list, node *(*copy_node)(node *))
{
    node *result, *elements;

    elements = copy_elements(list->u.list, copy_node);
    if (elements->type == MAL_ERROR)
        result = elements;
    else
        result = allocvector(elements);

    return result;
}

static node *copy_vector(node *vector, node *(*copy_node)(node *))
{
    node *result, *elements;

    elements = copy_elements(vector->u.list, copy_node);
    if (elements->type == MAL_ERROR)
        result = elements;
    else
        result = allocvector(elements);

    return result;
}

static node *copy_hashmap(node *hashmap, node *(*copy_node)(node *))
{
    node *result, *keys, *values;

    keys = copy_elements(hashmap->u.hash.keys, copy_node);
    if (keys->type == MAL_ERROR)
        result = keys;
    else
    {
        values = copy_elements(hashmap->u.hash.values, copy_node);
        if (values->type == MAL_ERROR)
            result = values;
        else
            result = allochashmap(keys, values);
    }

    return result;
}

static node *copy_node(node *n)
{
    node *result;

    if (n && n->type == MAL_LIST)
        result = copy_list(n, copy_node);
    else if (n && n->type == MAL_VECTOR)
        result = copy_vector(n, copy_node);
    else if (n && n->type == MAL_HASHMAP)
        result = copy_hashmap(n, copy_node);
    else
        result = copy_atom(n);

    return result;
}

static void freesymbol(symbol *sym)
{
    if (sym)
        free(sym->name);
    free(sym);
}

static bool symbol_is_variable(symbol *sym)
{
    return sym && sym->type == SYM_VARIABLE;
}

static bool symbol_is_function(symbol *sym)
{
    return sym && sym->type == SYM_FUNCTION;
}

static symbol *lookup_variable(environment *env, char *name)
{
    symbol *sym;

    if (!env)
        return NULL;

    sym = env->symbols;
    while (sym)
    {
        if (!strcmp(sym->name, name) && sym->type == SYM_VARIABLE)
            return sym;
        sym = sym->next;
    }

    return NULL;
}

static symbol *lookup_function(environment *env, char *name)
{
    symbol *sym;

    if (!env)
        return NULL;

    sym = env->symbols;
    while (sym)
    {
        if (!strcmp(sym->name, name) && sym->type == SYM_FUNCTION)
            return sym;
        sym = sym->next;
    }

    return NULL;
}

static node *remove_variable(environment *env, char *name)
{
    symbol *sym, **prev;
    node *value;

    if (!env)
        return NULL;

    prev = &env->symbols;
    sym = env->symbols;
    while (sym)
    {
        if (!strcmp(sym->name, name) && sym->type == SYM_VARIABLE)
        {
            *prev = sym->next;
            sym->next = NULL;
            value = sym->u.value;
            freesymbol(sym);
            return value;
        }
        prev = &sym->next;
        sym = sym->next;
    }

    return NULL;
}

static node *(*remove_function(environment *env, char *name))(environment *, node *, node *(*)(environment *, node *))
{
    symbol *sym, **prev;
    node *(*eval)(environment *, node *, node *(*eval_)(environment *, node *));

    if (!env)
        return NULL;

    prev = &env->symbols;
    sym = env->symbols;
    while (sym)
    {
        if (!strcmp(sym->name, name) && sym->type == SYM_FUNCTION)
        {
            *prev = sym->next;
            sym->next = NULL;
            eval = sym->u.eval;
            freesymbol(sym);
            return eval;
        }
        prev = &sym->next;
        sym = sym->next;
    }

    return NULL;
}


static symbol *add_symbol(environment *env, char *name, symboltype type)
{
    symbol *sym = calloc(1, sizeof(symbol));
    if (!sym)
        return NULL;

    sym->type = type;
    sym->name = strdup(name);
    sym->next = env->symbols;
    env->symbols = sym;

    return sym;
}

static symbol *add_variable(environment *env, char *name, node *value_)
{
    symbol *sym;
    node *value;

    value = copy_node(value_);
    if (value && value->type != MAL_ERROR)
        return NULL;

    sym = add_symbol(env, name, SYM_VARIABLE);
    if (!sym)
    {
        freenode(value);
        return NULL;
    }

    sym->u.value = value;
    return sym;
}

static symbol *add_function(environment *env, char *name, node *(*eval)(environment *, node *, node *(*)(environment *, node *)))
{
    symbol *sym;
    node *value;

    sym = add_symbol(env, name, SYM_FUNCTION);
    if (!sym)
        return NULL;

    sym->u.eval = eval;
    return sym;
}

static void freeenvironment(environment *env)
{
    int i;
    symbol *sym, *next;

    if (env)
    {
        sym = env->symbols;
        while (sym)
        {
            next = sym->next;
            freesymbol(sym);
            sym = next;
        }
    }

    free(env);
}

static environment *allocenvironment()
{
    environment *env = calloc(1, sizeof(environment));
    if (!env)
        return NULL;

    return env;
}

static bool print_(node *n, bool mayfree, bool readably, int level)
{
    node *k, *v, *orig = n;
    bool eof = false;
    char *s;

    if (n)
        switch (n->type)
        {
            default: fprintf(stderr, "internal error: node type not printable: %d\n", n->type); break;
            case MAL_EOF: eof = true; break;
            case MAL_ERROR: printf("%s", n->u.message); break;
            case MAL_NIL: printf("nil"); break;
            case MAL_TRUE: printf("true"); break;
            case MAL_FALSE: printf("false"); break;
            case MAL_NUMBER: printf("%d", n->u.number); break;
            case MAL_SYMBOL: printf("%s", n->u.sym); break;
            case MAL_KEYWORD: printf(":%s", n->u.keyword); break;
            case MAL_STRING:
                  if (readably)
                      printf("\"");
                  s = n->u.string;
                  while (*s)
                  {
                      if (readably && *s == '\\')
                          printf("\\\\");
                      else if (readably && *s == '\n')
                          printf("\\n");
                      else if (readably && *s == '"')
                          printf("\\\"");
                      else
                          printf("%c", *s);
                      s++;
                  }
                  if (readably)
                      printf("\"");
                  break;
            case MAL_LIST:
                  printf("(");
                  n = n->u.list;
                  while (n)
                  {
                      eof = eof || print_(n, false, readably, level + 1);
                      if (n->next)
                          printf(" ");
                      n = n->next;
                  }
                  printf(")");
                  break;
            case MAL_VECTOR:
                  printf("[");
                  n = n->u.list;
                  while (n)
                  {
                      eof = eof || print_(n, false, readably, level + 1);
                      n = n->next;
                      if (n)
                          printf(" ");
                  }
                  printf("]");
                  break;
            case MAL_HASHMAP:
                  printf("{");
                  k = n->u.hash.keys;
                  v = n->u.hash.values;
                  while (k && v)
                  {
                      eof = eof || print_(k, false, readably, level + 1);
                      printf(" ");
                      eof = eof || print_(v, false, readably, level + 1);

                      k = k->next;
                      v = v->next;
                      if (k)
                          printf(" ");
                  }
                  printf("}");
                  break;
        }

    if (orig)
    {
        if (level == 0)
            printf("\n");
        if (mayfree)
            freenode(orig);
    }

    return eof;
}

static bool print(node *n)
{
    return print_(n, true, true, 0);
}

static int arguments(node *n)
{
    int count = 0;

    while (n)
    {
        n = n->next;
        count++;
    }

    return count;
}

static node *eval_add(environment *env, node *args, node *(*eval_)(environment *, node *))
{
    node *result, *arg = args;
    int sum = 0;

    while (arg)
    {
        if (arg->type != MAL_NUMBER)
        {
            freenode(args);
            return allocerror("argument to + not a number");
        }

        sum += arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    result = allocnumber(sum);
    if (result->type == MAL_ERROR)
        return result;

    return result;
}

static node *eval_mul(environment *env, node *args, node *(*eval_)(environment *, node *))
{
    node *result, *arg = args;
    int mult = 1;

    while (arg)
    {
        if (arg->type != MAL_NUMBER)
        {
            freenode(args);
            return allocerror("argument to * not a number");
        }

        mult *= arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    result = allocnumber(mult);
    if (result->type == MAL_ERROR)
        return result;

    return result;
}

static node *eval_sub(environment *env, node *args, node *(*eval_)(environment *, node *))
{
    node *result, *arg = args;
    int remainder = 0;

    if (arg)
    {
        if (arg->type != MAL_NUMBER)
        {
            freenode(args);
            return allocerror("first argument to - not a number");
        }

        remainder = arg->u.number;
        arg = arg->next;
    }

    while (arg)
    {
        if (arg->type != MAL_NUMBER)
        {
            freenode(args);
            return allocerror("argument to - not a number");
        }

        remainder -= arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    result = allocnumber(remainder);
    if (result->type == MAL_ERROR)
        return result;

    return result;
}

static node *eval_div(environment *env, node *args, node *(*eval_)(environment *, node *))
{
    node *result, *arg = args;
    int quotient = 0;

    if (arg)
    {
        if (arg->type != MAL_NUMBER)
        {
            freenode(args);
            return allocerror("first argument to / not a number");
        }

        quotient = arg->u.number;
        arg = arg->next;
    }

    while (arg)
    {
        if (arg->type != MAL_NUMBER)
        {
            freenode(args);
            return allocerror("division by something other than number");
        }

        if (arg->u.number == 0)
        {
            freenode(args);
            return allocerror("division by 0");
        }

        quotient /= arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    result = allocnumber(quotient);
    if (result->type == MAL_ERROR)
        return result;

    return result;
}

static node *eval_function(environment *env, node *list, node *(*eval_)(environment *, node *))
{
    symbol *sym;
    node *func, *args, *result;

    func = list->u.list;
    args = func ? func->next : NULL;
    func->next = NULL;

    sym = lookup_function(env, func->u.sym);

    if (sym)
        result = sym->u.eval(env, args, eval_);
    else
        result = allocerror("function not found");

    freenode(func);

    return result;
}

static node *eval_variable(environment *env, node *variable)
{
    symbol *sym = lookup_variable(env, variable->u.sym);
    node *result;

    if (sym)
        result = copy_node(sym->u.value);
    else
        result = copy_node(variable);

    return result;
}

static node *eval_list(environment *env, node *list, node *(*eval_)(environment *, node *))
{
    node *result = NULL;
    node *first, *element, *evallist, *next, **dst;

    first = list->u.list;
    if (first && first->type == MAL_SYMBOL && lookup_function(env, first->u.sym))
        return result = eval_function(env, list, eval_);

    evallist = NULL;
    dst = &evallist;

    element = first;
    while (element && !(result && result->type == MAL_ERROR))
    {
        next = element->next;
        element->next = NULL;
        result = *dst = eval_(env, element);
        if ((*dst)->type == MAL_ERROR)
            continue;

        dst = &((*dst)->next);
        element = next;
    }

    if (!(result && result->type == MAL_ERROR))
        result = alloclist(evallist);

    if (result && result->type == MAL_ERROR)
        freenode(evallist);

    return result;
}

static node *eval_vector(environment *env, node *vector, node *(*eval_)(environment *, node *))
{
    node *result = NULL;
    node *element, *evallist, **dst;

    evallist = NULL;
    dst = &evallist;

    element = vector->u.list;
    while (element && !(result && result->type == MAL_ERROR))
    {
        result = *dst = eval_(env, element);
        if ((*dst)->type == MAL_ERROR)
            continue;

        dst = &((*dst)->next);
        element = element->next;
    }

    if (!(result && result->type == MAL_ERROR))
        result = allocvector(evallist);

    if (result && result->type == MAL_ERROR)
        freenode(evallist);

    freenode(vector);

    return result;
}

static node *eval_hashmap(environment *env, node *hashmap, node *(*eval_)(environment *, node * ))
{
    node *result = NULL;
    node *value, *evalvalues, *keys, **dst;

    evalvalues = NULL;
    dst = &evalvalues;

    value = hashmap->u.hash.values;
    while (value && !(result && result->type == MAL_ERROR))
    {
        result = *dst = eval_(env, value);
        if ((*dst)->type == MAL_ERROR)
            continue;

        dst = &((*dst)->next);
        value = value->next;
    }

    if (!(result && result->type == MAL_ERROR))
        result = keys = copy_node(hashmap->u.hash.keys);

    if (!(result && result->type == MAL_ERROR))
        result = allochashmap(keys, evalvalues);

    if (result && result->type == MAL_ERROR)
    {
        freenode(keys);
        freenode(evalvalues);
    }

    freenode(hashmap);

    return result;
}

static node *eval_atom(environment *env, node *atom)
{
    return copy_node(atom);
}

static node *eval_(environment *env, node *n)
{
    node *result;

    if (n && n->type == MAL_LIST)
        result = eval_list(env, n, eval_);
    else if (n && n->type == MAL_VECTOR)
        result = eval_vector(env, n, eval_);
    else if (n && n->type == MAL_HASHMAP)
        result = eval_hashmap(env, n, eval_);
    else if (n && n->type == MAL_SYMBOL)
        result = eval_variable(env, n);
    else
        result = eval_atom(env, n);

    return result;
}

static node *eval(environment *env, node *n)
{
    node *result = eval_(env, n);
    freenode(n);
    return result;
}

static bool isnumber(char *token)
{
    const char *signs = "+-";
    const char *digits = "0123456789";

    if (strchr(signs, *token))
        token++;

    if (*token == '\0')
        return false;

    while (*token != '\0' && strchr(digits, *token))
        token++;

    return *token == '\0';
}

static node *read_atom(char *token)
{
    const char *digits = "0123456789";
    node *n = NULL;

    if (!token)
        return allocerror("no token to read");

    if (isnumber(token))
    {
        bool negative;
        int val = 0;

        if (*token == '+')
        {
            negative = false;
            token++;
        }
        else if (*token == '-')
        {
            negative = true;
            token++;
        }
        else
            negative = false;

        while (*token && strchr(digits, *token))
        {
            val = val * 10 + (*token - '0');
            token++;
        }

        if (negative)
            val = -val;

        n = allocnumber(val);
        if (n->type == MAL_ERROR)
            return n;
    }
    else if (!strcmp(token, "nil"))
        n = allocnil();
    else if (!strcmp(token, "true"))
        n = alloctrue();
    else if (!strcmp(token, "false"))
        n = allocfalse();
    else if (strlen(token))
        n = allocsymbol(token);
    else
        return allocerror("token of unknown type");

    return n;
}

static node *read_form_(char ***tokens)
{
    node *result, *n, *car, *cdr;
    char *quotesymbol = NULL;
    char *expected = NULL;
    bool keyword = false;
    bool metadata = false;
    char *sym;

    if (!*tokens)
        return alloceof();
    if (**tokens == NULL)
        return NULL;

    if (!strcmp(**tokens, "~@"))
        quotesymbol = "splice-unquote";
    else if (!strcmp(**tokens, "'"))
        quotesymbol = "quote";
    else if (!strcmp(**tokens, "`"))
        quotesymbol = "quasiquote";
    else if (!strcmp(**tokens, "~"))
        quotesymbol = "unquote";
    else if (!strcmp(**tokens, "@"))
        quotesymbol = "deref";
    else if (!strcmp(**tokens, "^"))
        metadata = true;
    else if (***tokens == ':')
        keyword = true;
    else if (***tokens == '"')
        expected = "\"";
    else if (!strcmp(**tokens, "{"))
        expected = "}";
    else if (!strcmp(**tokens, "("))
        expected = ")";
    else if (!strcmp(**tokens, "["))
        expected = "]";

    if (quotesymbol)
    {
        (*tokens)++;

        cdr = read_form_(tokens);
        if (cdr->type == MAL_ERROR)
            return cdr;

        sym = strdup(quotesymbol);
        if (!sym)
        {
            freenode(cdr);
            return allocerror("unable to copy symbol");
        }

        car = allocsymbol(sym);
        if (car->type == MAL_ERROR)
        {
            freenode(cdr);
            return car;
        }
        car->next = cdr;

        result = alloclist(car);
        if (result->type == MAL_ERROR)
        {
            freenode(car);
            return result;
        }
    }
    else if (metadata)
    {
        (*tokens)++;

        cdr = read_form_(tokens);
        if (cdr->type == MAL_ERROR)
            return cdr;

        car = read_form_(tokens);
        if (car->type == MAL_ERROR)
        {
            freenode(cdr);
            return car;
        }
        car->next = cdr;
        cdr = car;

        sym = strdup("with-meta");
        if (!sym)
        {
            freenode(cdr);
            return allocerror("unable to copy symbol");
        }

        car = allocsymbol(sym);
        if (car->type == MAL_ERROR)
        {
            freenode(cdr);
            return car;
        }
        car->next = cdr;

        result = alloclist(car);
        if (result->type == MAL_ERROR)
        {
            freenode(car);
            return result;
        }
    }
    else if (keyword)
    {
        char *begin, *end, *keyword;

        begin = **tokens + 1;
        if (strlen(begin) < 1)
            return allocerror("expected ':', got EOF");
        end = begin + strlen(begin);

        (*tokens)++;

        result = allockeyword(begin, end);
        if (result->type == MAL_ERROR)
            return result;
    }
    else if (expected && !strcmp(expected, "\""))
    {
        char *begin, *end;
        char *str, *src, *dst;
        int len;

        begin = **tokens + 1;
        if (strlen(begin) < 1)
            return allocerror("expected '\"', got EOF");

        end = begin + strlen(begin) - 1;
        if (*end != '"')
            return allocerror("expected '\"', got '%c'", *end);

        (*tokens)++;

        result = allocstring(begin, end);
        if (result->type == MAL_ERROR)
            return result;
    }
    else if (expected && !strcmp(expected, "}"))
    {
        node *keys = NULL;
        node *vals = NULL;
        node **dstkey = &keys;
        node **dstval = &vals;
        int countkeys;
        int countvals;
        node *element;
        bool key = true;
        node *n;

        (*tokens)++;

        while (**tokens && strcmp(**tokens, expected))
        {
            element = read_form_(tokens);
            if (!element || element->type == MAL_ERROR)
            {
                freenode(keys);
                freenode(vals);
                return element;
            }

            if (key)
            {
                *dstkey = element;
                dstkey = &element->next;

                if (element->type != MAL_STRING && element->type != MAL_KEYWORD)
                {
                    freenode(keys);
                    freenode(vals);
                    return allocerror("hashmap key not a string/keyword");
                }
            }
            else
            {
                *dstval = element;
                dstval = &element->next;
            }
            key = !key;
        }

        if (!**tokens)
        {
            freenode(keys);
            freenode(vals);
            return allocerror("expected '}', got EOF");
        }
        if (strcmp(**tokens, expected))
        {
            freenode(keys);
            freenode(vals);
            return allocerror("expected '}', got something else");
        }
        (*tokens)++;

        n = keys;
        countkeys = 0;
        while (n->next)
        {
            countkeys++;
            n = n->next;
        }

        n = vals;
        countvals = 0;
        while (n->next)
        {
            countvals++;
            n = n->next;
        }

        if (countkeys != countvals)
        {
            freenode(keys);
            freenode(vals);
            return allocerror("number of keys/vals do not match");
        }

        result = allochashmap(keys, vals);
        if (result->type == MAL_ERROR)
        {
            freenode(keys);
            freenode(vals);
            return result;
        }
    }
    else if (expected && (!strcmp(expected, ")") || !strcmp(expected, "]")))
    {
        node *contents = NULL;
        node **dst = &contents;
        node *element;

        (*tokens)++;

        while (**tokens && strcmp(**tokens, expected))
        {
            element = read_form_(tokens);
            if (!element || element->type == MAL_ERROR)
            {
                freenode(contents);
                return element;
            }

            *dst = element;
            dst = &element->next;
        }

        if (!**tokens)
        {
            freenode(contents);
            switch (*expected)
            {
                case ')': return allocerror("expected ')', got EOF");
                case ']': return allocerror("expected ']', got EOF");
            }
        }
        if (strcmp(**tokens, expected))
        {
            freenode(contents);
            switch (*expected)
            {
                case ')': return allocerror("expected ')', something else");
                case ']': return allocerror("expected ']', something else");
            }
        }
        (*tokens)++;

        switch (*expected)
        {
            case ')': result = alloclist(contents); break;
            case ']': result = allocvector(contents); break;
        }
        if (result->type == MAL_ERROR)
        {
            freenode(contents);
            return result;
        }
    }
    else
    {
        result = read_atom(**tokens);
        (*tokens)++;
    }

    return result;
}

static node *read_form(char **tokens)
{
    return read_form_(&tokens);
}

static char *read_whitecomma(char *line)
{
    const char *whitecomma = " \t\v\r\n,";
    while (*line && strchr(whitecomma, *line))
        line++;

    return line;
}

static char *read_spliceunquote(char *line, char **begin, char **end)
{
    if (strncmp(line, "~@", 2))
        return line;

    *begin = line;
    *end = line + 2;
    return *end;
}

static char *read_special(char *line, char **begin, char **end)
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

static char *read_keyword(char *line, char **begin, char **end)
{
    const char *notallowed = " \t\v\r\n[]{}()'\"`,;";
    char *s = line;

    if (*s != ':')
        return line;
    s++;

    while (*s && !strchr(notallowed, *s))
        s++;

    if (s == line)
        return line;

    *begin = line;
    *end = s;
    return *end;
}

static char *read_string(char *line, char **begin, char **end)
{
    char *s = line;

    if (*s != '"')
        return line;
    s++;

    while (s && !*end)
    {
        s = strchr(s, '"');
        if (!s)
            continue;

        if (s[-1] != '\\')
        {
            *begin = line;
            *end = s + 1;
            return *end;
        }

        s++;
    }

    *begin = line;
    *end = line + strlen(line);
    return *end;
}

static char *read_comment(char *line, char **begin, char **end)
{
    if (*line != ';')
        return line;

    *begin = line;
    *end = line + strlen(line);
    return *end;
}

static char *read_symbol(char *line, char **begin, char **end)
{
    const char *notallowed = " \t\v\r\n[]{}()'\"`,;";
    char *s = line;

    if (!*s)
        return line;

    while (*s && !strchr(notallowed, *s))
        s++;

    if (s == line)
        return line;

    *begin = line;
    *end = s;
    return *end;
}

static void freetokens(char **tokens)
{
    char **token = tokens;

    while (token && *token)
    {
        free(*token);
        token++;
    }

    free(tokens);
}

static bool ateol(char *line)
{
    return *line == '\0';
}

static char **tokenizer(char *line)
{
    char *origline = line;
    char *begin, *end;
    char **tokens = NULL;
    char **newtokens = NULL;
    int count = 1;

    if (!line)
        return NULL;

    tokens = calloc(count, sizeof(char *));
    tokens[0] = NULL;

    while (tokens && line && !ateol(line))
    {
        line = read_whitecomma(line);
        if (ateol(line))
            continue;

        begin = end = NULL;

        line = read_comment(line, &begin, &end);
        if (begin && end) continue;
        if (!begin && !end) line = read_spliceunquote(line, &begin, &end);
        if (!begin && !end) line = read_keyword(line, &begin, &end);
        if (!begin && !end) line = read_special(line, &begin, &end);
        if (!begin && !end) line = read_string(line, &begin, &end);
        if (!begin && !end) line = read_symbol(line, &begin, &end);
        if (!begin && !end)
        {
            freetokens(tokens);
            return NULL;
        }

        newtokens = realloc(tokens, ++count * sizeof(char *));
        if (!newtokens)
        {
            freetokens(tokens);
            return NULL;
        }
        tokens = newtokens;

        tokens[count - 1] = NULL;
        tokens[count - 2] = calloc(end - begin + 1, sizeof (char));
        if (!tokens[count - 2])
        {
            freetokens(tokens);
            return NULL;
        }
        memcpy(tokens[count - 2], begin, end - begin);
    }

    return tokens;
}

static node *parse(char *line)
{
    char **tokens;
    node *n;

    tokens = tokenizer(line);
    free(line);

    n = read_form(tokens);
    freetokens(tokens);

    return n;
}

static node *reed(char *prompt)
{
    char *line = readline(prompt);
    if (line && *line)
        add_history(line);
    return parse(line);
}

static environment *initial_environment()
{
    environment *env = allocenvironment();
    if (!env)
        return NULL;

    add_function(env, "+", eval_add);
    add_function(env, "*", eval_mul);
    add_function(env, "-", eval_sub);
    add_function(env, "/", eval_div);

    return env;
}

static void repl()
{
    environment *env = initial_environment();
    while (!print(eval(env, reed(PROMPT))));
    freeenvironment(env);
}

int main(int argc, char **argv)
{
    repl();

    return 0;
}
