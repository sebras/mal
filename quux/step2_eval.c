// https://github.com/kanaka/mal/blob/master/process/guide.md#step-2-eval
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <readline/readline.h>
#include <readline/history.h>

#define PROMPT "user> "

typedef struct environment_s environment;
typedef struct symbol_s symbol;
typedef enum nodetype_e nodetype;
typedef struct node_s node;

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

struct symbol_s
{
    symbol *next;
    char *name;
    node *value;
    node *(*eval)(environment *env, node *func, node *args);
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

static void freeenvironment(environment *env)
{
    int i;
    symbol *sym, *tmp;

    if (env)
    {
        sym = env->symbols;
        while (sym)
        {
            tmp = sym->next;
            free(sym->name);
            free(sym);
            sym = tmp;
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

static symbol *add_symbol(environment *env, char *name)
{
    symbol *sym;

    sym = calloc(1, sizeof(symbol));
    if (!sym)
        return NULL;

    sym->name = strdup(name);
    sym->next = env->symbols;
    env->symbols = sym;

    return sym;
}

static symbol *add_function(environment *env, char *name, node *(*eval)(environment *, node *, node *))
{
    symbol *sym;

    sym = calloc(1, sizeof(symbol));
    if (!sym)
        return NULL;

    sym->name = strdup(name);
    sym->eval = eval;
    sym->next = env->symbols;
    env->symbols = sym;

    return sym;
}

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
        if (n != &eof && n != &error)
            free(n);
        n = next;
    }
}

static node *alloceof()
{
    return &eof;
}

static node *allocerror(char *msg)
{
    error.u.message = msg;
    return &error;
}

static node *allocnode(int type)
{
    node *n = calloc(1, sizeof(node));
    if (!n)
        return allocerror("cannot allocate node");
    n->type = type;
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
        n->u.sym = s;
    return n;
}

static node *allocstring(char *s)
{
    node *n = allocnode(MAL_STRING);
    if (n->type == MAL_STRING)
        n->u.string = s;
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

static node *allockeyword(char *keyword)
{
    node *n = allocnode(MAL_KEYWORD);
    if (n->type == MAL_KEYWORD)
        n->u.string = keyword;
    return n;
}


static node *copynode(node *n)
{
    node *m;

    if (!n)
        return NULL;

    switch (n->type)
    {
        case MAL_EOF:
            return alloceof();

        case MAL_ERROR:
            return allocerror(n->u.message);

        case MAL_NIL:
            return allocnil();

        case MAL_TRUE:
            return alloctrue();

        case MAL_FALSE:
            return allocfalse();

        case MAL_NUMBER:
            return allocnumber(n->u.number);

        case MAL_SYMBOL:
            {
                char *sym = strdup(n->u.sym);
                if (!sym)
                    return allocerror("unable to copy symbol");

                m = allocsymbol(sym);
                if (m->type == MAL_ERROR)
                    free(sym);
                return m;
            }

        case MAL_KEYWORD:
            {
                char *keyword = strdup(n->u.keyword);
                if (!keyword)
                    return allocerror("unable to copy keyword");

                m = allockeyword(keyword);
                if (m->type == MAL_ERROR)
                    free(keyword);
                return m;
            }

        case MAL_STRING:
            {
                char *string = strdup(n->u.string);
                if (!string)
                    return allocerror("unable to copy string");

                m = allocstring(string);
                if (m->type == MAL_ERROR)
                    free(string);
                return m;
            }

        case MAL_LIST:
            {
                node *list, *element, **dst;

                list = NULL;
                dst = &list;

                element = n->u.list;
                while (element)
                {
                    *dst = copynode(element);
                    if ((*dst)->type == MAL_ERROR)
                    {
                        freenode(list);
                        return *dst;
                    }

                    dst = &((*dst)->next);
                    element = element->next;
                }

                m = alloclist(list);
                if (m->type == MAL_ERROR)
                    freenode(list);
                return m;
            }

        case MAL_VECTOR:
            {
                node *vector, *element, **dst;

                vector = NULL;
                dst = &vector;

                element = n->u.list;
                while (element)
                {
                    *dst = copynode(element);
                    if ((*dst)->type == MAL_ERROR)
                    {
                        freenode(vector);
                        return *dst;
                    }

                    dst = &((*dst)->next);
                    element = element->next;
                }

                m = allocvector(vector);
                if (m->type == MAL_ERROR)
                    freenode(vector);
                return m;
            }

        case MAL_HASHMAP:
            {
                node *keys, *values, *element, **dst;

                keys = NULL;
                dst = &keys;

                element = n->u.hash.keys;
                while (element)
                {
                    *dst = copynode(element);
                    if ((*dst)->type == MAL_ERROR)
                    {
                        freenode(keys);
                        return *dst;
                    }

                    dst = &((*dst)->next);
                    element = element->next;
                }

                values = NULL;
                dst = &values;

                element = n->u.hash.values;
                while (element)
                {
                    *dst = copynode(element);
                    if ((*dst)->type == MAL_ERROR)
                    {
                        freenode(keys);
                        freenode(values);
                        return *dst;
                    }

                    dst = &((*dst)->next);
                    element = element->next;
                }

                m = allochashmap(keys, values);
                if (m->type == MAL_ERROR)
                {
                    freenode(keys);
                    freenode(values);
                }
                return m;
            }
    }

    return m;
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

static node *eval_add(environment *env, node *func, node *args)
{
    node *result, *arg = args;
    int sum = 0;

    freenode(func);

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

static node *eval_mul(environment *env, node *func, node *args)
{
    node *result, *arg = args;
    int mult = 1;

    freenode(func);

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

static node *eval_sub(environment *env, node *func, node *args)
{
    node *result, *arg = args;
    int remainder = 0;

    freenode(func);

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

static node *eval_div(environment *env, node *func, node *args)
{
    node *result, *arg = args;
    int quotient = 0;

    freenode(func);

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

static node *eval_function(environment *env, node *func, node *args)
{
    symbol *sym = env->symbols;

    while (sym)
    {
        if (!strcmp(sym->name, func->u.sym))
        {
            if (sym->value)
                return allocerror("function is a symbol");

            return sym->eval(env, func, args);
        }
        sym = sym->next;
    }

    freenode(func);
    freenode(args);

    return allocerror("function not found");
}

static node *eval_symbol(environment *env, node *lookup, bool erroronmissing)
{
    symbol *sym;

    if (env)
    {
        sym = env->symbols;
        while (sym)
        {
            if (!strcmp(lookup->u.sym, sym->name))
            {
                if (sym->value)
                    return copynode(sym->value);
                else
                    return copynode(lookup);
            }
            sym = sym->next;
        }
    }

    if (erroronmissing)
        return allocerror("missing symbol");

    return copynode(lookup);
}

static node *eval_(environment *env, node *n, bool mayfree)
{
    node *result, *element, *list, *keys, **dst;

    if (n == NULL)
        result = NULL;
    else if (n->type == MAL_SYMBOL)
        result = eval_symbol(env, n, mayfree);
    else if (n->type == MAL_LIST)
    {
        list = NULL;
        dst = &list;

        element = n->u.list;
        while (element)
        {
            *dst = eval_(env, element, false);
            if ((*dst)->type == MAL_ERROR)
            {
                freenode(list);
                freenode(n);
                return *dst;
            }

            dst = &((*dst)->next);
            element = element->next;
        }

        if (list && list->type == MAL_SYMBOL)
        {
            node *args = list->next;
            list->next = NULL;
            result = eval_function(env, list, args);
        }
        else
        {
            result = alloclist(list);
            if (result->type == MAL_ERROR)
            {
                freenode(list);
                freenode(n);
                return result;
            }
        }
    }
    else if (n->type == MAL_VECTOR)
    {
        list = NULL;
        dst = &list;

        element = n->u.list;
        while (element)
        {
            *dst = eval_(env, element, false);
            if ((*dst)->type == MAL_ERROR)
            {
                freenode(list);
                freenode(n);
                return *dst;
            }

            dst = &((*dst)->next);
            element = element->next;
        }

        result = allocvector(list);
        if (result->type == MAL_ERROR)
        {
            freenode(list);
            freenode(n);
            return result;
        }
    }
    else if (n->type == MAL_HASHMAP)
    {
        list = NULL;
        dst = &list;

        element = n->u.hash.values;
        while (element)
        {
            *dst = eval_(env, element, false);
            if ((*dst)->type == MAL_ERROR)
            {
                freenode(list);
                freenode(n);
                return *dst;
            }

            dst = &((*dst)->next);
            element = element->next;
        }

        keys = copynode(n->u.hash.keys);
        if (!keys)
        {
            freenode(list);
            freenode(n);
            return keys;
        }

        result = allochashmap(keys, list);
        if (result->type == MAL_ERROR)
        {
            freenode(keys);
            freenode(list);
            freenode(n);
            return result;
        }
    }
    else
        result = copynode(n);

    if (mayfree)
        freenode(n);

    return result;
}

static node *eval(environment *env, node *n)
{
    return eval_(env, n, true);
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
    {
        n = allocnil();
        if (n->type == MAL_ERROR)
            return n;
    }
    else if (!strcmp(token, "true"))
    {
        n = alloctrue();
        if (n->type == MAL_ERROR)
            return n;
    }
    else if (!strcmp(token, "false"))
    {
        n = allocfalse();
        if (n->type == MAL_ERROR)
            return n;
    }
    else if (strlen(token))
    {
        char *sym = strdup(token);
        if (!sym)
            return allocerror("unable to copy symbol");

        n = allocsymbol(sym);
        if (n->type == MAL_ERROR)
        {
            free(sym);
            return n;
        }
    }
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
            return allocerror("expected '\"', got EOF");
        end = begin + strlen(begin);

        keyword = calloc(end - begin + 1, sizeof(char));
        if (!keyword)
            return allocerror("cannot allocate keyword");
        memcpy(keyword, begin, end - begin);

        result = allockeyword(keyword);
        if (result->type == MAL_ERROR)
        {
            free(keyword);
            return result;
        }

        (*tokens)++;
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
            return allocerror("expected '\"', got something else");

        (*tokens)++;

        str = calloc(end - begin + 1, sizeof(char));
        if (!str)
            return allocerror("unable to allocate string");

        dst = str;
        src = begin;
        while (src < end)
        {
            if (*src == '\\')
            {
                src++;

                if (src >= end)
                {
                    free(str);
                    return allocerror("unterminated escape sequence at end of string");
                }

                switch (*src++)
                {
                case '\\': *dst++ = '\\'; break;
                case 'n': *dst++ = '\n'; break;
                case '"': *dst++ = '"'; break;
                default:
                    free(str);
                    return allocerror("unknown escape sequence in string");
                }
            }
            else
                *dst++ = *src++;
        }

        result = allocstring(str);
        if (result->type == MAL_ERROR)
        {
            free(str);
            return result;
        }
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
        if (!begin && !end) line = read_symbol(line, &begin, &end);
        if (!begin && !end) line = read_string(line, &begin, &end);
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
