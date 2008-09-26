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

#ifndef _USE_ENV_H
#define _USE_ENV_H

enum { TYPE_STR, TYPE_INT, TYPE_SYM };
enum { SYM_INT, SYM_STR };

struct lex_item {
    int    used;   /* Is item still used (for item cache) */
    char * name;   /* Name of item                        */
    int    type;   /* Type of item (int, string, symbol)  */
    char * str;    /* String representation of item       */

    union {        /* Union of different item types       */
        int num;
        char *str;
        const struct sym *sym;
    } val;
};

struct sym {
    char * name;   /* Name of symbol                     */
    int    type;   /* Type of symbol (INT || STRING)     */
    int    val;    /* Value if type is INT               */
    char * string; /* String representation              */
};

typedef char * (*getenv_f) (void *arg, const char *name);
typedef int (*unsetenv_f) (void *arg, const char *name);
typedef int (*setenv_f) (void *arg, const char *name, 
                         const char *value, int overwrite);

struct use_env_ops {
    getenv_f   getenv;
    setenv_f   setenv;
    unsetenv_f unsetenv;
};

/*
 *  Environment manipulation
 */
const char * xgetenv (const char *name);
int xunsetenv (const char *name);
int xsetenv (const char *name, const char *value, int overwrite);


/*
 *  Parser operations:
 */
void use_env_parser_init ();
void use_env_set_operations (struct use_env_ops *ops, void *arg);
int use_env_parse (const char *filename);
void use_env_parser_fini ();

/*
 *  Lexer cleanup
 */
void lex_fini (); 

/*
 *  lex_item functions
 */
void lex_item_cache_clear ();
struct lex_item * lex_item_create (char *name, int type);
int is_valid_identifier (const char *s);

int item_cmp (int cmp, struct lex_item *x, struct lex_item *y);
int item_strcmp (struct lex_item *x, struct lex_item *y);
char * item_str (struct lex_item *item);
int item_val (struct lex_item *item);
int item_type_int (struct lex_item *i);

/*
 *  symbol lookup and definition functions
 */
const struct sym * sym (char *name);
const struct sym * sym_define (char *name, const char *value);
const struct sym * keyword_define (char *name, const char *value);
int sym_delete (char *name);
int env_cache_delete (char *name);
void symtab_destroy ();
void keytab_destroy ();
void dump_keywords ();
void dump_symbols ();

/*
 * include file functions
 */
int lex_file_init (const char *file);
int lex_include_push (const char *include);
int lex_include_pop ();

const char *lex_file ();
int lex_line ();
int lex_line_increment ();

#endif
/*
 * vi: ts=4 sw=4 expandtab
 */
