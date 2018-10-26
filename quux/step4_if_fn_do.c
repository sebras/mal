// https://github.com/kanaka/mal/blob/master/process/guide.md#step-3-environments
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
        struct {
            int nonevalargs;
            node *(*eval)(environment *env, node *args, node *(eval_)(environment *, node *));
        } func;
    } u;
};

struct environment_s
{
    environment *outer;
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

static node *allocerror(node *result, char *fmt, ...)
{
    node *n;
    int len;
    va_list args;

    freenode(result);

    n = allocnode(MAL_ERROR);
    if (n != &error)
    {
        va_start(args, fmt);
        len = vsnprintf(NULL, 0, fmt, args);
        va_end(args);
        if (len >= 0)
        {
            n->u.message = calloc(len + 1, sizeof(char));
            if (n->u.message)
            {
                va_start(args, fmt);
                len = vsnprintf(n->u.message, len + 1, fmt, args);
                va_end(args);
            }
        }
        if (len < 0 || n->u.message == NULL)
        {
            n = &error;
            n->u.message = "error formatting error message";
        }
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
            return allocerror(n, "cannot allocate symbol");
    }
    return n;
}

static node *allockeyword(char *s, char *e)
{
    node *n = allocnode(MAL_KEYWORD);
    if (n->type == MAL_ERROR)
        return n;

    if (e != NULL)
        n->u.keyword = calloc(e - s + 1, sizeof(char));
    else
        n->u.keyword = strdup(s);
    if (!n->u.keyword)
        return allocerror(n, "cannot allocate keyword");

    if (e)
        memcpy(n->u.keyword, s, e - s);

    return n;
}

static node *allocstring(char *s, char *e)
{
    node *n = allocnode(MAL_STRING);
    if (n->type == MAL_ERROR)
        return n;

    if (e != NULL)
        n->u.string = calloc(e - s + 1, sizeof(char));
    else
        n->u.string = strdup(s);
    if (!n->u.string)
        return allocerror(n, "cannot allocate string");

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
                    return allocerror(n, "unterminated escape sequence at end of string");

                switch(*src++)
                {
                case '\\': *dst = '\\'; break;
                case '\n': *dst = '\n'; break;
                case '"': *dst = '"'; break;
                default:
                    return allocerror(n, "unknown escape sequence '%c' in string", *--src);
                }
            }
        }
    }

    return n;
}

static node *alloclist()
{
    node *n = allocnode(MAL_LIST);
    return n;
}

static node *allocvector()
{
    node *n = allocnode(MAL_VECTOR);
    return n;
}

static node *allochashmap()
{
    node *n = allocnode(MAL_HASHMAP);
    return n;
}

static node *copy_atom(node *atom)
{
    node *result = NULL;

    if (!atom)
        return NULL;

    switch (atom->type)
    {
        case MAL_EOF:
            return alloceof();

        case MAL_ERROR:
            return allocerror(NULL, atom->u.message);

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

        default:
            return allocerror(NULL, "not an atom");
    }

    return result;
}

static node *copy_elements(node *container, node **dst, node *elements, node *(copy_node_)(node *))
{
    if (!dst)
        return allocerror(container, "no elements destination");
    if (!elements)
        return container;
    if (elements->type == MAL_ERROR)
    {
        freenode(container);
        return elements;
    }

    while (elements)
    {
        node *element = copy_node_(elements);
        if (element->type == MAL_ERROR)
        {
            freenode(container);
            return element;
        }

        *dst = element;
        dst = &((*dst)->next);
        elements = elements->next;
    }

    return container;
}

static node *copy_list(node *list, node *(*copy_node_)(node *))
{
    node *result, *elements;

    result = alloclist();
    if (result->type == MAL_ERROR)
        return result;

    return copy_elements(result, &result->u.list, list->u.list, copy_node_);
}

static node *copy_vector(node *vector, node *(*copy_node_)(node *))
{
    node *result;

    result = allocvector();
    if (result->type == MAL_ERROR)
        return result;

    return copy_elements(result, &result->u.list, vector->u.list, copy_node_);
}

static node *copy_hashmap(node *hashmap, node *(*copy_node_)(node *))
{
    node *result, *keys, *values;

    result = allochashmap();
    if (result->type == MAL_ERROR)
        return result;

    result = copy_elements(result, &result->u.hash.keys, hashmap->u.hash.keys, copy_node_);
    if (result->type == MAL_ERROR)
        return result;

    return copy_elements(result, &result->u.hash.values, hashmap->u.hash.values, copy_node_);
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

    if (env->outer)
        return lookup_variable(env->outer, name);

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

    if (env->outer)
        return lookup_function(env->outer, name);

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
    node *(*eval)(environment *, node *, node *(eval_)(environment *, node *));

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
            eval = sym->u.func.eval;
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

    value = remove_variable(env, name);
    freenode(value);

    value = copy_node(value_);
    if (value && value->type == MAL_ERROR)
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

static symbol *add_function(environment *env, char *name, int nonevalargs, node *(*eval)(environment *, node *, node *(*)(environment *, node *)))
{
    symbol *sym;

    remove_function(env, name);

    sym = add_symbol(env, name, SYM_FUNCTION);
    if (!sym)
        return NULL;

    sym->u.func.eval = eval;
    sym->u.func.nonevalargs = nonevalargs;
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

static environment *allocenvironment(environment *outer)
{
    environment *env = calloc(1, sizeof(environment));
    if (!env)
        return NULL;

    env->outer = outer;
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

static node *eval_add(environment *env, node *args, node *(eval_)(environment *, node *))
{
    node *result, *arg = args;
    int sum = 0;

    while (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "argument to + not a number");

        sum += arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    return allocnumber(sum);
}

static node *eval_mul(environment *env, node *args, node *(eval_)(environment *, node *))
{
    node *result, *arg = args;
    int mult = 1;

    while (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "argument to * not a number");

        mult *= arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    return allocnumber(mult);
}

static node *eval_sub(environment *env, node *args, node *(eval_)(environment *, node *))
{
    node *result, *arg = args;
    int remainder = 0;

    if (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "first argument to - not a number");

        remainder = arg->u.number;
        arg = arg->next;
    }

    while (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "argument to - not a number");

        remainder -= arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    return allocnumber(remainder);
}

static node *eval_div(environment *env, node *args, node *(eval_)(environment *, node *))
{
    node *result, *arg = args;
    int quotient = 0;

    if (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "first argument to / not a number");

        quotient = arg->u.number;
        arg = arg->next;
    }

    while (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "division by something other than number");

        if (arg->u.number == 0)
            return allocerror(args, "division by 0");

        quotient /= arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    return allocnumber(quotient);
}

static node *eval_lt(environment *env, node *args, node *(eval_)(environment *, node *))
{
    node *result, *arg = args;
    bool res = false;
    int first;

    if (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "first argument to < not a number");

        first = arg->u.number;
        arg = arg->next;
        res = true;
    }

    while (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "comparison by something other than number");

        res &= first < arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    if (res)
        return alloctrue();
    else
        return allocfalse();
}

static node *eval_lteq(environment *env, node *args, node *(eval_)(environment *, node *))
{
    node *result, *arg = args;
    bool res = false;
    int first;

    if (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "first argument to <= not a number");

        first = arg->u.number;
        arg = arg->next;
        res = true;
    }

    while (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "comparison by something other than number");

        res &= first <= arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    if (res)
        return alloctrue();
    else
        return allocfalse();
}

static node *eval_gt(environment *env, node *args, node *(eval_)(environment *, node *))
{
    node *result, *arg = args;
    bool res = false;
    int first;

    if (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "first argument to > not a number");

        first = arg->u.number;
        arg = arg->next;
        res = true;
    }

    while (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "comparison by something other than number");

        res &= first > arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    if (res)
        return alloctrue();
    else
        return allocfalse();
}

static node *eval_gteq(environment *env, node *args, node *(eval_)(environment *, node *))
{
    node *result, *arg = args;
    bool res = false;
    int first;

    if (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "first argument to >= not a number");

        first = arg->u.number;
        arg = arg->next;
        res = true;
    }

    while (arg)
    {
        if (arg->type != MAL_NUMBER)
            return allocerror(args, "comparison by something other than number");

        res &= first >= arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    if (res)
        return alloctrue();
    else
        return allocfalse();
}

static bool nodes_atom_eq(environment *env, node *fst, node *snd, node *(eval_)(environment *, node *))
{
    switch (fst->type)
    {
    case MAL_NUMBER:
        return fst->u.number == snd->u.number;

    case MAL_SYMBOL:
        return !strcmp(fst->u.sym, snd->u.sym);

    case MAL_KEYWORD:
        return !strcmp(fst->u.keyword, snd->u.keyword);

    case MAL_STRING:
        return !strcmp(fst->u.string, snd->u.string);
    }

    return false;
}

static bool nodes_list_eq(environment *env, node *fst, node *snd, node *(eval_)(environment *, node *))
{
    do
    {
        node *

        if (eq(env, 

    }

}

static bool nodes_eq(environment *env, node *fst, node *snd, node *(eval_)(environment *, node *))
{
    if (fst->type != snd->type)
        return false;

    if (fst->type == MAL_LIST)
        return nodes_list_eq(env, fst, snd, eval_);
    else
        return nodes_atom_eq(env, fst, snd, eval_);
}

static node *eval_eq(environment *env, node *args, node *(eval_)(environment *, node *))
{
    node *result, *first, *arg = args;
    bool res = false;
    int first;

    if (arg)
    {
        first = arg;
        arg = arg->next;
        res = true;
    }

    while (arg)
    {
        if (arg->type != first->type)
            res = false;
        else if (arg->type == MAL_NUMBER && arg->u.number != first->u.number)
            res = false;
        else if (arg->type == MAL_SYMBOL && strcmp(arg->u.symbol, first->u.symbol))
            res = false;
        else if (arg->type == MAL_KEYWORD && strcmp(arg->u.keyword, first->u.keyword))
            res = false;
        else if (arg->type == MAL_STRING && strcmp(arg->u.string, first->u.string))
            res = false;
        else if (arg->type == MAL_LIST)
        {
            firstlist = first->u.list;
            arglist = arg->u.list;

            do
            {
                if (!(firstlist && arglist))
                {
                    res = false;
                    continue;
                }




                firstlist = firstlist->next;
                arglist = arglist->next;

            } while (res && firstlist && arglist)

                }
        }


        res &= first == arg->u.number;
        arg = arg->next;
    }

    freenode(args);

    if (res)
        return alloctrue();
    else
        return allocfalse();
}

static node *eval_def(environment *env, node *args, node *(eval_)(environment *, node *))
{
    node *sym, *value;
    symbol *var;

    if (!args)
        return allocerror(args, "no symbol to define");

    sym = args;
    value = args->next;
    if (sym->type != MAL_SYMBOL)
        return allocerror(args, "not a symbol");
    if (!value)
        return allocerror(args, "symbol value missing");
    if (value->next)
        return allocerror(args, "excessive symbol values");

    var = add_variable(env, args->u.sym, value);
    if (!var)
        return allocerror(args, "unable to define symbol");

    freenode(args);

    return copy_node(var->u.value);
}

static node *eval_let(environment *outer, node *args, node *(eval_)(environment *, node *))
{
    node *result, *bindings, *expression;
    environment *env;

    if (!args)
        return allocerror(args, "no bindings");
    if (args->type != MAL_LIST && args->type != MAL_VECTOR)
        return allocerror(args, "no valid list/vector of bindings");
    if (!args->next)
        return allocerror(args, "no expression to evaluate using bindings");
    if (args->next->next)
        return allocerror(args, "too many expressions to evaluate");

    bindings = args->u.list;
    expression = args->next;

    env = allocenvironment(outer);
    if (!env)
        return allocerror(args, "could not allocate inner environment");

    while (bindings)
    {
        node *value;

        if (bindings && !bindings->next)
        {
            freeenvironment(env);
            return allocerror(args, "unterminated binding");
        }

        if (bindings->type != MAL_SYMBOL)
        {
            freeenvironment(env);
            return allocerror(args, "can not set binding for non-symbol");
        }

        value = eval_(env, bindings->next);
        if (value->type == MAL_ERROR)
        {
            freeenvironment(env);
            return allocerror(args, "can not evaluate binding value");
        }

        if (!add_variable(env, bindings->u.sym, value))
        {
            freenode(value);
            freeenvironment(env);
            return allocerror(args, "could not set binding");
        }
        freenode(value);

        bindings = bindings->next->next;
    }

    result = eval_(env, expression);
    freeenvironment(env);

    return result;
}

static node *eval_function(environment *env, node *list, node *(eval_)(environment *, node *))
{
    symbol *func;
    node *name, *args, *evallist, **dst, *element;

    name = list->u.list;
    args = name ? name->next : NULL;

    func = lookup_function(env, name->u.sym);

    if (!func)
        return allocerror(NULL, "function not found");

    if (func->u.func.nonevalargs >= 0)
    {
        int i = 0;

        evallist = NULL;
        dst = &evallist;

        while (args)
        {
            if (i < func->u.func.nonevalargs)
                element = copy_node(args);
            else
                element = eval_(env, args);
            if (element->type == MAL_ERROR)
            {
                freenode(evallist);
                return element;
            }

            *dst = element;
            dst = &element->next;
            args = args->next;
            i++;
        }

        args = evallist;
    }

    return func->u.func.eval(env, args, eval_);
}

static node *eval_variable(environment *env, node *name)
{
    symbol *var = lookup_variable(env, name->u.sym);
    node *result;

    if (!var)
        return allocerror(NULL, "unbound variable '%s'", name->u.sym);

    return copy_node(var->u.value);
}

static node *eval_append_element(node *container, node **dst, node *element)
{
    if (!dst)
        return allocerror(container, "no element destination");
    if (!element)
        return container;
    if (element->type == MAL_ERROR)
    {
        freenode(container);
        return element;
    }

    while (*dst)
        dst = &((*dst)->next);

    *dst = element;
    return container;
}

static node *eval_list(environment *env, node *list, node *(eval_)(environment *, node *))
{
    node *result;
    node *first, *element;

    first = list->u.list;
    if (first && first->type == MAL_SYMBOL && lookup_function(env, first->u.sym))
        return eval_function(env, list, eval_);

    result = alloclist();
    if (result->type == MAL_ERROR)
        return result;

    element = first;
    while (element)
    {
        result = eval_append_element(result, &result->u.list, eval_(env, element));
        if (result->type == MAL_ERROR)
            return result;

        element = element->next;
    }

    return result;
}

static node *eval_vector(environment *env, node *vector, node *(eval_)(environment *, node *))
{
    node *result;
    node *element, *evallist, **dst;

    result = allocvector();
    if (result->type == MAL_ERROR)
        return result;

    element = vector->u.list;
    while (element)
    {
        result = eval_append_element(result, &result->u.list, eval_(env, element));
        if (result->type == MAL_ERROR)
            return result;

        element = element->next;
    }

    return result;
}

static node *eval_hashmap(environment *env, node *hashmap, node *(eval_)(environment *, node * ))
{
    node *result;
    node *keys, *element;

    result = allochashmap();
    if (result->type == MAL_ERROR)
        return result;

    keys = copy_node(hashmap->u.hash.keys);
    if (keys->type == MAL_ERROR)
    {
        freenode(result);
        return result;
    }
    result->u.hash.keys = keys;

    element = hashmap->u.hash.values;
    while (element)
    {
        result = eval_append_element(result, &result->u.hash.values, eval_(env, element));
        if (result->type == MAL_ERROR)
            return result;

        element = element->next;
    }

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

static node *read_prepend_element(node *container, node **dst, node *element)
{
    if (!dst)
        return allocerror(container, "no element destination");
    if (!element)
        return container;
    if (element->type == MAL_ERROR)
    {
        freenode(container);
        return element;
    }

    element->next = *dst;
    *dst = element;
    return container;
}

static node *read_append_element(node *container, node **dst, node *element)
{
    if (!dst)
        return allocerror(container, "no element destination");
    if (!element)
        return container;
    if (element->type == MAL_ERROR)
    {
        freenode(container);
        return element;
    }

    while (*dst)
        dst = &((*dst)->next);

    *dst = element;
    return container;
}

static node *read_quote(char *quote, char ***tokens, node *(read_form_(char ***)))
{
    node *result, *cdr, *car;

    (*tokens)++;

    result = alloclist();
    if (result->type == MAL_ERROR)
        return result;

    result = read_prepend_element(result, &result->u.list, read_form_(tokens));
    if (result->type == MAL_ERROR)
        return result;

    result = read_prepend_element(result, &result->u.list, allocsymbol(quote));
    if (result->type == MAL_ERROR)
        return result;

    return result;
}

static node *read_metadata(char ***tokens, node *(read_form_(char ***)))
{
    node *result;

    result = alloclist();
    if (result->type == MAL_ERROR)
        return result;

    result = read_prepend_element(result, &result->u.list, read_form_(tokens));
    if (result->type == MAL_ERROR)
        return result;

    result = read_prepend_element(result, &result->u.list, read_form_(tokens));
    if (result->type == MAL_ERROR)
        return result;

    result = read_prepend_element(result, &result->u.list, allocsymbol("with-meta"));
    if (result->type == MAL_ERROR)
        return result;

    return result;
}

static node *read_keyword(char ***tokens, node *(read_form_(char ***)))
{
    node *result;
    char *begin, *end;

    begin = **tokens + 1;
    if (*begin == '\0')
        return allocerror(NULL, "keyword terminated too early");
    end = begin + strlen(begin);

    result = allockeyword(begin, end);
    if (result->type != MAL_ERROR)
        (*tokens)++;

    return result;
}

static node *read_string(char ***tokens, node *(read_form_(char ***)))
{
    node *result;
    char *begin, *end;
    char *str, *src, *dst;
    int len;

    begin = **tokens + 1;
    if (strlen(begin) < 1)
        return allocerror(NULL, "unterminated string");

    end = begin + strlen(begin) - 1;
    if (*end != '"')
        return allocerror(NULL, "string can not be terminated by '%c'", *end);

    result = allocstring(begin, end);
    if (result->type != MAL_ERROR)
        (*tokens)++;

    return result;
}

static node *read_hashmap(char ***tokens, node *(read_form_(char ***)))
{
    node *result;
    bool key = true;

    (*tokens)++;

    result = allochashmap();
    if (result->type == MAL_ERROR)
        return result;

    while (**tokens && strcmp(**tokens, "}"))
    {
        if (key)
            result = read_append_element(result, &result->u.hash.keys, read_form_(tokens));
        else
            result = read_append_element(result, &result->u.hash.values, read_form_(tokens));
        if (result->type == MAL_ERROR)
            return result;

        key = !key;
    }

    if (!**tokens)
        result = allocerror(result, "unterminated hashmap");
    else if (strcmp(**tokens, "}"))
        result = allocerror(result, "expected '}', got '%s'",  **tokens);
    else if (!key)
        result = allocerror(result, "last keyword in hashmap lacks value");
    else
        (*tokens)++;

    return result;
}

static node *read_list(char ***tokens, node *(read_form_(char ***)))
{
    node *result;
    node *contents = NULL;
    node **dst = &contents;
    node *element;

    (*tokens)++;

    result = alloclist();
    if (result->type == MAL_ERROR)
        return result;

    while (**tokens && strcmp(**tokens, ")"))
    {
        result = read_append_element(result, &result->u.list, read_form_(tokens));
        if (result->type == MAL_ERROR)
            return result;
    }

    if (!**tokens)
        result = allocerror(result, "unterminated list");
    else if (strcmp(**tokens, ")"))
        result = allocerror(result, "expected ')', got '%s'", **tokens);
    else
        (*tokens)++;

    return result;
}

static node *read_vector(char ***tokens, node *(read_form_(char ***)))
{
    node *result;
    node *contents = NULL;
    node **dst = &contents;
    node *element;

    (*tokens)++;

    result = allocvector();
    if (result->type == MAL_ERROR)
        return result;

    while (**tokens && strcmp(**tokens, "]"))
    {
        result = read_append_element(result, &result->u.list, read_form_(tokens));
        if (result->type == MAL_ERROR)
            return result;
    }

    if (!**tokens)
        result = allocerror(result, "unterminated vector");
    else if (strcmp(**tokens, "]"))
        result = allocerror(result, "expected ']', got '%s'", **tokens);
    else
        (*tokens)++;

    return result;
}

static node *read_number(char ***tokens)
{
    node *result;
    const char *allowed = "0123456789";
    bool negative;
    int val = 0;
    char *c = **tokens;

    if (*c == '+')
    {
        negative = false;
        c++;
    }
    else if (*c == '-')
    {
        negative = true;
        c++;
    }
    else
        negative = false;

    if (!*c)
        return NULL;

    while (*c && strchr(allowed, *c))
    {
        val = val * 10 + (*c - '0');
        c++;
    }

    if (*c)
        return NULL;

    if (negative)
        val = -val;

    return allocnumber(val);
}

static node *read_atom(char ***tokens)
{
    node *result = NULL;

    if (!**tokens)
        return allocerror(NULL, "no token to read");
    else if (!strcmp(**tokens, "nil"))
        result = allocnil();
    else if (!strcmp(**tokens, "true"))
        result = alloctrue();
    else if (!strcmp(**tokens, "false"))
        result = allocfalse();
    else
        result = read_number(tokens);

    if (!result && strlen(**tokens))
        result = allocsymbol(**tokens);
    if (!result)
        return allocerror(NULL, "token of unknown type");

    (*tokens)++;
    return result;
}

static node *read_form_(char ***tokens)
{
    node *result, *n, *car, *cdr;
    char *expected = NULL;
    char *sym;

    if (!*tokens)
        return alloceof();
    if (**tokens == NULL)
        return NULL;

    if (!strcmp(**tokens, "~@"))
        result = read_quote("splite-unquote", tokens, read_form_);
    else if (!strcmp(**tokens, "'"))
        result = read_quote("quote", tokens, read_form_);
    else if (!strcmp(**tokens, "`"))
        result = read_quote("quasiquote", tokens, read_form_);
    else if (!strcmp(**tokens, "~"))
        result = read_quote("unquote", tokens, read_form_);
    else if (!strcmp(**tokens, "@"))
        result = read_quote("deref", tokens, read_form_);
    else if (!strcmp(**tokens, "^"))
        result = read_metadata(tokens, read_form_);
    else if (!strcmp(**tokens, "{"))
        result = read_hashmap(tokens, read_form_);
    else if (!strcmp(**tokens, "("))
        result = read_list(tokens, read_form_);
    else if (!strcmp(**tokens, "["))
        result = read_vector(tokens, read_form_);
    else if (***tokens == ':')
        result = read_keyword(tokens, read_form_);
    else if (***tokens == '"')
        result = read_string(tokens, read_form_);
    else
        result = read_atom(tokens);

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

static char *tokenize_spliceunquote(char *line, char **begin, char **end)
{
    if (strncmp(line, "~@", 2))
        return line;

    *begin = line;
    *end = line + 2;
    return *end;
}

static char *tokenize_special(char *line, char **begin, char **end)
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

static char *tokenize_keyword(char *line, char **begin, char **end)
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

static char *tokenize_string(char *line, char **begin, char **end)
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

static char *tokenize_symbol(char *line, char **begin, char **end)
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
        if (!begin && !end) line = tokenize_spliceunquote(line, &begin, &end);
        if (!begin && !end) line = tokenize_keyword(line, &begin, &end);
        if (!begin && !end) line = tokenize_special(line, &begin, &end);
        if (!begin && !end) line = tokenize_string(line, &begin, &end);
        if (!begin && !end) line = tokenize_symbol(line, &begin, &end);
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
    environment *env = allocenvironment(NULL);
    if (!env)
        return NULL;

    add_function(env, "def!", 1, eval_def);
    add_function(env, "let*", 2, eval_let);

    add_function(env, "+", 0, eval_add);
    add_function(env, "*", 0, eval_mul);
    add_function(env, "-", 0, eval_sub);
    add_function(env, "/", 0, eval_div);
    add_function(env, "<", 0, eval_lt);
    add_function(env, ">", 0, eval_gt);
    add_function(env, "<=", 0, eval_lteq);
    add_function(env, ">=", 0, eval_gteq);
    add_function(env, "=", 0, eval_eq);

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
