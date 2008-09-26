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

#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <slurm/spank.h>

#include "overcommit.h"

SPANK_PLUGIN (overcommit, 1);

const char env_flag [] = "SPANK_OVERCOMMIT_MEMORY_FLAG";

static int jobid;
static int stepid;
static int overcommit_ratio = 100;
static overcommit_shared_ctx_t ctx = NULL;

static int overcommit_opt_process (int val, const char *arg, int remote);

struct spank_option spank_options [] = 
{
    { "overcommit-memory", "[m]",
      "Choose memory overcommit mode [m] (always|off|on) for all nodes of job.",
      1, 0,
      (spank_opt_cb_f) overcommit_opt_process
    },
    SPANK_OPTIONS_TABLE_END
};

static int set_overcommit_policy (int val)
{
    ctx = overcommit_shared_ctx_create (jobid, stepid);

    if (ctx == NULL)
        return (-1);

    if (overcommit_in_use (ctx, val)) {
        slurm_error ("overcommit-memory: Cannot set desired mode on this node");
        overcommit_shared_ctx_destroy (ctx);
    }
    else if (overcommit_memory_set_current_state (val) < 0)
        slurm_error ("overcommit-memory: Failed to set overcommit = %d", val);
    else if (overcommit_ratio_set (overcommit_ratio) < 0)
        slurm_error ("overcommit-memory: Failed to set overcommit_ratio to %d\n",
                     overcommit_ratio);

    return (0);
}

static int strnmatch (const char *src, int n, ...)
{
    int i = 0;
    int rc = 0;
    va_list ap;

    va_start (ap, n);

    while ((i++ < n) && !(rc = (strcmp (src, va_arg (ap, char *)) == 0))) {;}

    va_end (ap);

    return (rc);
}

static int overcommit_opt_process (int val, const char *arg, int remote)
{
    int overcommit_mode = 0;

    if (strnmatch (arg, 4, "off", "no", "never", "2"))
        overcommit_mode = 2;
    else if (strnmatch (arg, 2, "always", "1"))
        overcommit_mode = 1;
    else if (strnmatch (arg, 2, "on", "yes", "0"))
        overcommit_mode = 0;
    else {
        slurm_error ("--overcommit-memory: invalid argument: %s", arg);
        return (-1);
    }

    if (!remote) {
        /*  Need to set a flag in environment so slurmd knows that a 
         *   command line option is called and won't apply any environment
         *   options.
         */
        setenv ("SPANK_OVERCOMMIT_MEMORY_FLAG", "1", 1);
        return (0);
    }

    if (set_overcommit_policy (overcommit_mode) < 0)
        return (-1);

    return (0);
}

static int check_env (spank_t sp, int remote)
{
    char buf [64];
    const char var[] = "SLURM_OVERCOMMIT_MEMORY";

    /*  If env_flag is set in environment, ignore options set from
     *   environment since command line option should override
     */
    if (spank_getenv (sp, env_flag, buf, sizeof (buf)) == ESPANK_SUCCESS) { spank_unsetenv (sp, env_flag);
        return (0);
    }
    
    if (spank_getenv (sp, var, buf, sizeof (buf)) == ESPANK_SUCCESS) {
        if (overcommit_opt_process (0, buf, remote) < 0) {
            slurm_error ("Environment setting %s=%s invalid", var, buf);
            return (-1);
        }
    }

    return (0);
}

static int str2int (const char *str)
{
    char *p;
    long l = strtol (str, &p, 10);

    if (p && (*p != '\0'))
        return (-1);

    return ((int) l);
}

int parse_options (int ac, char **av)
{
    int i;
    int retval = 0;

    for (i = 0; i < ac; i++) {
        if (strncmp ("ratio=", av[i], 6) == 0) {
            char *ratio = av[i] + 6;
            if ((overcommit_ratio = str2int (ratio)) < 0) {
                slurm_error ("overcommit-memory: Invalid ratio = %s\n", ratio);
                retval = -1;
            }
        }
        else  {
            slurm_error ("overcommit-memory: Invalid option %s\n", av[i]);
            retval = -1;
        }
    }

    return (retval);
}

int slurm_spank_init (spank_t sp, int ac, char **av)
{
    if (parse_options (ac, av) < 0)
        return (-1);

    if (!spank_remote (sp)) {
        if (check_env (sp, 0) < 0)
            return (-1);
        return (0);
    }

    /*
     *  Set jobid and stepid from spank_init. Options are processed
     *    *after* spank_init, but the option handler does not have access
     *    to the spank_t handle.
     */
    spank_get_item (sp, S_JOB_ID, &jobid);
    spank_get_item (sp, S_JOB_STEPID, &stepid);

    if (check_env (sp, 1) < 0)
        return (-1);

    return (0);
}


int slurm_spank_exit (spank_t sp, int ac, char **av)
{
    if (!spank_remote (sp) || !ctx)
        return (0);

    overcommit_shared_ctx_unregister (ctx);

    return (0);
}


/*
 * vi: ts=4 sw=4 expandtab
 */
