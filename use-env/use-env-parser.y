/*****************************************************************************
 *
 *  Copyright (C) 2007-2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *
 *  UCRL-CODE-235358
 * 
 *  This file is part of chaos-spankings, a set of spank plugins for SLURM.
 * 
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

%{ 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fnmatch.h>

#include "use-env.h"
#include "log_msg.h"
#include "list.h"

#define YYDEBUG 1
int yydebug = 0;

extern int yylex ();
void yyerror (const char *);

/*
 * Set parser options from config file
 */
static int set_parser_option (const char *option, struct lex_item *x);
static int define_symbol (char *name, struct lex_item *x);

/*
 * Environment manipulation functions
 */
static int env_var_set (char *name, char *val, int op);
static int env_var_unset (char *name);

/*
 * Condition functions
 */
static int condition_push_if (int val);
static int condition_push_else_if (int val);
static int condition_push_else ();
static int condition_pop_endif ();
static int condition_pop ();
static int condition ();

/*
 * Special in-task block
 */
static int in_task_begin ();
static int in_task_end ();

/*
 * Item tests
 */
static int do_fnmatch (struct lex_item *x, struct lex_item *y);
static int item_defined (struct lex_item *i);
static int cmp_items (int cmp, struct lex_item *x, struct lex_item *y);
static int test_item (struct lex_item *i);
static int include_file (char *name);
static void dump_item (char *name);


struct parser_ctx {
    int in_task;
    struct use_env_ops *ops;
    void *arg;
};

static struct parser_ctx ctx = { 0, NULL, NULL };
    

%}

%union {
    int val;
    struct lex_item *item;
}

%token APPEND 
%token PREPEND 
%token UNSET 
%token INCLUDE 
%token COND_SET 
%token IF 
%token ELSE 
%token ENDIF 
%token AND 
%token OR 
%token DEFINED
%token PRINT
%token SET
%token DEF
%token UNDEF
%token DUMP
%token IN_TASK
%token MATCH
%token <item> ITEM
%token <val>  EQ LT GT LE GE NE 

%type <val> test tests cmp op

%left EQ LT GT LE GE NE AND OR '!' 

%%

stmts   : /* empty */
        | stmt               { lex_item_cache_clear (); }
        | stmts stmt         { lex_item_cache_clear (); }
        ;

stmt    : stmt_end
        | expr stmt_end
        | if_stmt stmt_end
        | in_task stmt_end
        | print stmt_end
        | error stmt_end
        | INCLUDE ITEM '\n'  { if (include_file ($2->name) < 0)  YYABORT; }
        ;

stmt_end: '\n'
        | ';'
        ;

print   : PRINT ITEM         { if (condition()) printf ("%s\n", item_str ($2)); }
        ;

if_stmt : IF '(' tests ')'   { if (condition_push_if ($3) < 0) YYABORT; } 
          '\n' 
          stmts if_tail 
        ;

if_tail : ENDIF              { condition_pop_endif (); }

        | ELSE  '\n'         { if (condition_push_else () < 0) YYABORT; }
          stmts ENDIF        { condition_pop_endif (); }

        | ELSE IF            { condition_pop (); } 
          '(' tests ')'      { if (condition_push_else_if ($5) < 0) YYABORT; }
          '\n'
          stmts if_tail
        ;

in_task : IN_TASK            { in_task_begin (); }
          block              { in_task_end (); }
        | IN_TASK '\n'       { in_task_begin (); }
          block              { in_task_end (); }

block   : '{' stmts '}'

tests   : tests AND test     { $$ = ($1 && $3); }
        | tests OR  test     { $$ = ($1 || $3); }
        | test
        ;

test    : ITEM cmp ITEM      { if (($$ = cmp_items ($2, $1, $3)) < 0) YYABORT; }
        | DEFINED ITEM       { if (($$ = item_defined ($2)) < 0) YYABORT; }
        | ITEM               { if (($$ = test_item ($1))    < 0) YYABORT; }
        | '(' test ')'       { $$ = $2; }
        | '!' test           { if (condition ()) $$ = !($2); else $$ = 0; }
        | ITEM MATCH ITEM    { if (($$ = do_fnmatch ($3, $1)) < 0) YYABORT; }


expr    : ITEM op ITEM       { env_var_set ($1->name, item_str ($3), $2); }
        | UNSET ITEM         { env_var_unset ($2->name); }
        | SET ITEM ITEM      { set_parser_option ($2->name, $3); }    
        | DUMP ITEM          { dump_item ($2->name); }
        | DEF ITEM '=' ITEM  { if (define_symbol ($2->name, $4) < 0) YYABORT; }
        | UNDEF ITEM         { if (condition ()) sym_delete ($2->name); }
        ;

op      : '='                { $$ = '='; }
        | COND_SET           { $$ = COND_SET; }
        | APPEND             { $$ = APPEND; } 
        | PREPEND            { $$ = PREPEND; }
        ;

cmp     : EQ                 { $$ = EQ; }
        | LT                 { $$ = LT; }
        | GT                 { $$ = GT; }
        | LE                 { $$ = LE; }
        | GE                 { $$ = GE; }
        | NE                 { $$ = NE; }
        ;

%%

void yyerror (const char *msg)
{
    log_err ("%s\n", msg);
}

/****************************************************************************
 *  Data Types
 ****************************************************************************/

struct cond {
    unsigned int val:1;
    unsigned int fallthru:1;
};

/****************************************************************************
 *  Global static variables
 ****************************************************************************/

static List cond_stack = NULL;

/****************************************************************************
 *  Includes
 ****************************************************************************/

static int include_file (char *name)
{
    if (condition ())
        return (lex_include_push (name));

    return (0);
}

/****************************************************************************
 *  Item tests
 ****************************************************************************/

static int do_fnmatch (struct lex_item *x, struct lex_item *y)
{
    log_debug ("fnmatch (\"%s\", \"%s\")\n", item_str (x), item_str (y));
    if (condition ())
        return (fnmatch (item_str (x), item_str (y), 0) == 0);
    return (0);
}

static int item_defined (struct lex_item *i)
{
    if (i->type != TYPE_SYM) {
        log_err ("use of `defined' keyword on non-symbol \"%s\"\n", i->name);
        return (-1);
    } 
    
    if (condition ())
        return (i->val.sym != NULL);
    else
        return (0);
}

static int test_item (struct lex_item *i)
{
    int rc = 0;

    if (!condition ())
        return (0);

    /*
     *  Return 0 unless item is INT and non-zero, or
     *    item string is not empty.
     */
    if (item_type_int (i))
        rc = (item_val (i) != 0);
    else {
        char *p = item_str (i);
        rc = p ? strlen (p) > 0 : 0;
    }

    return (rc);
}

static int cmp_items (int cmp, struct lex_item *x, struct lex_item *y) 
{
    if (condition () == 0) 
        return (0);

    return (item_cmp (cmp, x, y));
}
  
/****************************************************************************
 *  Set parser options
 ****************************************************************************/

static int set_parser_option (const char *option, struct lex_item *x)
{
    if (condition() == 0)
        return (0);

    if (strcmp (option, "debuglevel") == 0) {

        if (!item_type_int (x)) {
            log_err ("Invalid value in \"set debuglevel %s\"\n", item_str (x));
            return (-1);
        }

        log_msg_set_verbose (item_val (x));

        log_verbose ("set debuglevel %d\n", item_val (x));
    } 
    else {
        log_err ("Unknown option \"%s\" to set keyword\n", option);
        return (-1);
    }

    return (0);
}

static void dump_item (char *name)
{
    if (condition() == 0)
        return;

    if (strncmp (name, "symbols", strlen (name)) == 0)
        dump_symbols ();
    else if (strncmp (name, "keywords", strlen (name)) == 0)
        dump_keywords ();
    else if (strncmp (name, "all", strlen (name)) == 0) {
        dump_keywords ();
        dump_symbols ();
    } 
    else
        log_err ("Invalid argument \"%s\" to `dump' command\n", name);

    return;
}

static int define_symbol (char *name, struct lex_item *x)
{
    if (!is_valid_identifier (name)) {
        log_err ("Unable to define invalid identifier \"%s\"\n", name);
        return (-1);
    }

    if (condition() == 0)
        return (0);

    log_verbose ("define %s = \"%s\"\n", name, item_str (x));

    return (sym_define (name, item_str (x)) != NULL);
}

/****************************************************************************
 *  Environment manipulation
 ****************************************************************************/

const char * xgetenv (const char *name)
{
    if (ctx.ops && ctx.ops->getenv)
        return ((*ctx.ops->getenv) (ctx.arg, name));
    else
        return (getenv (name));
}

int xunsetenv (const char *name)
{
    if (ctx.ops && ctx.ops->getenv)
        return ((*ctx.ops->unsetenv) (ctx.arg, name));
    else
        return (unsetenv (name));
}

int xsetenv (const char *name, const char *value, int overwrite)
{
    if (ctx.ops && ctx.ops->setenv)
        return ((*ctx.ops->setenv) (ctx.arg, name, value, overwrite));
    else
        return (setenv (name, value, overwrite));
}

static char * env_var_add (char *buf, size_t size,
        const char *orig, const char *val, int append)
{
    if (strlen (val) >= size)
        return (NULL);

    if (strlen (orig) == 0)
        return (strcpy (buf, val));

    if ((strlen (val) + strlen (orig) + 2) > size)
        return (NULL);

    if (append)
        snprintf (buf, size, "%s:%s", orig, val);
    else
        snprintf (buf, size, "%s:%s", val, orig);

    return (buf);
}

static int env_var_unset (char *name)
{
    if (!is_valid_identifier (name))
        return (log_err ("Invalid identifier \"%s\" in unset\n", name));

    if (condition () == 0)
        return (0);

    log_verbose ("unsetenv (%s)\n", name);

    /* 
     * Delete any references to this value in the local env_cache
     */
    env_cache_delete (name);

    if (xunsetenv (name) < 0)
        return ((log_err ("unsetenv (%s): %s\n", name, strerror (errno))));

    return (0);
}


static int env_var_set (char *name, char *val, int op)
{
    char buf [4096];
    const char *orig = NULL;
    char *newval = val;
    int overwrite = 1;

    if (!is_valid_identifier (name))
        return (log_err ("Invalid identifier \"%s\" in expression\n", name));

    if (condition () == 0)
        return (0);

    if (op == COND_SET)
        overwrite = 0;

    if (((op == APPEND) || (op == PREPEND)) && (orig = xgetenv (name)))
        newval = env_var_add (buf, sizeof (buf), orig, val, op == APPEND);

    /* 
     * Delete any references to this value in the local env_cache
     */
    env_cache_delete (name);

    log_verbose ("setenv (%s, \"%s\", overwrite=%d)\n", 
                 name, newval, overwrite);

    return (xsetenv (name, newval, overwrite));
}


/****************************************************************************
 *  Conditional stack
 ****************************************************************************/

static struct cond * cond_create (int v)
{
    struct cond *c = malloc (sizeof (*c));

    if (c == NULL) 
        return (NULL);

    c->val = v;
    c->fallthru = 0; 
    return (c);
}

static void cond_destroy (struct cond *c)
{
    free (c);
}

static void condition_fallthru_set ()
{
    struct cond *c = list_peek (cond_stack);
    c->fallthru = 1;
}

static void condition_fallthru_clear ()
{
    struct cond *c = list_peek (cond_stack);
    c->fallthru = 0;
}

static int condition_fallthru ()
{
    struct cond *c = list_peek (cond_stack);
    return (c->fallthru);
}

int condition ()
{
    struct cond *c = list_peek (cond_stack);
    /*
     *  The current condition value must be true
     *   AND fallthru must NOT be set in order 
     *   to evaluate expressions.
     *
     *  If fallthru is set this means we are in
     *   the middle of evaluating an if/else(if)* 
     *   within this block, and the true condition
     *   was already evaluated. Thus we no longer need
     *   to evaluate expressions in else if's for this 
     *   block.
     */
    return (c->val && !c->fallthru);
}

static int condition_push_val (int val)
{
    struct cond *c;

    if (!(c = cond_create (val))) 
        return (-1);

    log_debug2 ("Pushing new condition %d\n", val);

    list_push (cond_stack, c);

    return (c->val);
}

void condition_init ()
{
    cond_stack = list_create ((ListDelF) cond_destroy);

    /*
     *  Intiial condition is false if we're in task context.
     */
    if (ctx.in_task)
        condition_push_val (0);
    else
        condition_push_val (1);
}

void condition_fini ()
{
    if (cond_stack) {
        list_destroy (cond_stack);
        cond_stack = NULL;
    }
}

static int condition_pop ()
{
    int rv;
    struct cond *c;


    if (!(c = list_pop (cond_stack))) 
        return (log_err ("else/endif without if"));

    log_debug2 ("Popped old condition %d\n", c->val);

    rv = c->val;
    cond_destroy (c);

    return (rv);
}

static int condition_pop_endif ()
{
    int rv = condition_pop ();
    /*
     *  endif resets fallthru state
     */
    condition_fallthru_clear ();
    return (rv); 
}

static int condition_push_if (int val)
{
    /* 
     *  If this `if' statement is true, then update current
     *   fall thru state to true so we fall through any
     *   subsequent else statements (and don't evaluate
     *   any else if expressions).
     */
    if (val)
        condition_fallthru_set ();

    return (condition_push_val (val));
}

static int condition_push_else ()
{
    int val = condition_pop ();

    /* 
     *  If we're falling through subsequent else's push false 
     *   Otherwise, push the inverse of the last value if
     *   this block is being evaluated (condtion == true)
     */
    val = 0;
    if (condition () && !condition_fallthru ())
        val = !val;

    return (condition_push_val (val));
}

static int condition_push_else_if (int val)
{
    if (!condition () || condition_fallthru ())
        val = 0;
    else if (val != 0)
        condition_fallthru_set ();

    return (condition_push_val (val));
}

/****************************************************************************
 *  In-task support
 ****************************************************************************/

static int in_task_begin (void)
{
    log_debug ("Found `in task' block: in_task = %d\n", ctx.in_task);
    return condition_push_val (ctx.in_task);
}

static int in_task_end (void)
{
    return condition_pop ();
}


/****************************************************************************
 *  Initialization and Cleanup
 ****************************************************************************/

void use_env_parser_init (int in_task)
{
    ctx.in_task = in_task;
    /*
     *  Keytab created on-demand
     */
    condition_init ();
}

void use_env_set_operations (struct use_env_ops *ops, void *arg)
{
    ctx.ops = ops;
    ctx.arg = arg;
}

int use_env_parse (const char *filename)
{
    if (lex_file_init (filename) < 0) {
        log_err ("Failed to open config file %s\n", filename);
        return (-1);
    }

    if (yyparse ()) {
        log_err ("%s: Parser failed.\n", filename);
        return (-1);
    }

    lex_fini ();

    return (0);
}

void use_env_parser_fini ()
{
    condition_fini ();
    keytab_destroy ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
