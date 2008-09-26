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


#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"
#include "log.h"

static char facility [64] = "cpuset";

struct logger {
    int      level;
    log_f   *logfn;
};

static List log_list = NULL;

static struct logger * logger_create (int level, log_f *fn)
{
    struct logger *l = malloc (sizeof (*l));

    if (l != NULL) {
        l->level = level;
        l->logfn = fn;
    }

    return (l);
}

void logger_destroy (struct logger *l)
{
    free (l);
}

int log_add_dest (int level, log_f *fn) 
{
    struct logger *l;

    if (log_list == NULL) {
        log_list = list_create ((ListDelF) logger_destroy);
    }

    if ((l = logger_create (level, fn)) == NULL) 
        return (-1);

    list_push (log_list, l);
    return (0);
}

int log_set_prefix (const char *prefix)
{
    strncpy (facility, prefix, sizeof (facility));
    return (0);
}

int find_fn (struct logger *l, log_f *fn)
{
    return (l->logfn == fn);
}

int log_update (int level, log_f *fn)
{
    struct logger *l = list_find_first (log_list, (ListFindF) find_fn, fn);

    if (l == NULL)
        return (-1);

    l->level = level;
    return (0);
}


void log_cleanup ()
{
    list_destroy (log_list);
}

static int do_log_all (int level, const char *buf)
{
    struct logger *l;
    ListIterator i = list_iterator_create (log_list);

    while ((l = list_next (i))) {
        if (l->level >= level)
            (*l->logfn) (buf);
    }

    list_iterator_destroy (i);
    return (0);
}

static void vlog_msg (const char *prefix, int level, const char *format, va_list ap)
{
    char  buf[4096];
    char *p;
    int   n;
    int   len;

    if (!log_list)
        return;

    p = buf;
    len = sizeof (buf);

    if (strlen (facility)) {
        n = snprintf (p, len, "%s: ", facility);
        if ((n < 0) || (n >= len)) {
            p += len - 1;
            len = 0;
        }
        else {
            p += n;
            len -= n;
        }
    }

    /*  Add a log level prefix.
     */
    if ((len > 0) && prefix) {
        n = snprintf (p, len, "%s: ", prefix);
        if ((n < 0) || (n >= len)) {
            p += len - 1;
            len = 0;
        }
        else {
            p += n;
            len -= n;
        }
    }

    if ((len > 0) && (format)) {
        n = vsnprintf (p, len, format, ap);
        if ((n < 0) || (n >= len)) {
            p += len - 1;
            len = 0;
        }
        else {
            p += n;
            len -= n;
        }
    }

    /*  Add suffix for truncation if necessary.
     */
    if (len <= 0) {
        char *q;
        const char *suffix = "+";
        q = buf + sizeof (buf) - 1 - strlen (suffix);
        p = (p < q) ? p : q;
        strcpy (p, suffix);
        p += strlen (suffix);
    }

    *p = '\0';

    do_log_all (level, buf);

    return;
}

int log_err (const char *format, ...)
{
    va_list ap;
    va_start (ap, format);
    vlog_msg ("Error", -1, format, ap);
    va_end (ap);
    return (-1); /* So we can do return (log_err (...)) */
}

void log_msg (const char *format, ...)
{
    va_list ap;
    va_start (ap, format);
    vlog_msg (NULL, 0, format, ap);
    va_end (ap);
    return;
}

void log_verbose (const char *format, ...)
{
    va_list ap;
    va_start (ap, format);
    vlog_msg (NULL, 1, format, ap);
    va_end (ap);
    return;
}

void log_debug (const char *format, ...)
{
    va_list ap;
    va_start (ap, format);
    vlog_msg ("Debug", 2, format, ap);
    va_end (ap);
    return;
}

void log_debug2 (const char *format, ...)
{
    va_list ap;
    va_start (ap, format);
    vlog_msg ("Debug", 3, format, ap);
    va_end (ap);
    return;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
