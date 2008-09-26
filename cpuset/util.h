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

#ifndef _HAVE_CPUSET_UTIL_H
#define _HAVE_CPUSET_UTIL_H

#include <fcntl.h>

#include <cpuset.h>
#include <bitmask.h>
#include <slurm/slurm.h>
#include <slurm/spank.h>

#include "fd.h"
#include "conf.h"

int cpumask_size (void);
int memmask_size (void);

int slurm_cpuset_lock (void);
int slurm_cpuset_unlock (int fd);

int user_cpuset_lock (uid_t uid);
void user_cpuset_unlock (int fd);

void print_current_cpuset_info ();
void print_cpuset_info (const char *path, struct cpuset *cp);

void print_bitmask (const char * fmt, const struct bitmask *b);

struct bitmask *used_cpus_bitmask_path (char *path, int clearall);

int slurm_cpuset_create (cpuset_conf_t conf);
int slurm_cpuset_clean_path (const char *path);

int str2int (const char *str);

const char * cpuset_path_to_name (const char *path);
#endif

/*
 *  vi: ts=4 sw=4 expandtab
 */
