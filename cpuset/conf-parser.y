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
#include <errno.h>

#include "conf.h"
#include "log.h"

extern int yylex ();
void yyerror (const char *s);
extern FILE *yyin;

static int cpuset_conf_line;

#define YYSTYPE char *
#define YYDEBUG 1
int yydebug = 0;

static int cf_policy (const char *);
static int cf_use_idle (const char *);
static int cf_order (const char *);
static int cf_const_mem (int);
static int cf_kill_orphs (int);

%}

%token POLICY       "policy"
%token USE_IDLE     "use-idle"
%token CONST_MEM    "constrain-mem"
%token KILL_ORPHS   "kill-orphs"
%token ORDER        "order"
%token TRUE         "true"
%token FALSE        "false"
%token STRING       "string"

%error-verbose

%%

file    : /* empty */
        | file stmts
        ;

stmts   : end
        | stmt end
        | stmts stmt
        ;

stmt    : POLICY '=' STRING      { if (cf_policy ($3) < 0)       YYABORT; }
        | USE_IDLE '=' STRING    { if (cf_use_idle ($3) < 0)     YYABORT; }
        | USE_IDLE '=' FALSE     { if (cf_use_idle ("no") < 0)   YYABORT; }
        | USE_IDLE '=' TRUE      { if (cf_use_idle ("yes") < 0)  YYABORT; }
        | CONST_MEM '=' TRUE     { if (cf_const_mem (1) < 0)     YYABORT; }
        | CONST_MEM '=' FALSE    { if (cf_const_mem (0) < 0)     YYABORT; }
        | KILL_ORPHS '=' TRUE    { if (cf_kill_orphs (1) < 0)    YYABORT; }
        | KILL_ORPHS '=' FALSE   { if (cf_kill_orphs (0) < 0)    YYABORT; }
        | ORDER '=' STRING       { if (cf_order ($3) < 0)        YYABORT; }

end     : '\n'                   { cpuset_conf_line++; }
        | ';'
        ;

%%

static cpuset_conf_t conf;
static const char * cpuset_conf_filename = NULL;

void cpuset_conf_debug ()
{
    yydebug = 1;
}

static const char * cf_file ()
{
    if (!cpuset_conf_filename)
        return ("stdin");
    return (cpuset_conf_filename);
}

static int cf_line ()
{
    return (cpuset_conf_line);
}

void yyerror (const char *s)
{
    log_err ("%s: %d: %s\n", cf_file (), cf_line (), s);
}

int cpuset_conf_parse (cpuset_conf_t cf, const char *path)
{
    cpuset_conf_filename = NULL;

    cpuset_conf_set_file (cf, path);

    if (strcmp (path, "-") == 0)
        yyin = stdin;
    else if (!(yyin = fopen (path, "r"))) {
        int err = errno;
        log_err ("open: %s: %s\n", path, strerror (errno));
        errno = err;
        return (-1);
    }

    cpuset_conf_filename = path;
    cpuset_conf_line = 1;
    conf = cf;

    log_debug ("reading config from \"%s\"\n", cf_file ());

    if (yyparse ()) {
        log_err ("%s: %d: parser failed\n", cf_file (), cf_line ());
        errno = 0;
        return (-1);
    }

    fclose (yyin);

    return (0);
}

static int cf_policy (const char *name) 
{
    log_debug ("%s: %d: Setting allocation policy to %s.\n", 
            cf_file (), cf_line(), name);
    if (cpuset_conf_set_policy_string (conf, name) < 0)
        return log_err ("%s: %d: Invalid allocation policy '%s'.\n", 
                cf_file (), cf_line (), name);
    return (0);
}

static int cf_use_idle (const char *s)
{
    log_debug ("%s: %d: Setting idle node use policy to %s.\n", 
            cf_file (), cf_line(), s);
    if (cpuset_conf_set_alloc_idle_string (conf, s) < 0) 
        return log_err ("%s: %d: Invalid alloc-idle string '%s'\n", 
                cf_file (), cf_line (), s);
    return (0);
}

static int cf_order (const char *s)
{
    log_debug ("%s: %d: Setting order to %s.\n",
            cf_file (), cf_line (), s);

    if (strcasecmp (s, "reverse") == 0)
        return cpuset_conf_set_order (conf, 1);
    else if (strcasecmp (s, "normal") == 0)
        return cpuset_conf_set_order (conf, 0);

    return log_err ("%s: %d: Invalid setting for order: %s\n", 
            cf_file (), cf_line (), s);
}

static int cf_const_mem (int val)
{
    log_debug ("%s: %d: Setting constrain-memsto %s.\n", 
            cf_file (), cf_line(), val ? "true" : "false");
    return (cpuset_conf_set_constrain_mem (conf, val));
}

static int cf_kill_orphs (int val)
{
    log_debug ("%s: %d: Setting kill-orphans to %s.\n", 
            cf_file (), cf_line(), val ? "true" : "false");
    return (cpuset_conf_set_kill_orphans (conf, val));
}

/*
 * vi: ts=4 sw=4 expandtab
 */

