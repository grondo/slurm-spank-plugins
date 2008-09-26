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


#ifndef _CPUSET_CONF_H
#define _CPUSET_CONF_H

typedef struct cpuset_conf * cpuset_conf_t;

/*
 *  Valid allocation policies for cpusets
 */
enum fit_policy {
    BEST_FIT,
    FIRST_FIT,
    WORST_FIT,
};


/*
 *  Accessor routines
 */
enum fit_policy cpuset_conf_policy (cpuset_conf_t conf);

int cpuset_conf_alloc_idle (cpuset_conf_t conf);

int cpuset_conf_constrain_mem (cpuset_conf_t conf);

int cpuset_conf_alloc_idle_gt (cpuset_conf_t conf);

int cpuset_conf_alloc_idle_multiple (cpuset_conf_t conf);

int cpuset_conf_kill_orphans (cpuset_conf_t conf);

int cpuset_conf_reverse_order (cpuset_conf_t conf);

int cpuset_conf_set_policy (cpuset_conf_t conf, enum fit_policy policy);

int cpuset_conf_set_alloc_idle (cpuset_conf_t conf, int alloc_idle);

int cpuset_conf_set_alloc_idle_mode (cpuset_conf_t conf, int multiple_only);

int cpuset_conf_set_kill_orphans (cpuset_conf_t conf, int kill_orphans);

int cpuset_conf_set_alloc_idle_string (cpuset_conf_t conf, const char *s);

int cpuset_conf_set_policy_string (cpuset_conf_t conf, const char *name);

int cpuset_conf_set_constrain_mem (cpuset_conf_t conf, int constrain_mem);

int cpuset_conf_set_order (cpuset_conf_t conf, int reverse);
/*
 *  Create and Destroy:
 */
cpuset_conf_t cpuset_conf_create ();

void cpuset_conf_destroy (cpuset_conf_t conf);


/*
 *   Parsing
 */

int cpuset_conf_parse (cpuset_conf_t conf, const char *path);

int cpuset_conf_parse_system (cpuset_conf_t conf);

int cpuset_conf_parse_opt (cpuset_conf_t conf, const char *opt);

/*
 *  Return filename of last config file parsed
 */
const char *cpuset_conf_file (cpuset_conf_t conf);

void cpuset_conf_set_file (cpuset_conf_t conf, const char *file);

#endif
/*
 * vi: ts=4 sw=4 expandtab
 */
