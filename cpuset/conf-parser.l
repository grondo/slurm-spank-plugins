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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "conf.h"

#define YYSTYPE char *
#include "conf-parser.h"

%}

%option nounput

%%

#[^\n]*           ; /* Ignore comments.   */
[ \t\r]+          ; /* Ignore whitespace. */
;                 { return ';'; }
,                 { return ','; }
(#.*)?\\?\n       { return '\n'; }

\"[^\"]*\" |
\'[^\']*\'        {
                    yytext [strlen (yytext) - 1] = '\0';
                    yylval = strdup (yytext+1);
                    return STRING;
                   }

(fit-)?policy     { return POLICY;       }
order             { return ORDER;        }
use-idle |
alloc-idle        { return USE_IDLE;     }
constrain-mem(s)? { return CONST_MEM;    }
kill-orph(an)?s   { return KILL_ORPHS;   }
=                 { return '='; }

0   |
no  |
No                { return FALSE;        }
1   | 
yes |
Yes               { return TRUE;         }

[^=;, \t\r\n]+    {
                     yylval = strdup (yytext);
                     return STRING;
                  }

%%

/*
 * vi: ts=4 sw=4 expandtab
 */
