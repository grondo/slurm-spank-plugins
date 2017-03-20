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

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/resource.h>

#include <slurm/spank.h>

/*
 * All spank plugins must define this macro for the SLURM plugin loader.
 */
SPANK_PLUGIN(renice, 1)

#define PRIO_ENV_VAR "SLURM_RENICE"
#define PRIO_NOT_SET 42

/*
 *  Minimum allowable value for priority. May be set globally
 *   via plugin option min_prio=<prio>
 */
static int min_prio =     -20;
static int default_prio =   0;

static int prio = PRIO_NOT_SET;

static int _renice_opt_process (int val, const char *optarg, int remote);
static int _str2prio (const char *str, int *p2int);
static int _check_env (spank_t sp);

/*
 *  Provide a --renice=[prio] option to srun:
 */
struct spank_option spank_options[] =
{
    { "renice", "[prio]", "Re-nice job tasks to priority [prio].", 1, 0,
        (spank_opt_cb_f) _renice_opt_process
    },
    SPANK_OPTIONS_TABLE_END
};


/*
 *  Called from both srun and slurmd.
 */
int slurm_spank_init (spank_t sp, int ac, char **av)
{
    int i;

    for (i = 0; i < ac; i++) {
        if (strncmp ("min_prio=", av[i], 9) == 0) {
            const char *optarg = av[i] + 9;
            if (_str2prio (optarg, &min_prio) < 0) 
                slurm_error ("Ignoring invalid min_prio value \"%s\"", av[i]);
        }
        else if (strncmp ("default=", av[i], 8) == 0) {
            const char *optarg = av[i] + 8;
            if (_str2prio (optarg, &default_prio) < 0)
                slurm_error ("renice: Ignoring invalid default value \"%s\"",
                             av[i]);
        }
        else {
            slurm_error ("renice: Invalid option \"%s\"", av[i]);
        }
    }

    if (!spank_remote (sp))
        slurm_verbose ("renice: min_prio = %d", min_prio);

    return (0);
}


int slurm_spank_task_post_fork (spank_t sp, int ac, char **av)
{
    pid_t pid;
    int taskid;

    /*
     *  Use default priority if prio not set by command line or env var
     */
    if ((prio == PRIO_NOT_SET) && (_check_env (sp) < 0))
        prio = default_prio;

    if (prio < min_prio) 
        prio = min_prio;

    spank_get_item (sp, S_TASK_GLOBAL_ID, &taskid);
    spank_get_item (sp, S_TASK_PID, &pid);

    /*
     *  No need to do any thing if priority is system default
     */
    if (prio == getpriority (PRIO_PROCESS, (int) pid))
        return (0);

    slurm_verbose ("re-nicing task%d pid %d to %d\n", taskid, pid, prio);

    if (setpriority (PRIO_PROCESS, (int) pid, (int) prio) < 0) {
        slurm_error ("setpriority: %m");
        return (-1);
    }

    return (0);
}

static int _renice_opt_process (int val, const char *optarg, int remote)
{
    if (optarg == NULL) {
        slurm_error ("--renice: invalid argument!");
        return (-1);
    }
        
    if (_str2prio (optarg, &prio) < 0) {
        slurm_error ("Bad value for --renice: \"%s\"\n", optarg);
        return (-1);
    }

    if (prio < min_prio) 
        slurm_error ("--renice=%d not allowed, will use min=%d", 
                     prio, min_prio);

    return (0);
}

static int _str2prio (const char *str, int *p2int)
{
    long int l;
    char *p;

    l = strtol (str, &p, 10);
    if ((*p != '\0') || (l < -20) || (l > 20)) 
        return (-1);

    *p2int = (int) l;

    return (0);
}

static int _check_env (spank_t sp)
{
    /* 
     *  See if SLURM_RENICE env var is set by user
     */
    char val [1024];

    if (spank_getenv (sp, PRIO_ENV_VAR, val, 1024) != ESPANK_SUCCESS)
       return (-1); 

    if (_str2prio (val, &prio) < 0) {
        slurm_error ("Bad value for %s: \"%s\".\n", PRIO_ENV_VAR, val);
        return (-1);
    }

    if (prio < min_prio) {
        slurm_error ("%s=%d not allowed, using min=%d",
                PRIO_ENV_VAR, prio, min_prio);
    }

    return (0);
}


/*
 * vi: ts=4 sw=4 expandtab
 */
