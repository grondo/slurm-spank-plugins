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
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "util.h"
#include "create.h"
#include "conf.h"
#include "log.h"

const char cpuset_path[] = "/dev/cpuset";

const char * basename (const char *path);
static FILE *fp = NULL;

static int log_fp (const char *msg) 
{
    if (fp)
        fprintf (fp, "%s", msg);
    return (0);
}

int main (int ac, char **av)
{
    int lockfd;
    char path [4096];
    const char *prog = basename (av[0]);

    cpuset_conf_t conf = cpuset_conf_create ();

    if (ac < 2) {
        fprintf (stderr, "Usage: %s cpuset_path\n", prog); 
        return (1);
    }

    fp = fopen ("/var/log/slurm-cpuset.log", "a");

    log_add_dest (C_LOG_VERBOSE, log_fp);
    cpuset_conf_parse_system (conf); /* Ignore errors, we must proceed */

    snprintf (path, sizeof (path), "%s%s", cpuset_path, av[1]);

    if ((lockfd = slurm_cpuset_create (conf)) < 0) {
        log_err ("Failed to lock slurm cpuset: %s\n", strerror (errno));
        exit (1);
    }

    log_verbose ("Cleaning path %s\n", path);

    update_user_cpusets (conf);
    slurm_cpuset_unlock (lockfd);
    cpuset_conf_destroy (conf);
    fclose (fp);

    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
