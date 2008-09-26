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


#include <cpuset.h>
#include <bitmask.h>
#include <stdio.h>
#include <stdlib.h>

#include "nodemap.h"
#include "util.h"
#include "conf.h"
#include "log.h"

static int log_stderr (const char *msg) 
{ 
    fprintf (stderr, "%s", msg); return 0; 
}

int main (int ac, char **av)
{
    cpuset_conf_t conf;
    struct bitmask * b;
    struct nodemap * map;
    int n = str2int (av[1]);

    log_add_dest (4, log_stderr);

    conf = cpuset_conf_create ();
    //cpuset_conf_debug ();

    if (cpuset_conf_parse_system (conf) < 0)
        exit (1);

    if (ac < 2) 
        exit (1);

    if (av[1] == NULL || ((n = str2int (av[1])) <= 0)) {
        fprintf (stderr, "Usage: %s NCPUS\n", av[0]);
        exit (1);
    }

    fprintf (stdout, "Faking a job with %d CPUs\n", n);

    if ((map = nodemap_create (conf, NULL)) == NULL) {
        fprintf (stderr, "Failed to create nodemap\n");
        exit (1);
    }

    print_nodemap (map);

    if (!(b = nodemap_allocate (map, n))) {
        fprintf (stderr, "Failed to allocate %d tasks in nodemap\n", n);
        exit (1);
    }

    print_bitmask ("Used CPUs: %s\n", nodemap_used (map));

    nodemap_destroy (map);

    cpuset_conf_destroy (conf);

    exit (0);

}

/*
 * vi: ts=4 sw=4 expandtab
 */
