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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <libgen.h>
#include <ctype.h>
#include <errno.h>

#include "use-env.h"
#include "use-env-parser.h" 
#include "list.h" 
#include "log_msg.h"

static char *s;
static char buf [4096];

/* 
 *  True if we've returned an item in POSTOP condition 
 */
static int  postop_got_item = 0; 

extern int yyerror (char *);

/*
 *  Macro for entering POSTOP start condition:
 *   - Initialize buf and string pointer `s'
 *   - reset postop_got_item to 0
 */
#define BEGIN_POSTOP \
    do { \
        memset (s = buf, 0, sizeof (buf)); \
        BEGIN (POSTOP); \
        postop_got_item = 0; \
    } while (0)

/*
 *  Initialize string buffer and begin STR condition.
 */
#define BEGIN_STR \
    do { \
        memset (s = buf, 0, sizeof (buf)); \
        BEGIN (STR); \
    } while (0)

/*
 *  Place a bracketed identifier ${id} item into yylval.item
 */
#define GET_BRACKETED_ITEM \
    do { \
        yytext [strlen(yytext) - 1] = '\0'; /* Nullify closing brace */ \
        yylval.item = lex_item_create (yytext+2, TYPE_SYM); \
    } while (0)

              

%}

%option noyywrap 

digit [0-9]
alpha [a-zA-Z]
alnum [0-9a-zA-Z]
ident [0-9a-zA-Z_]
id    [_a-zA-z][0-9a-zA-Z_]*
p     [\)<>;=+\n \t#]


%x STR STR2 POSTOP

%%

[ \t]+      ; /* Ignore whitespace */
#[^\n]*     ; /* Ignore comments   */

\n           lex_line_increment (); return '\n';

\"           BEGIN_STR;

dump         return DUMP;
define       return DEF;
undefine     return UNDEF;
set          return SET;
unset        return UNSET;
if           return IF; 
else         return ELSE;
endif        return ENDIF;
defined      return DEFINED; 
"in task"    return IN_TASK;
match(es)?   return MATCH;

print        BEGIN_POSTOP; return PRINT;
include      BEGIN_POSTOP; return INCLUDE; 
"|="         BEGIN_POSTOP; return COND_SET;
"+="         BEGIN_POSTOP; return PREPEND;
"=+"         BEGIN_POSTOP; return APPEND;
"="          BEGIN_POSTOP; return '=';
","          return ',';
"!"          return '!';
"("          return '('; 
")"          return ')';
"{"          return '{';
"}"          return '}';
";"          return ';';
"<"          return LT;
">"          return GT;
"=="         return EQ;
"<="         return LE;
">="         return GE;
"!="         return NE; 
"&&"         return AND;
"||"         return OR;


[0-9]+/{p}   { yylval.item = lex_item_create (yytext, TYPE_INT); return ITEM; }

{ident}+     { yylval.item = lex_item_create (yytext, TYPE_STR); return ITEM; }

\${id}       { yylval.item = lex_item_create (yytext+1, TYPE_SYM); return ITEM;}

\$\{{id}\}   { GET_BRACKETED_ITEM; return ITEM; }


<POSTOP>{
    \"   { BEGIN (STR2); }

    (\n|;) {
        BEGIN INITIAL;
        unput (*yytext); /* Return the newline or ; to the stream */
        if (strlen (buf) || !postop_got_item) {
            yylval.item = lex_item_create (buf, TYPE_STR);
            return ITEM;
        }
    }

    [ \t]+ {
        if (strlen (buf)) { /* Don't return an empty string separated by ws */
            postop_got_item = 1;
            yylval.item = lex_item_create (buf, TYPE_STR);
            memset (s = buf, 0, sizeof (buf));
            return ITEM;
        }
    }

    #[^\n]*  ; /* Skip comments */

    \\\   { *s++ = ' '; }
}

<STR>{
    \" { 
        BEGIN INITIAL;
        yylval.item = lex_item_create (buf, TYPE_STR);
        return ITEM;
    }
}

<STR2>{
    \" {  
        postop_got_item = 1;
        *s = '\0';
        yylval.item = lex_item_create (buf, TYPE_STR);
        memset (s = buf, 0, sizeof (buf));
        BEGIN POSTOP;
        return ITEM; 
    }
}

<STR,STR2>{
    \n { 
        log_err ("Unterminated double-quoted string?\n");
        lex_line_increment (); 
        BEGIN (INITIAL);
    }
}

<STR,STR2,POSTOP>{
    ~ {
        const char *home;
        if ((s == buf) && (home = getenv ("HOME"))) {
            strncat (buf, home, sizeof (buf));
            s += strlen (home);
        } else
            *s++ = '~';
    }

    \${id} { 
        const struct sym *m = sym (yytext+1);
        if (m) {
            strncat (buf, m->string, sizeof (buf));
            s += strlen (m->string);
        }
    }
    \$\{{id}\} {
        const struct sym *m;
        yytext[strlen(yytext)-1] = '\0'; /* Nullify closing brace */
        if ((m = sym (yytext+2))) {
            *s = '\0';
            strncat (buf, m->string, sizeof (buf));
            s += strlen (m->string);
        }
    }
    \\$   { *s++ = '$';     }
    \\n   { *s++ = '\n';    }
    \\t   { *s++ = '\t';    }
    \\r   { *s++ = '\r';    }
    \\\"  { *s++ = '\"';    }
    .     { *s++ = *yytext; }
}


<<EOF>> {
    if (!lex_include_pop ())
        yyterminate ();
}

%%


/****************************************************************************
 *  Data Types
 ****************************************************************************/

struct file_info {
    FILE *          fp;
    char *          path;
    int             line;
    YY_BUFFER_STATE yybuf;
};


/****************************************************************************
 *  Static Globals
 ****************************************************************************/

static List includes = NULL;
static struct file_info *current;

/*
 *  Three-level symbol table. I know, overly complex - but it is actually
 *    pretty simple. 
 *
 *  Keywords (stored in the keytab) have the highest precedence and
 *    cannot be overridden by the config file nor environment.
 * 
 *  Local symbols defined by the user using the "define" command are
 *    stored in the symtab. These have higher precedence than environment
 *    variables, and can be updated and changed by the user with
 *    subsequent ``define'' invocations.
 *
 *  The envtab contains cached environment variable "symbol" records
 *    for later destruction.
 */
static List keytab = NULL;
static List symtab = NULL;
static List envtab = NULL;

static List itemcache = NULL;

/****************************************************************************
 *  Include file funtions
 ****************************************************************************/

static void file_info_destroy (struct file_info *f)
{
    if (f == NULL)
        return;
    if (f->path)
        free (f->path);
    if (f->fp && f->fp != stdin)
        fclose (f->fp);
    if (f->yybuf)
        yy_delete_buffer (f->yybuf);
    free (f);
    return;
}

static struct file_info * file_info_create (const char *path)
{
    struct file_info *f = malloc (sizeof (*f));

    memset (f, 0, sizeof (*f));
    
    if (f == NULL)
        return (NULL);

    f->line = 1;

    if (path == NULL) {
        f->path = strdup ("stdin");
        f->fp   = stdin;
    } else {
        f->path = strdup (path);

        if ((f->fp = fopen (path, "r")) == NULL) {
            if (current) 
                log_err ("failed to include \"%s\"\n", path);
            else
                log_err ("Failed to open %s: %s\n", path, strerror (errno));

            file_info_destroy (f);
            return (NULL);
        }
    }

    f->yybuf = yy_create_buffer (f->fp, YY_BUF_SIZE);

    return (f);
}

static int lex_switch_buffer (struct file_info *f)
{
    yyin = f->fp;
    yy_switch_to_buffer (f->yybuf);
    current = f;
    return (0);
}

static int find_f (struct file_info *f, char *file)
{
    return (strcmp (f->path, file) == 0);
}

int lex_file_init (const char *path)
{
    struct file_info *f = file_info_create (path);

    if (f == NULL)
        return (-1);

    lex_switch_buffer (f);

    return (0);
}

const char * lex_file ()
{
    if (!current)
        return (NULL);
    return (current->path);
}

int lex_line ()
{
    if (!current)
        return (0);
    return (current->line);
}

int lex_line_increment ()
{
    if (!current)
        return (0);
    return (current->line++);
}

static char * full_path (const char *path, const char *include, 
    char *buf, size_t len)
{
    char *p = strdup (path);
    char *prefix;

    if (p == NULL)
        return (NULL);

    if (include[0] == '/')
        return (strdup (include));

    if (strcmp ("stdin", path) == 0)
        prefix = ".";
    else 
        prefix = dirname (p);

    snprintf (buf, len, "%s/%s", prefix, include);

    buf [len - 1] = '\0';

    free (p);

    return (buf);
}


int lex_include_push (const char *include)
{
    struct file_info *f;
    char buf [4096];
    char *path;

    assert (include != NULL);

    /*
     *  Decrement line counter for this file so that error messages
     *   correspond to the line that the include is on.
     */
    current->line--;

    path = full_path (current->path, include, buf, sizeof (buf));

    if ((path == NULL) || !(f = file_info_create (path)))
        return (-1);

    if (!includes)
        includes = list_create ((ListDelF) file_info_destroy);
    else if (list_find_first (includes, (ListFindF) find_f, f->path)) {
        log_err ("Recursively included file\n");
        file_info_destroy (f);
        return (-1);
    } 
    else if (list_count (includes) > 20) {
        log_err ("include files nested too deep\n");
        file_info_destroy (f);
        return (-1);
    }
    log_verbose ("including file %s\n", f->path);

    current->fp    = yyin;
    current->yybuf = YY_CURRENT_BUFFER;

    list_push (includes, current);

    lex_switch_buffer (f);

    return (0);
}

int lex_include_pop ()
{
    struct file_info *f, *tmp = current;

    if (!includes)
        return (0);

    assert (current);

    if (!(f = list_pop (includes)))
        return (0);

    lex_switch_buffer (f);

    /*  
     *  Re-increment line counter when popping back to original file.
     */
    current->line++;

    log_verbose ("popping back to file %s\n", current->path);

    file_info_destroy (tmp);

    return (1);
}


/****************************************************************************
 *  Lex Item Functions
 ****************************************************************************/


static void lex_item_clear (struct lex_item *i)
{
    if ((i->type == TYPE_SYM) && (i->val.sym == NULL) && i->str)
        free (i->str);
    if (i->name)
        free (i->name);
    memset (i, 0, sizeof (*i));
    return;
}

static void lex_item_destroy (struct lex_item *i)
{
    lex_item_clear (i);
    free (i);
}

static int item_unused (struct lex_item *i, void *arg)
{
    return (i->used == 0);
}

static struct lex_item * item_cache_find_unused ()
{
    if (itemcache == NULL) 
        itemcache = list_create ((ListDelF) lex_item_destroy);

    return (list_find_first (itemcache, (ListFindF) item_unused, NULL));
}

static struct lex_item * lex_item_alloc ()
{
    struct lex_item *i = item_cache_find_unused ();

    if (i == NULL) {
        log_debug3 ("allocated new lex_item\n");
        i = malloc (sizeof (*i));
        list_append (itemcache, i);
    } else
        log_debug3 ("pulled lex_item off cache with %d items\n",
            list_count (itemcache));

    i->used = 1;

    return (i);
}

struct lex_item * lex_item_create (char *name, int type)
{
    struct lex_item *i = lex_item_alloc ();

    i->name = strdup (name);
    i->str  = i->name;
    i->type = type;

    if (type == TYPE_STR) 
        i->val.str = i->name;
    else if (type == TYPE_INT) 
        i->val.num = atoi (name);
    else if (type == TYPE_SYM) {
        if ((i->val.sym = sym (name)))
            i->str = i->val.sym->string;
        else 
            i->str = strdup ("");
    }

    log_debug2 ("creating item \"%s\"\n", name);

    return (i);
}

static int item_clear (struct lex_item *i, void *arg)
{
    if (i->used) {
        lex_item_clear (i);
        i->used = 0;
    }
    return (0);
}

void lex_item_cache_clear ()
{
    int a = 1;

    if (itemcache == NULL)
        return;

    log_debug3 ("clearing %d items in cache\n", list_count (itemcache));

    list_for_each (itemcache, (ListForF) item_clear, (void *) &a);
}

int item_type_int (struct lex_item *i)
{
    if (i->type == TYPE_INT)
        return (1);
    if ((i->type == TYPE_SYM) && i->val.sym && (i->val.sym->type == SYM_INT))
        return (1);
    return (0);
}

int item_val (struct lex_item *item)
{
    assert (item_type_int (item));

    if (item->type == TYPE_INT)
        return (item->val.num);

    if (item->type == TYPE_SYM)
        return (item->val.sym->val);

    return (0);
}

char * item_str (struct lex_item *item)
{
    return (item->str);
}

int item_strcmp (struct lex_item *x, struct lex_item *y)
{
    return (strcmp (item_str (x), item_str (y)));
}

static const char * cmp_str (int cmp)
{
    switch (cmp) {
        case LT: return "<";
        case GT: return ">";
        case LE: return "<=";
        case GE: return ">=";
        case EQ: return "==";
        case NE: return "!=";
    }
    return ("??");
}

int item_cmp (int cmp, struct lex_item *x, struct lex_item *y)
{
    int rv = -1;

    switch (cmp) {
    case LT:
        if (item_type_int (x) && item_type_int (y))
            rv = (item_val (x) < item_val (y));
        break;
    case GT:
        if (item_type_int (x) && item_type_int (y))
            rv = (item_val (x) > item_val(y));
        break;
    case LE:
        if (item_type_int (x) && item_type_int (y))
            rv = (item_val (x) <= item_val (y));
        break;
    case GE:
        if (item_type_int (x) && item_type_int (y))
            rv = (item_val (x) >= item_val (y));
        break;
    case EQ:
        if (item_type_int (x) && item_type_int (y))
            rv = (item_val (x) == item_val (y));
        else
            rv = (item_strcmp (x, y) == 0);
        break;
    case NE:
        if (item_type_int (x) && item_type_int (y))
            rv = (x->val.num != y->val.num);
        else
            rv = (item_strcmp (x, y) != 0);
        break;
    default:
        log_err ("Invalid comparitor %d\n", cmp);
    }

    if (rv < 0)
        log_err ("Invalid comparison: `%s'(=%s) %s `%s'(=%s)\n",
                x->name, item_str (x), cmp_str (cmp), 
                y->name, item_str (y));
    else
        log_debug ("testing: (`%s'(=%s) %s `%s'(=%s)) = %s\n",
                x->name, item_str (x), cmp_str (cmp), 
                y->name, item_str (y), (rv ? "true":"false"));

    return (rv);
}

int is_valid_identifier (const char *str)
{
    const char *p;

    if (!str)
        return (0);

    /*
     *  First character must be [a-zA-Z_]
     */
    if (!(isalpha (str[0]) || str[0] == '_')) 
        return (0);

    for (p = str + 1; *p != '\0'; p++) {
        if (!(isalnum (*p) || *p == '_')) 
            return (0);
    }

    return (1);
}


/****************************************************************************
 *  Symbol functions
 ****************************************************************************/

int sym_find (struct sym *s, char *name)
{
	return (strcmp (s->name, name) == 0);
}

void sym_destroy (struct sym *s)
{
	if (s == NULL)
		return;

	if (s->name)
		free (s->name);
	if (s->string)
		free (s->string);
	free (s);
}

static int sym_reset_value (struct sym *s, const char *value)
{
    long val;
    char *p;

    if (s->string)
        free (s->string);

    s->string = strdup (value);
    s->type   = SYM_STR;
    s->val    = -1;

    val = strtol (value, &p, 10);

    if (p && *p == '\0') {
        s->type = SYM_INT;
        s->val = (int) val;
    } 

    return (0);
}

struct sym * sym_create (const char *name, const char *value)
{
    struct sym *s = malloc (sizeof (*s));

    memset (s, 0, sizeof (*s));

    s->name = strdup (name);

    sym_reset_value (s, value);

    return (s);

}

static struct sym * sym_lookup (List l, char *s)
{
    if (l == NULL)
        return (NULL);
	return (list_find_first (l, (ListFindF) sym_find, s));
}

int sym_delete (char *name)
{
    int rc = 0;

    log_verbose ("undef \"%s\"\n", name);

    if (symtab)
        rc = list_delete_all (symtab, (ListFindF) sym_find, name);

    return (rc);
}

int env_cache_delete (char *name)
{
    int rc = 0;
    if (envtab)
        rc = list_delete_all (envtab, (ListFindF) sym_find, name);

    return (rc);
}

const struct sym * keyword_define (char *name, const char *value)
{
    struct sym *s;

    if (!keytab)
        keytab = list_create ((ListDelF) sym_destroy);
    else
        list_delete_all (keytab, (ListFindF) sym_find, name);

    if ((s = sym_create (name, value)))
        list_prepend (keytab, s);

    return (s);
}

const struct sym * sym_define (char *name, const char *value)
{
	struct sym *s;

    /*
     *  Do not override a keyword with a symbol
     */
	if (sym_lookup (keytab, name)) 
        return (NULL);

	if (!symtab)
		symtab = list_create ((ListDelF) sym_destroy);

    if ((s = sym_lookup (symtab, name))) 
        sym_reset_value (s, value);
    else if ((s = sym_create (name, value)))
        list_prepend (symtab, s);

	return (s);
}

static const struct sym * env_sym_create (char *name, const char *value)
{
    struct sym *s = NULL;

    if (envtab == NULL)
        envtab = list_create ((ListDelF) sym_destroy);

    if ((s = sym_create (name, value)))
        list_prepend (envtab, s);
    else
        log_err ("Failed to create env symbol \"%s\". Out of memory?", name);

    return (s);
}

const struct sym * sym (char *name)
{
	const char *rv;
	const struct sym *s;

    if ((s = sym_lookup (keytab, name)))
        return (s);

	if ((s = sym_lookup (symtab, name)))
		return (s);

	if ((rv = xgetenv (name))) 
		return (env_sym_create (name, rv));

	return (NULL);
}

void symtab_destroy ()
{
    if (symtab) {
        list_destroy (symtab);
        symtab = NULL;
    }

    if (envtab) {
        list_destroy (envtab);
        envtab = NULL;
    }
}

void keytab_destroy ()
{
    if (keytab) {
        list_destroy (keytab);
        keytab = NULL;
    }
}

int print_sym (struct sym *s, void *arg)
{
    log_msg (" %s = \"%s\"\n", s->name, s->string);
    return (0);
}

void dump_symbols (void)
{
    log_msg ("Dumping symbols\n");
    if (symtab)
      list_for_each (symtab, (ListForF) print_sym, NULL);
}

void dump_keywords (void)
{
    log_msg ("Dumping keywords\n");
    if (keytab)
      list_for_each (keytab, (ListForF) print_sym, NULL);
}

/****************************************************************************
 *  Initialization and Cleanup
 ****************************************************************************/

void lex_fini ()
{
    symtab_destroy ();

    if (itemcache) {
        list_destroy (itemcache);
        itemcache = NULL;
    }

    if (includes) {
        list_destroy (includes);
        includes = NULL;
    }

    file_info_destroy (current);
    current = NULL;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
