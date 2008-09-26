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


#ifndef _HAVE_CREATE_H
#define _HAVE_CREATE_H

#include <bitmask.h>
#include <unistd.h>
#include <stdint.h>

#include "conf.h"

int job_cpuset_exists (uint32_t jobid, uid_t uid);

int create_cpuset_for_job (cpuset_conf_t cf,
		unsigned int jobid, uid_t uid, int ncpus);

int create_cpuset_for_step (cpuset_conf_t cf,
		unsigned int stepid, int ncpus);

int create_cpuset_for_task (cpuset_conf_t cf,
		unsigned int taskid, int ncpus_per_task);

int user_cpuset_update (cpuset_conf_t cf, 
		uid_t uid, const struct bitmask *b);

int update_user_cpusets ();

#endif 
