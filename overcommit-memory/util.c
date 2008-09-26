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

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <libgen.h>

#include "overcommit.h"

char *prog = NULL;

static int cleanup = 0;
static int list_users = 0;
static int force_reset = 0;
static int jobid = -1;

#define __GNU_SOURCE
#include <getopt.h>

struct option opt_table [] = {
    { "help",         0, NULL, 'h' },
    { "cleanup",      0, NULL, 'c' },
    { "list-users",   0, NULL, 'l' },
    { "force-reset",  0, NULL, 'f' },
    { "jobid",        1, NULL, 'j' },
    { NULL,           0, NULL,  0  }
};

const char opt_string[] = "hclfj:";

#define USAGE "\
Usage: %s [OPTONS]\n\
 -h, --help           Display this message\n\
 -l, --list-users     List current jobs using overcommit-memory plugin.\n\
 -c, --cleanup        Cleanup any overcommit-memory usage by a SLURM job.\n\
                       SLURM_JOBID and SLURM_STEPID should be set in current\n\
                       environment. Removes shared memory file and resets\n\
                       overcommit_memory to default if no more references\n\
                       to overcommit-memory exist.\n\
 -f, --force-reset    Force total cleanup of overcommit-memory state. Reset\n\
                       overcommit_memory setting to default and remove\n\
                       overcommit shared file.\n\
 -j, --jobid=ID       Specify SLURM jobid to clean up after if SLURM_JOBID\n\
                       not set in environment\n"

static int get_env_int (const char *var);
static int str2int (const char *str);
static int parse_cmdline (int ac, char **av);
static void log_fatal (char *fmt, ...);

int main (int ac, char *av[])
{
    int stepid = -1;

    parse_cmdline (ac, av);

    if (jobid < 0)
        jobid = get_env_int ("SLURM_JOBID");
    if (stepid < 0)
        stepid = get_env_int ("SLURM_STEPID");

    if (cleanup && jobid < 0)
        log_fatal ("--cleanup requires SLURM_JOBID in environment\n");

    if (!cleanup && !list_users && !force_reset) 
        log_fatal ("Specify one of --cleanup, --force-reset, or --list-users.\n");

    if (list_users)
        overcommit_shared_list_users ();

    if (force_reset) {
        if (overcommit_force_cleanup () < 0)
            return (1);
        printf ("Successfuly reset overcommit-memory state\n");
    }
    else if (cleanup) {
        /*
         *  If overcommit_shared_cleanup returns < 0, this probably just
         *   means that the jobid.stepid is not in the shared memory state.
         */
        if (overcommit_shared_cleanup (jobid, stepid) < 0) 
            printf ("No overcommit state for job %d\n", jobid);
        else
            printf ("Succesfully cleaned up overcommit state for job %d\n", 
                    jobid);
    }

    return (0);
}

static void usage (const char *prog)
{
    fprintf (stderr, USAGE, prog);
}

static int parse_cmdline (int ac, char **av)
{
    prog = basename (av[0]);

    for (;;) {
        char c = getopt_long (ac, av, opt_string, opt_table, NULL);

        if (c == -1)
            break;

        switch (c) {
            case 'h':
                usage (prog);
                exit (0);
            case 'c':
                cleanup = 1;
                break;
            case 'l':
                list_users = 1;
                break;
            case 'f':
                force_reset = 1;
                break;
            case 'j':
                if ((jobid = str2int (optarg)) < 0)
                    log_fatal ("Invalid argument: --jobid=%s\n", optarg);
                break;
            case '?':
                if (optopt > 0)
                    fprintf (stderr, "%s: Invalid option \"-%c\"\n", 
                            prog, optopt);
                else
                    fprintf (stderr, "%s: Invalid option \"%s\"\n", 
                            prog, av[optind-1]);
                break;
            default:
                fprintf (stderr, "%s: Unimplemented option \"%s\"\n",
                        prog, av[optind-1]);
                break;
        }
    }

    return (0);
}

static void log_fatal (char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    fprintf (stderr, "%s: ", prog);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
    exit (1);
}

static int str2int (const char *str)
{
    char *p;
    long l = strtol (str, &p, 10);

    if (p && (*p != '\0'))
        return (-1);

    return ((int) l);
}

static int get_env_int (const char *var)
{
    char *val;
    int id;

    if (!(val = getenv (var)))
        return (-1);

    if ((id = str2int (val)) < 0)
        log_fatal ("Bad environment value: %s=%s\n", var, val);

    return (id);
}


/*
 * vi: ts=4 sw=4 expandtab
 */
