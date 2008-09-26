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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "conf.h"
#include "log.h"

#include "conf-parser.h"

static const char * default_config = "/etc/slurm/slurm-cpuset.conf";

struct cpuset_conf {
    char            filename [1024];

    enum fit_policy policy;                 

    unsigned        filename_valid:1;
    unsigned        reverse_order:1;
    unsigned        alloc_idle_nodes:1;      
    unsigned        use_idle_if_multiple:1;
    unsigned        constrain_mems:1;
    unsigned        kill_orphans:1;
};


/*
 *  Accessor routines
 */
enum fit_policy cpuset_conf_policy (cpuset_conf_t conf)
{
    return (conf->policy);
}

int cpuset_conf_alloc_idle (cpuset_conf_t conf)
{
    return (conf->alloc_idle_nodes);
}

int cpuset_conf_alloc_idle_gt (cpuset_conf_t conf)
{
    return (conf->alloc_idle_nodes && !conf->use_idle_if_multiple);
}

int cpuset_conf_alloc_idle_multiple (cpuset_conf_t conf)
{
    return (conf->alloc_idle_nodes && conf->use_idle_if_multiple);
}

int cpuset_conf_constrain_mem (cpuset_conf_t conf)
{
    return (conf->constrain_mems);
}

int cpuset_conf_kill_orphans (cpuset_conf_t conf)
{
    return (conf->kill_orphans);
}

int cpuset_conf_reverse_order (cpuset_conf_t conf)
{
    return (conf->reverse_order);
}

int cpuset_conf_set_policy (cpuset_conf_t conf, enum fit_policy policy)
{
    if (!conf)
        return (-1);
    conf->policy = policy;
    return (0);
}

int cpuset_conf_set_policy_string (cpuset_conf_t conf, const char *name)
{
    if (strcmp (name, "best-fit") == 0)
        return (cpuset_conf_set_policy (conf, BEST_FIT));
    else if (strcmp (name, "worst-fit") == 0)
        return (cpuset_conf_set_policy (conf, WORST_FIT));
    else if (strcmp (name, "first-fit") == 0) 
        return (cpuset_conf_set_policy (conf, FIRST_FIT));
    else
        return (-1);
}

int cpuset_conf_set_alloc_idle (cpuset_conf_t conf, int alloc_idle)
{
    if (!conf)
        return (-1);
    conf->alloc_idle_nodes = alloc_idle;
    return (0);
}

int cpuset_conf_set_alloc_idle_mode (cpuset_conf_t conf, int multiple_only)
{
    if (!conf)
        return (-1);
    conf->use_idle_if_multiple = multiple_only;
    return (0);
}

int cpuset_conf_set_alloc_idle_string (cpuset_conf_t conf, const char *s)
{
    if (strcmp (s, "0") == 0 ||
        strcasecmp (s, "never") == 0 ||
        strcasecmp (s,    "no") == 0)
        return (cpuset_conf_set_alloc_idle (conf, 0));

    if (strcmp (s, "1") == 0 ||
        strcasecmp (s, "yes") == 0)
        return (cpuset_conf_set_alloc_idle (conf, 1));

    if (strcasecmp (s, "multiple") == 0 || 
        strcasecmp (s, "mult") == 0)
        return (cpuset_conf_set_alloc_idle_mode (conf, 1));

    if (strcasecmp (s, "gt") == 0 || 
        strcasecmp (s, "greater") == 0)
        return (cpuset_conf_set_alloc_idle_mode (conf, 0));

    log_err ("Unknown alloc-idle setting \"%s\"\n", s);

    return (-1);
}

int cpuset_conf_parse_opt (cpuset_conf_t conf, const char *opt)
{
    /*
     *  First check to see if we're setting a policy
     */
    if (cpuset_conf_set_policy_string (conf, opt) == 0)
        return (0);

    if (strncmp ("policy=", opt, 7) == 0) {
        if (cpuset_conf_set_policy_string (conf, opt + 7) < 0)
            return (log_err ("Unknown allocation policy \"%s\"", opt));
    }

    /*
     *  Next check for new config file via "conf="
     */
    if (strncmp ("conf=", opt, 5) == 0) 
        return (cpuset_conf_parse (conf, opt + 5));

    if ((strcmp ("!idle-1st", opt) == 0) || 
        (strcmp ("no-idle", opt) == 0)) 
        return (cpuset_conf_set_alloc_idle (conf, 0));

    if (strncmp ("idle-1st=", opt, 9) == 0)
        return (cpuset_conf_set_alloc_idle_string (conf, opt + 9));

    if (strncmp ("idle-first=", opt, 11) == 0)
        return (cpuset_conf_set_alloc_idle_string (conf, opt + 11));

    if ((strcmp ("!mem", opt) == 0)  || 
        (strcmp ("nomem", opt) == 0) || 
        (strcmp ("!constrain-mem", opt) == 0))
        return (cpuset_conf_set_constrain_mem (conf, 0));

    if ((strcmp ("mem", opt) == 0) ||
        (strcmp ("constrain-mem", opt) == 0))
        return (cpuset_conf_set_constrain_mem (conf, 1));

    if ((strcmp ("reverse", opt) == 0) || 
        (strcmp ("order=reverse", opt) == 0))
        return (cpuset_conf_set_order (conf, 1));

    if ((strcmp ("order=normal", opt) == 0))
        return (cpuset_conf_set_order (conf, 0));

    return (log_err ("Unknown option \"%s\"\n", opt));
}

int cpuset_conf_set_constrain_mem (cpuset_conf_t conf, int constrain_mem)
{
    if (!conf)
        return (-1);
    conf->constrain_mems = constrain_mem;
    return (0);
}

int cpuset_conf_set_kill_orphans (cpuset_conf_t conf, int kill_orphans)
{
    if (!conf)
        return (-1);
    conf->kill_orphans = kill_orphans;
    return (0);
}

int cpuset_conf_set_order (cpuset_conf_t conf, int reverse)
{
    if (!conf)
        return (-1);
    conf->reverse_order = reverse;
    return (0);
}


/*
 *  Create and Destroy:
 */
cpuset_conf_t cpuset_conf_create ()
{
    cpuset_conf_t conf = malloc (sizeof (*conf));

    if (conf == NULL)
        return (NULL);

    memset (conf->filename, 0, sizeof (conf->filename));
    conf->filename_valid =       0;

    /*
     *  Set defaults
     */
    conf->policy =               BEST_FIT;
    conf->reverse_order =        0;
    conf->alloc_idle_nodes =     1;
    conf->use_idle_if_multiple = 1;
    conf->constrain_mems =       1;
    conf->kill_orphans =         0;

    return (conf);
}

void cpuset_conf_destroy (cpuset_conf_t conf)
{
    if (conf) free (conf);
}


/*
 *   Parsing
 */

static int parse_if_exists (cpuset_conf_t conf, const char *file)
{
    if (access (file, F_OK) < 0)
        return (0);

    if (access (file, R_OK) < 0) {
        log_err ("File %s exists but is not readable.\n", file);
        return (-1);
    }

    if (cpuset_conf_parse (conf, file) < 0)
        return (-1);

    /* Successfully read config file */
    return (0);
}

int cpuset_conf_parse_system (cpuset_conf_t conf)
{
    return (parse_if_exists (conf, default_config));
}

const char * cpuset_conf_file (cpuset_conf_t conf)
{
    if (!conf->filename_valid)
        return (NULL);
    return (conf->filename);
}

void cpuset_conf_set_file (cpuset_conf_t conf, const char *file)
{
    strncpy (conf->filename, file, sizeof (conf->filename));
    conf->filename_valid = 1;
}

/*
 *  Later, perhaps allow a per-user conf file in ~/.slurm/cpuset.conf...
 */

/*
 * vi: ts=4 sw=4 expandtab
 */
